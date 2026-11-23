/*
 * serve.c: `opticnerve serve`, the QNX driver of the core. One thread on a dispatch loop: sockets through
 * select_attach(), Screen's composition signal, the timer and SIGTERM as pulses. The core (agent/core) decides; this
 * file only does what the core asks and tells it what happened.
 *
 *   opticnerve serve [-display=N] [-port=P] [-passfile=PATH] [-holdoff=MS] [-logintimeout=S] [-v]
 *   -logintimeout=S is the time a viewer's user has to type the password (default 60, 0: no limit).
 *   -v logs every connection and every injected event to standard error (key values included: test use only).
 *
 * Runs in the foreground and logs to standard error (detaching comes later). Needs root: the display manager and
 * input provider context types.
 */
#define _QNX_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/dispatch.h>
#include <sys/keycodes.h>
#include <sys/neutrino.h>
#include <sys/socket.h>
#include <screen/screen.h>
#include "on_core.h"
#include "on_des.h"
#include "on_dev.h"
#include "device.h"
#include <sys/procmgr.h>

#define ON_VERSION "0.1.0"
#define PULSE_TIMER (_PULSE_CODE_MINAVAIL + 0)
#define PULSE_SCREEN (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_TERM (_PULSE_CODE_MINAVAIL + 2)
#define PULSE_HUP (_PULSE_CODE_MINAVAIL + 3)

static dispatch_t *dpp;
static struct on_core *core;
static int coid, quit, reload, listen_fd = -1;
static const char *passfile = "/data/opticnerve/etc/vncpasswd";
static int fds[ON_SLOTS], want_write[ON_SLOTS];
static timer_t timer;
static screen_context_t sctx;
static screen_display_t disp;
static screen_device_t kbd, ptr;
static screen_pixmap_t pixmap;
static screen_buffer_t pbuf;
static uint8_t *pixels, *staging; /* the pixmap's memory, and the copy of each capture in ordinary memory */
static int pstride;
static int dsize[2], ptr_x, ptr_y, verbose;

static on_time now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (on_time)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void die(const char *what)
{
	fprintf(stderr, "opticnerve: %s: %s\n", what, strerror(errno));
	exit(1);
}

static void get_random(uint8_t *p, size_t n)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, p, n) != (ssize_t)n)
		die("/dev/urandom");
	close(fd);
}

/* --- Screen --------------------------------------------------------------------------------------------------- */

static void screen_setup(int display_no)
{
	if (screen_create_context(&sctx, SCREEN_DISPLAY_MANAGER_CONTEXT | SCREEN_INPUT_PROVIDER_CONTEXT) != 0)
		die("screen_create_context (needs root)");
	int count = 0;
	screen_get_context_property_iv(sctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count);
	if (count < display_no || display_no < 1) {
		errno = ENODEV;
		die("display");
	}
	screen_display_t *d = calloc((size_t)count, sizeof(*d));
	screen_get_context_property_pv(sctx, SCREEN_PROPERTY_DISPLAYS, (void **)d);
	disp = d[display_no - 1];
	free(d);
	screen_get_display_property_iv(disp, SCREEN_PROPERTY_SIZE, dsize);

	/* A keyboard device of our own: Screen keeps the modifiers of key events that name a device, and drops them from
	 * events that do not (logged with agent/spikes/input_log.c, 2026-11-16). Without it no Ctrl, Alt or Shift. */
	if (screen_create_device_type(&kbd, sctx, SCREEN_EVENT_KEYBOARD) == 0) {
		screen_set_device_property_cv(kbd, SCREEN_PROPERTY_ID_STRING, (int)strlen("opticnerve-kbd"), "opticnerve-kbd");
		screen_flush_context(sctx, 0);
	} else {
		kbd = NULL;
		fprintf(stderr, "opticnerve: no keyboard device of our own (%s): keys go without modifiers\n", strerror(errno));
	}
	/* A pointer device of our own, as for the keyboard: pointer events name it, as a real mouse's do. (It did not
	 * bring back the launcher's clicks after an app had closed, 2026-11-19; the events reached Screen without it.) */
	if (screen_create_device_type(&ptr, sctx, SCREEN_EVENT_POINTER) == 0) {
		screen_set_device_property_cv(ptr, SCREEN_PROPERTY_ID_STRING, (int)strlen("opticnerve-ptr"), "opticnerve-ptr");
		screen_flush_context(sctx, 0);
	} else {
		ptr = NULL;
		fprintf(stderr, "opticnerve: no pointer device of our own (%s): pointer events name no device\n", strerror(errno));
	}

	int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ, format = SCREEN_FORMAT_RGBA8888;
	if (screen_create_pixmap(&pixmap, sctx) != 0)
		die("screen_create_pixmap");
	screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_BUFFER_SIZE, dsize);
	if (screen_create_pixmap_buffer(pixmap) != 0)
		die("screen_create_pixmap_buffer");
	screen_get_pixmap_property_pv(pixmap, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&pbuf);
	/* Each capture is copied once, row by row, into `staging` (ordinary memory) before the core compares it: on the Pi
	 * the comparison runs several times faster on the copy than on the capture buffer in place, copy included
	 * (measured 2026-11-22). */
	if (screen_get_buffer_property_pv(pbuf, SCREEN_PROPERTY_POINTER, (void **)&pixels) != 0 || !pixels ||
	    screen_get_buffer_property_iv(pbuf, SCREEN_PROPERTY_STRIDE, &pstride) != 0 || pstride < dsize[0] * 4)
		die("pixmap pointer or stride");
	if (!(staging = malloc((size_t)dsize[0] * 4 * (size_t)dsize[1])))
		die("malloc (capture staging buffer)");

	/* the composition signal, registered once (design 4.5) */
	struct sigevent ev;
	SIGEV_PULSE_INIT(&ev, coid, SIGEV_PULSE_PRIO_INHERIT, PULSE_SCREEN, 0);
	if (screen_register_event(sctx, &ev) != 0 || screen_notify(sctx, SCREEN_NOTIFY_UPDATE, disp, &ev) != 0)
		die("screen_notify");
}

static void capture(void)
{
	on_time t0 = now_ns();
	if (screen_read_display(disp, pbuf, 0, NULL, 0) != 0)
		die("screen_read_display"); /* design 4.3 I7: Screen lost: exit, no recovery inside the process */
	on_time t1 = now_ns();
	size_t row = (size_t)dsize[0] * 4;
	for (int y = 0; y < dsize[1]; y++)
		memcpy(staging + y * row, pixels + (size_t)y * pstride, row);
	/* started/ended stay the read itself (4.5): the copy and the comparison run after it, as the comparison did */
	on_captured(core, staging, (int)row, t0, t1);
}

/* X keysym to QNX key code: characters are their own code; the 0xff00 block maps onto KEYCODE_PC_KEYS with the same
 * low byte. Linefeed has no key and is not injected (the guardrail: it is not turned into Return). */
static int map_key(uint32_t sym)
{
	if (sym >= 0x20 && sym <= 0xff)
		return (int)sym;
	if (sym == 0xff0a)
		return -1;
	if (sym >= 0xff00 && sym <= 0xffff)
		return KEYCODE_PC_KEYS + (int)(sym & 0xff);
	return -1;
}

/* X keysym to the USB keyboard usage code a real keyboard puts in SCREEN_PROPERTY_SCAN (US layout); 0 if none.
 * Some applications read it: the quick-start image's Terminal (st) ends a line only on a Return that carries its
 * scan code, 0x28, as the real keyboard's does (logged with agent/spikes/input_log.c, 2026-11-16). */
static int map_scan(uint32_t sym)
{
	static const char shifted_digits[] = ")!@#$%^&*(";
	static const struct { const char *keys; int usage; } punct[] = {
		{ " ", 0x2c }, { "-_", 0x2d }, { "=+", 0x2e }, { "[{", 0x2f }, { "]}", 0x30 }, { "\\|", 0x31 },
		{ ";:", 0x33 }, { "'\"", 0x34 }, { "`~", 0x35 }, { ",<", 0x36 }, { ".>", 0x37 }, { "/?", 0x38 },
	};
	static const struct { uint32_t sym; int usage; } named[] = {
		{ 0xff0d, 0x28 }, { 0xff1b, 0x29 }, { 0xff08, 0x2a }, { 0xff09, 0x2b }, { 0xff61, 0x46 }, { 0xff63, 0x49 },
		{ 0xff50, 0x4a }, { 0xff55, 0x4b }, { 0xffff, 0x4c }, { 0xff57, 0x4d }, { 0xff56, 0x4e }, { 0xff53, 0x4f },
		{ 0xff51, 0x50 }, { 0xff54, 0x51 }, { 0xff52, 0x52 }, { 0xff8d, 0x58 }, { 0xffe3, 0xe0 }, { 0xffe1, 0xe1 },
		{ 0xffe9, 0xe2 }, { 0xffeb, 0xe3 }, { 0xffe4, 0xe4 }, { 0xffe2, 0xe5 }, { 0xffea, 0xe6 }, { 0xffec, 0xe7 },
	};
	if (sym >= 'a' && sym <= 'z')
		return 0x04 + (int)(sym - 'a');
	if (sym >= 'A' && sym <= 'Z')
		return 0x04 + (int)(sym - 'A');
	if (sym >= '1' && sym <= '9')
		return 0x1e + (int)(sym - '1');
	if (sym == '0')
		return 0x27;
	if (sym >= 0xffbe && sym <= 0xffc9) /* F1 to F12 */
		return 0x3a + (int)(sym - 0xffbe);
	if (sym > 0 && sym < 0x80) {
		const char *d = strchr(shifted_digits, (int)sym);
		if (d)
			return d == shifted_digits ? 0x27 : 0x1e + (int)(d - shifted_digits) - 1;
		for (size_t i = 0; i < sizeof(punct) / sizeof(punct[0]); i++)
			if (strchr(punct[i].keys, (int)sym))
				return punct[i].usage;
	}
	for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
		if (named[i].sym == sym)
			return named[i].usage;
	return 0;
}

/* The character on the same key without Shift (US layout): 'A' -> 'a', ':' -> ';', '!' -> '1'; others unchanged. */
static uint32_t unshifted(uint32_t sym)
{
	static const char from[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ)!@#$%^&*(_+{}|:\"~<>?";
	static const char to[] = "abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;'`,./";
	const char *p = sym > 0 && sym < 0x80 ? strchr(from, (int)sym) : NULL;
	return p ? (uint32_t)(unsigned char)to[p - from] : sym;
}

/* Modifier keys held by the client, as KEYMOD_* bits. Some applications take Shift, Ctrl and Alt from the event's
 * SCREEN_PROPERTY_MODIFIERS rather than from the modifier keys' own events: the Developer Desktop (Weston) typed Ctrl+A
 * as "a" and ':' as ';' without them (2026-11-16). */
static int held_mods;

static const char *stamp(void);

static void inject_key(uint32_t sym, int down)
{
	int code = map_key(sym);
	if (code < 0)
		return;
	int bit = sym == 0xffe1 || sym == 0xffe2 ? KEYMOD_SHIFT
		: sym == 0xffe3 || sym == 0xffe4 ? KEYMOD_CTRL
		: sym == 0xffe9 || sym == 0xffea ? KEYMOD_ALT : 0;
	if (bit)
		held_mods = down ? held_mods | bit : held_mods & ~bit;
	/* A client may send a shifted character without Shift held (a VNC keysym is the character): it gets the Shift
	 * modifier and the unshifted key cap, as a real keyboard's event would have. */
	uint32_t base = unshifted(sym);
	int mods = held_mods | (base != sym ? KEYMOD_SHIFT : 0), cap = map_key(base);
	screen_event_t ev;
	int type = SCREEN_EVENT_KEYBOARD, flags = KEY_SYM_VALID | KEY_CAP_VALID | (down ? KEY_DOWN : 0), scan = map_scan(sym);
	if (scan)
		flags |= KEY_SCAN_VALID;
	if (screen_create_event(&ev) != 0)
		return;
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &flags);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_SYM, &code);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_KEY_CAP, &cap);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_MODIFIERS, &mods);
	if (kbd)
		screen_set_event_property_pv(ev, SCREEN_PROPERTY_DEVICE, (void **)&kbd);
	if (scan)
		screen_set_event_property_iv(ev, SCREEN_PROPERTY_SCAN, &scan);
	int rc = screen_inject_event(disp, ev);
	screen_flush_context(sctx, 0); /* deliver now; whether Screen would hold it otherwise is not tested */
	if (verbose)
		fprintf(stderr, "%s key 0x%x -> 0x%x %s: %s\n", stamp(), (unsigned)sym, (unsigned)code, down ? "down" : "up",
			rc ? strerror(errno) : "ok");
	screen_destroy_event(ev);
}

static void inject_pointer(int x, int y, int mask)
{
	if (x >= 0) {
		ptr_x = x;
		ptr_y = y;
	}
	int buttons = ((mask & 1) ? SCREEN_LEFT_MOUSE_BUTTON : 0) | ((mask & 2) ? SCREEN_MIDDLE_MOUSE_BUTTON : 0) |
		      ((mask & 4) ? SCREEN_RIGHT_MOUSE_BUTTON : 0);
	int wheel = (mask & 8) ? -1 : (mask & 16) ? 1 : 0;
	screen_event_t ev;
	int type = SCREEN_EVENT_POINTER, flags = SCREEN_FLAG_POSITION_VALID, pos[2] = { ptr_x, ptr_y };
	if (screen_create_event(&ev) != 0)
		return;
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &flags); /* without it the position arrives as 0,0 (S7) */
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_POSITION, pos);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, pos);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
	if (ptr)
		screen_set_event_property_pv(ev, SCREEN_PROPERTY_DEVICE, (void **)&ptr);
	if (wheel)
		screen_set_event_property_iv(ev, SCREEN_PROPERTY_MOUSE_WHEEL, &wheel);
	int rc = screen_inject_event(disp, ev);
	screen_flush_context(sctx, 0); /* deliver now; whether Screen would hold it otherwise is not tested */
	if (verbose)
		fprintf(stderr, "%s pointer %d,%d buttons 0x%x: %s\n", stamp(), ptr_x, ptr_y, (unsigned)buttons,
			rc ? strerror(errno) : "ok");
	screen_destroy_event(ev);
}

/* --- sockets -------------------------------------------------------------------------------------------------- */

static int on_client_io(select_context_t *ctp, int fd, unsigned flags, void *handle);
static void take_actions(void);
static int more[ON_SLOTS]; /* read_slot() stopped at its limit with input possibly left: read again next pass */

/* Wall-clock time for the -v log, so its lines can be set beside a client's log or a packet capture */
static const char *stamp(void)
{
	static char s[32];
	struct timespec ts;
	struct tm tm;
	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &tm);
	snprintf(s, sizeof(s), "%02d:%02d:%02d.%03d", tm.tm_hour % 24, tm.tm_min % 60, tm.tm_sec % 61,
		 (int)(ts.tv_nsec / 1000000 % 1000));
	return s;
}

static void watch(int slot)
{
	select_detach(dpp, fds[slot]);
	unsigned f = SELECT_FLAG_READ | SELECT_FLAG_REARM | (want_write[slot] ? SELECT_FLAG_WRITE : 0);
	if (select_attach(dpp, NULL, fds[slot], f, on_client_io, (void *)(intptr_t)slot) != 0)
		die("select_attach");
}

static void close_fd(int slot)
{
	if (fds[slot] < 0)
		return;
	select_detach(dpp, fds[slot]);
	close(fds[slot]);
	fds[slot] = -1;
	want_write[slot] = 0;
	more[slot] = 0;
}

/* Reads a client's socket until it would block, so that an end of stream or a reset that follows the last data is
 * handled in the same pass (written during the RB01 investigation of 2026-11-23). Each read's actions are taken
 * before the next read. At most 8 reads (16 KB) per call: if the limit is reached, the slot is marked and read
 * again on the next pass, which run_pass() schedules at once, so nothing depends on another notification. */
static void read_slot(int slot)
{
	/* 2048 bytes: at most 342 actions per read (341 PointerEvents of 6 bytes, plus one message carried over from the
	 * previous read). The core's queue (512) is empty when read_slot() starts and is emptied after each read, so it
	 * cannot overflow here; the releases of a closing client are queued later, by reap(), after it was emptied. */
	uint8_t buf[2048];
	more[slot] = 0;
	for (int reads = 0; fds[slot] >= 0; reads++) {
		if (reads == 8) {
			more[slot] = 1;
			return;
		}
		ssize_t n = recv(fds[slot], buf, sizeof(buf), 0);
		if (n > 0) {
			on_input(core, slot, buf, (size_t)n, now_ns());
			take_actions(); /* only key and pointer injections can be waiting here */
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if (verbose)
			fprintf(stderr, "%s slot %d: connection ended (%s)\n", stamp(), slot,
				n == 0 ? "end of stream" : strerror(errno));
		on_closed(core, slot, now_ns());
		return;
	}
}

static int on_client_io(select_context_t *ctp, int fd, unsigned flags, void *handle)
{
	(void)ctp;
	(void)fd;
	(void)flags; /* read on any event; a read that finds nothing is harmless */
	int slot = (int)(intptr_t)handle;
	if (fds[slot] >= 0)
		read_slot(slot);
	return 0;
}

static int on_listen(select_context_t *ctp, int fd, unsigned flags, void *handle)
{
	(void)ctp;
	(void)flags;
	(void)handle;
	int c = accept(fd, NULL, NULL);
	if (c < 0)
		return 0;
	uint8_t challenge[16];
	get_random(challenge, sizeof(challenge));
	int slot = on_accept(core, challenge, now_ns());
	if (slot < 0) {
		close(c);
		return 0;
	}
	int one = 1, idle = 10, intvl = 5, cnt = 3;
	fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
	setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(c, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)); /* a peer that vanishes is noticed (UBUNTU24 9) */
	setsockopt(c, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	setsockopt(c, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
	setsockopt(c, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
	fds[slot] = c;
	want_write[slot] = 0;
	more[slot] = 0;
	watch(slot);
	if (verbose)
		fprintf(stderr, "%s accepted slot %d\n", stamp(), slot);
	return 0;
}

/* Sends what each slot has queued, until the socket would block. */
static void flush(void)
{
	on_time t = now_ns();
	for (int i = 0; i < ON_SLOTS; i++) {
		if (fds[i] < 0)
			continue;
		const uint8_t *p;
		size_t n;
		int blocked = 0;
		while ((n = on_output(core, i, &p)) > 0) {
			ssize_t w = send(fds[i], p, n, 0);
			if (w < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					blocked = 1;
				else if (errno != EINTR) {
					if (verbose)
						fprintf(stderr, "%s slot %d: connection ended (send: %s)\n", stamp(), i,
							strerror(errno));
					on_closed(core, i, t);
				}
				break;
			}
			on_sent(core, i, (size_t)w, t);
		}
		if (blocked != want_write[i]) {
			want_write[i] = blocked;
			watch(i);
		}
	}
}

/* --- the loop ------------------------------------------------------------------------------------------------- */

static int on_pulse(message_context_t *ctp, int code, unsigned flags, void *handle)
{
	(void)ctp;
	(void)flags;
	(void)handle;
	if (code == PULSE_SCREEN) {
		on_signalled(core, now_ns());
		device_signal(now_ns());
	} else if (code == PULSE_TERM) {
		quit = 1;
	} else if (code == PULSE_HUP) {
		reload = 1;
	}
	return 0;
}

static void on_term(int sig)
{
	/* safe in a signal handler; the loop wakes and sees it */
	MsgSendPulse(coid, -1, sig == SIGHUP ? PULSE_HUP : PULSE_TERM, 0);
}

static void take_actions(void)
{
	struct on_action a;
	while (on_next_action(core, &a)) {
		switch (a.type) {
		case ON_ACT_CAPTURE:
			capture();
			break;
		case ON_ACT_CLOSE:
			if (verbose)
				fprintf(stderr, "%s closed slot %d\n", stamp(), a.slot);
			close_fd(a.slot);
			break;
		case ON_ACT_SHUTDOWN_SEND:
			if (fds[a.slot] >= 0)
				shutdown(fds[a.slot], SHUT_WR);
			break;
		case ON_ACT_KEY:
			inject_key(a.keysym, a.down);
			break;
		case ON_ACT_POINTER:
			inject_pointer(a.x, a.y, a.buttons);
			break;
		default:
			break;
		}
	}
}

/* Reap, decide, act; again if a capture was taken; then send, and arm the timer for the core's next deadline. */
static void run_pass(void)
{
	int pending = 0;
	for (int i = 0; i < ON_SLOTS; i++) /* the rest of what read_slot() left at its limit */
		if (more[i] && fds[i] >= 0) {
			read_slot(i);
			pending |= more[i];
		}
	for (int round = 0; round < 3; round++) {
		uint64_t before;
		struct on_stats st;
		on_stats(core, &st);
		before = st.captures;
		on_time wake = on_schedule(core, now_ns());
		take_actions();
		flush();
		take_actions();
		on_time dw = device_pass(now_ns());
		if (dw < wake)
			wake = dw;
		on_stats(core, &st);
		if (pending && wake > now_ns() + 1000000)
			wake = now_ns() + 1000000; /* a slot still has input waiting: read it again in 1 ms */
		if (st.captures == before) {
			struct itimerspec it = { 0 };
			if (wake != ON_NEVER) {
				it.it_value.tv_sec = wake / 1000000000LL;
				it.it_value.tv_nsec = wake % 1000000000LL;
			}
			timer_settime(timer, TIMER_ABSTIME, &it, NULL);
			return;
		}
	}
	struct itimerspec it = { 0 };
	it.it_value.tv_nsec = 1000000; /* more work is waiting: come back at once */
	timer_settime(timer, 0, &it, NULL);
}

static void display_state(int *idle, int *power)
{
	screen_get_display_property_iv(disp, SCREEN_PROPERTY_IDLE_STATE, idle);
	screen_get_display_property_iv(disp, SCREEN_PROPERTY_POWER_MODE, power);
}

static int read_password(const char *path, uint8_t pw[8])
{
	char line[64] = "";
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	int ok = fgets(line, sizeof(line), f) != NULL;
	fclose(f);
	line[strcspn(line, "\r\n")] = 0;
	ok = ok && line[0];
	on_vnc_password(line, pw);
	memset(line, 0, sizeof(line));
	return ok;
}

int serve_main(int argc, char **argv)
{
	int display_no = 1, port = 5901, holdoff_ms = 33, foreground = 0;
	long login_s = 60;
	char *end;
	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], "-display=", 9) == 0)
			display_no = atoi(argv[i] + 9);
		else if (strncmp(argv[i], "-port=", 6) == 0)
			port = atoi(argv[i] + 6);
		else if (strncmp(argv[i], "-passfile=", 10) == 0)
			passfile = argv[i] + 10;
		else if (strncmp(argv[i], "-holdoff=", 9) == 0)
			holdoff_ms = atoi(argv[i] + 9);
		else if (strncmp(argv[i], "-logintimeout=", 14) == 0 && argv[i][14] >= '0' && argv[i][14] <= '9' &&
			 (login_s = strtol(argv[i] + 14, &end, 10), *end == '\0') && login_s <= 999999)
			continue; /* seconds to type the password, at most 999999; 0: no limit */
		else if (strcmp(argv[i], "-v") == 0)
			verbose = 1;
		else if (strcmp(argv[i], "-foreground") == 0)
			foreground = 1;
		else {
			fprintf(stderr, "usage: opticnerve serve [-display=N] [-port=P] [-passfile=PATH] [-holdoff=MS] "
					"[-logintimeout=S] [-v] [-foreground]\n");
			return 2;
		}
	}
	if (access(ON_DEV_PATH, F_OK) == 0) {
		fprintf(stderr, "opticnerve: %s already exists: an agent is running\n", ON_DEV_PATH);
		return 1;
	}
	/* Detach from the terminal unless asked not to; standard error stays where the caller sent it. */
	if (!foreground && procmgr_daemon(0, PROCMGR_DAEMON_NOCHDIR | PROCMGR_DAEMON_NOCLOSE | PROCMGR_DAEMON_NODEVNULL) < 0)
		die("procmgr_daemon");
	signal(SIGPIPE, SIG_IGN); /* a peer that resets mid-send must not end the agent (UBUNTU24 finding 8) */

	if (!(dpp = dispatch_create()))
		die("dispatch_create");
	if (pulse_attach(dpp, 0, PULSE_TIMER, on_pulse, NULL) < 0 || pulse_attach(dpp, 0, PULSE_SCREEN, on_pulse, NULL) < 0 ||
	    pulse_attach(dpp, 0, PULSE_TERM, on_pulse, NULL) < 0 || pulse_attach(dpp, 0, PULSE_HUP, on_pulse, NULL) < 0)
		die("pulse_attach");
	if ((coid = message_connect(dpp, MSG_FLAG_SIDE_CHANNEL)) < 0)
		die("message_connect");
	struct sigevent tev;
	SIGEV_PULSE_INIT(&tev, coid, SIGEV_PULSE_PRIO_INHERIT, PULSE_TIMER, 0);
	if (timer_create(CLOCK_MONOTONIC, &tev, &timer) != 0)
		die("timer_create");
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGHUP, on_term);

	screen_setup(display_no);
	char host[64] = "qnx", name[128];
	gethostname(host, sizeof(host));
	snprintf(name, sizeof(name), "OpticNerve %s %s:%d", ON_VERSION, host, display_no);
	struct on_config cfg = { dsize[0], dsize[1], name, 10000000000LL, 30000000000LL,
				 (on_time)holdoff_ms * 1000000, 2000000000LL, (on_time)login_s * 1000000000LL };
	if (!(core = on_core_new(&cfg)))
		die("on_core_new");
	uint8_t pw[8];
	int have = read_password(passfile, pw);
	on_set_key(core, pw, have);
	memset(pw, 0, sizeof(pw));
	if (!have) {
		fprintf(stderr, "opticnerve: no password in %s: the VNC side does not start\n", passfile);
		return 1;
	}

	for (int i = 0; i < ON_SLOTS; i++)
		fds[i] = -1;
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = INADDR_ANY };
	if (listen_fd < 0 || bind(listen_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(listen_fd, 8) != 0)
		die("listen");
	fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | O_NONBLOCK);
	if (select_attach(dpp, NULL, listen_fd, SELECT_FLAG_READ | SELECT_FLAG_REARM, on_listen, NULL) != 0)
		die("select_attach listen");

	struct device_info di = { ON_VERSION, dsize[0], dsize[1], port, now_ns, display_state };
	device_init(dpp, core, &di);

	dispatch_context_t *ctp = dispatch_context_alloc(dpp);
	if (!ctp)
		die("dispatch_context_alloc");
	char limit[24] = "none";
	if (login_s > 0)
		snprintf(limit, sizeof(limit), "%ld s", login_s);
	fprintf(stderr, "opticnerve %s: display %d (%dx%d), port %d, login limit %s, pid %d\n", ON_VERSION, display_no,
		dsize[0], dsize[1], port, limit, (int)getpid());
	while (!quit) {
		dispatch_context_t *r = dispatch_block(ctp);
		if (r) {
			ctp = r;
			dispatch_handler(ctp);
		} else if (errno != EINTR) {
			die("dispatch_block");
		}
		if (reload) {
			/* SIGHUP from `opticnerve passwd`: read the file again; a bad file changes nothing (design 4.7) */
			reload = 0;
			uint8_t k[8];
			if (read_password(passfile, k))
				on_set_key(core, k, 1);
			else
				fprintf(stderr, "opticnerve: %s has no password: the key in use stays\n", passfile);
			memset(k, 0, sizeof(k));
		}
		run_pass();
	}

	struct on_stats st;
	on_stats(core, &st);
	fprintf(stderr, "opticnerve: stopping: signals %llu, captures %llu (timed %llu), failed logins %llu, "
		"timed-out logins %llu\n",
		(unsigned long long)st.signals, (unsigned long long)st.captures, (unsigned long long)st.timed_captures,
		(unsigned long long)st.logins_failed, (unsigned long long)st.logins_timed_out);
	for (int i = 0; i < ON_SLOTS; i++)
		if (fds[i] >= 0)
			on_closed(core, i, now_ns());
	on_schedule(core, now_ns());
	take_actions(); /* releases held keys and buttons, closes sockets */
	close(listen_fd);
	screen_destroy_pixmap(pixmap);
	free(staging);
	if (kbd)
		screen_destroy_device(kbd);
	if (ptr)
		screen_destroy_device(ptr);
	screen_destroy_context(sctx);
	on_core_free(core);
	return 0;
}
