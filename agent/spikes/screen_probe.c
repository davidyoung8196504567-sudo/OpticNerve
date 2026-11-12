/*
 * screen_probe: phase 2 spikes S2 to S5 (see docs/DESIGN.md).
 *
 * Throwaway test program, one subcommand per question. Plain-text output so the results can be pasted into
 * docs/notes. Not part of the agent.
 *
 *   screen_probe status                    display list, idle state and timeout, keep-awakes, power mode, metrics
 *   screen_probe watch SECS [rearm]        S5: count SCREEN_NOTIFY_UPDATE pulses per second, with idle state
 *   screen_probe capture OUT.ppm [X Y W H] S2: screen_read_display() to a PPM file (needs root)
 *   screen_probe wm SECS [dm]              S3: window manager (or, with dm, display manager) context; print
 *                                          window events (needs root)
 *   screen_probe keepawake SECS            S4: invisible keep-awake window; print idle state every 5 s
 *
 * Every subcommand works on the first display (index 0).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/neutrino.h>
#include <screen/screen.h>

#define PULSE_CODE_UPDATE (_PULSE_CODE_MINAVAIL + 1)

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void fail(const char *what)
{
	fprintf(stderr, "FAIL %s: %s (errno %d)\n", what, strerror(errno), errno);
	exit(1);
}

static screen_context_t open_ctx(int type, const char *name)
{
	screen_context_t ctx;
	if (screen_create_context(&ctx, type) != 0) {
		fprintf(stderr, "FAIL screen_create_context(%s): %s (errno %d)\n", name, strerror(errno), errno);
		exit(1);
	}
	printf("context %s: ok\n", name);
	return ctx;
}

static screen_display_t first_display(screen_context_t ctx, int *count_out)
{
	int count = 0;
	if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count) != 0)
		fail("get DISPLAY_COUNT");
	if (count < 1) {
		fprintf(stderr, "FAIL no displays\n");
		exit(1);
	}
	screen_display_t *displays = calloc(count, sizeof(*displays));
	if (screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays) != 0)
		fail("get DISPLAYS");
	screen_display_t d = displays[0];
	free(displays);
	if (count_out)
		*count_out = count;
	return d;
}

static void print_idle(screen_display_t d)
{
	int idle = -1, awakes = -1, power = -1;
	screen_get_display_property_iv(d, SCREEN_PROPERTY_IDLE_STATE, &idle);
	screen_get_display_property_iv(d, SCREEN_PROPERTY_KEEP_AWAKES, &awakes);
	screen_get_display_property_iv(d, SCREEN_PROPERTY_POWER_MODE, &power);
	printf("idle_state %d  keep_awakes %d  power_mode %d\n", idle, awakes, power);
}

static void print_metrics(screen_display_t d)
{
	static const char *names[] = { "attach", "power_on", "idle", "event", "update", "update_pixels",
				       "update_reads", "update_writes" };
	int n = 0;
	if (screen_get_display_property_iv(d, SCREEN_PROPERTY_METRIC_COUNT, &n) != 0 || n <= 0) {
		printf("metrics: unavailable (%s)\n", strerror(errno));
		return;
	}
	long long *m = calloc(n, sizeof(*m));
	if (screen_get_display_property_llv(d, SCREEN_PROPERTY_METRICS, m) != 0) {
		printf("metrics: read failed (%s)\n", strerror(errno));
		free(m);
		return;
	}
	printf("metrics:");
	for (int i = 0; i < n; i++)
		printf(" %s=%lld", i < 8 ? names[i] : "?", m[i]);
	printf("\n");
	free(m);
}

static int cmd_status(void)
{
	screen_context_t ctx = open_ctx(SCREEN_APPLICATION_CONTEXT, "application");
	int count = 0;
	screen_display_t d = first_display(ctx, &count);
	int size[2] = { 0, 0 }, attached = -1;
	long long timeout = -1;
	char id[64] = "";
	screen_get_display_property_iv(d, SCREEN_PROPERTY_SIZE, size);
	screen_get_display_property_iv(d, SCREEN_PROPERTY_ATTACHED, &attached);
	screen_get_display_property_cv(d, SCREEN_PROPERTY_ID_STRING, sizeof(id), id);
	if (screen_get_display_property_llv(d, SCREEN_PROPERTY_IDLE_TIMEOUT, &timeout) != 0)
		printf("idle_timeout: read failed (%s)\n", strerror(errno));
	printf("displays %d; display 0 id '%s' size %dx%d attached %d\n", count, id, size[0], size[1], attached);
	printf("idle_timeout_ns %lld (%.1f s)\n", timeout, timeout / 1e9);
	print_idle(d);
	print_metrics(d);
	printf("metrics read again at once (do they reset on read?):\n");
	print_metrics(d);
	screen_destroy_context(ctx);
	return 0;
}

static int cmd_watch(int secs, int rearm)
{
	screen_context_t ctx = open_ctx(SCREEN_APPLICATION_CONTEXT, "application");
	screen_display_t d = first_display(ctx, NULL);
	int chid = ChannelCreate(_NTO_CHF_PRIVATE);
	int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
	if (chid < 0 || coid < 0)
		fail("channel");
	struct sigevent ev;
	SIGEV_PULSE_INIT(&ev, coid, SIGEV_PULSE_PRIO_INHERIT, PULSE_CODE_UPDATE, 0);
	if (screen_register_event(ctx, &ev) != 0)
		fail("screen_register_event");
	if (screen_notify(ctx, SCREEN_NOTIFY_UPDATE, d, &ev) != 0)
		fail("screen_notify(UPDATE, display)");
	printf("watching %d s, rearm %s\n", secs, rearm ? "after each pulse" : "off");
	print_metrics(d);

	double start = now_s(), second = start, last = 0;
	long total = 0, in_second = 0;
	while (now_s() - start < secs) {
		struct _pulse p;
		uint64_t to = 200000000ULL; /* 200 ms, so the per-second line prints even with no updates */
		TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &to, NULL);
		int r = MsgReceivePulse(chid, &p, sizeof(p), NULL);
		double t = now_s();
		if (r == 0 && p.code == PULSE_CODE_UPDATE) {
			total++;
			in_second++;
			last = t;
			if (rearm && screen_notify(ctx, SCREEN_NOTIFY_UPDATE, d, &ev) != 0)
				fail("screen_notify rearm");
		}
		if (t - second >= 1.0) {
			int idle = -1;
			screen_get_display_property_iv(d, SCREEN_PROPERTY_IDLE_STATE, &idle);
			printf("t=%5.1f  updates %3ld  idle %d  last update %.2f s ago\n", t - start, in_second, idle,
			       last ? t - last : -1.0);
			fflush(stdout);
			in_second = 0;
			second = t;
		}
	}
	printf("total updates %ld in %d s\n", total, secs);
	print_metrics(d);
	screen_destroy_context(ctx);
	return 0;
}

static int cmd_capture(const char *out, int argc, char **argv)
{
	screen_context_t ctx = open_ctx(SCREEN_DISPLAY_MANAGER_CONTEXT, "display manager");
	screen_display_t d = first_display(ctx, NULL);
	int size[2];
	if (screen_get_display_property_iv(d, SCREEN_PROPERTY_SIZE, size) != 0)
		fail("get display SIZE");
	int rect[4] = { 0, 0, size[0], size[1] };
	int count = 0;
	if (argc == 4) {
		for (int i = 0; i < 4; i++)
			rect[i] = atoi(argv[i]);
		count = 1;
	}

	screen_pixmap_t pix;
	int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ;
	int format = SCREEN_FORMAT_RGBA8888;
	int bsize[2] = { rect[2], rect[3] };
	if (screen_create_pixmap(&pix, ctx) != 0)
		fail("screen_create_pixmap");
	screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_BUFFER_SIZE, bsize);
	if (screen_create_pixmap_buffer(pix) != 0)
		fail("screen_create_pixmap_buffer");
	screen_buffer_t buf;
	if (screen_get_pixmap_property_pv(pix, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf) != 0)
		fail("get RENDER_BUFFERS");

	double t0 = now_s();
	if (screen_read_display(d, buf, count, count ? rect : NULL, 0) != 0)
		fail(errno == ENOTSUP ? "screen_read_display (display is mirrored)" : "screen_read_display");
	double t1 = now_s();

	unsigned char *p = NULL;
	int stride = 0;
	screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&p);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
	FILE *f = fopen(out, "wb");
	if (!f)
		fail(out);
	fprintf(f, "P6\n%d %d\n255\n", bsize[0], bsize[1]);
	for (int y = 0; y < bsize[1]; y++) {
		const unsigned char *row = p + (size_t)y * stride;
		for (int x = 0; x < bsize[0]; x++) {
			/* RGBA8888 is 0xAARRGGBB in a little-endian word: bytes B, G, R, A */
			unsigned char rgb[3] = { row[4 * x + 2], row[4 * x + 1], row[4 * x] };
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
	printf("captured %d,%d %dx%d of %dx%d in %.3f s to %s\n", rect[0], rect[1], rect[2], rect[3], size[0],
	       size[1], t1 - t0, out);
	screen_destroy_pixmap(pix);
	screen_destroy_context(ctx);
	return 0;
}

static const char *event_name(int type)
{
	switch (type) {
	case SCREEN_EVENT_CREATE: return "CREATE";
	case SCREEN_EVENT_PROPERTY: return "PROPERTY";
	case SCREEN_EVENT_CLOSE: return "CLOSE";
	case SCREEN_EVENT_POST: return "POST";
	case SCREEN_EVENT_IDLE: return "IDLE";
	case SCREEN_EVENT_DISPLAY: return "DISPLAY";
	case SCREEN_EVENT_POINTER: return "POINTER";
	case SCREEN_EVENT_KEYBOARD: return "KEYBOARD";
	case SCREEN_EVENT_MTOUCH_TOUCH: return "MTOUCH_TOUCH";
	default: return "other";
	}
}

static int cmd_wm(int secs, int dm)
{
	screen_context_t ctx = dm ? open_ctx(SCREEN_DISPLAY_MANAGER_CONTEXT, "display manager")
				  : open_ctx(SCREEN_WINDOW_MANAGER_CONTEXT, "window manager");
	screen_display_t d = first_display(ctx, NULL);
	if (!dm) {
		if (screen_register_window_manager(ctx, d) == 0)
			printf("screen_register_window_manager: ok (we are now the window manager; exiting releases it)\n");
		else
			printf("screen_register_window_manager: failed: %s (errno %d)\n", strerror(errno), errno);
	}
	int nwin = -1;
	if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_WINDOW_COUNT, &nwin) == 0)
		printf("context window count %d\n", nwin);
	else
		printf("context window count: read failed (%s)\n", strerror(errno));

	screen_event_t ev;
	if (screen_create_event(&ev) != 0)
		fail("screen_create_event");
	printf("listening %d s; open and close an app on the Pi now\n", secs);
	fflush(stdout);
	double start = now_s();
	long n = 0;
	while (now_s() - start < secs) {
		if (screen_get_event(ctx, ev, 500000000ULL) != 0)
			fail("screen_get_event");
		int type = SCREEN_EVENT_NONE;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		if (type == SCREEN_EVENT_NONE)
			continue;
		n++;
		printf("t=%5.1f  %s (%d)", now_s() - start, event_name(type), type);
		if (type == SCREEN_EVENT_CREATE || type == SCREEN_EVENT_CLOSE || type == SCREEN_EVENT_POST ||
		    type == SCREEN_EVENT_PROPERTY) {
			int otype = -1;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_OBJECT_TYPE, &otype);
			if (otype == SCREEN_OBJECT_TYPE_WINDOW) {
				screen_window_t w = NULL;
				char id[64] = "";
				int pid = -1, pos[2] = { 0, 0 }, sz[2] = { 0, 0 }, vis = -1;
				screen_get_event_property_pv(ev, SCREEN_PROPERTY_WINDOW, (void **)&w);
				if (w) {
					screen_get_window_property_cv(w, SCREEN_PROPERTY_ID_STRING, sizeof(id), id);
					screen_get_window_property_iv(w, SCREEN_PROPERTY_OWNER_PID, &pid);
					screen_get_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
					screen_get_window_property_iv(w, SCREEN_PROPERTY_SIZE, sz);
					screen_get_window_property_iv(w, SCREEN_PROPERTY_VISIBLE, &vis);
				}
				printf("  window id '%s' pid %d pos %d,%d size %dx%d visible %d", id, pid, pos[0],
				       pos[1], sz[0], sz[1], vis);
			} else {
				printf("  object type %d", otype);
			}
		}
		printf("\n");
		fflush(stdout);
	}
	printf("events %ld in %d s\n", n, secs);
	screen_destroy_event(ev);
	screen_destroy_context(ctx);
	return 0;
}

static int cmd_keepawake(int secs)
{
	screen_context_t ctx = open_ctx(SCREEN_APPLICATION_CONTEXT, "application");
	screen_display_t d = first_display(ctx, NULL);
	print_idle(d);

	/* A small, fully transparent window, so it is invisible even if a window manager resizes it. */
	screen_window_t w;
	int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
	int format = SCREEN_FORMAT_RGBA8888;
	int transp = SCREEN_TRANSPARENCY_SOURCE_OVER;
	int size[2] = { 16, 16 }, pos[2] = { 0, 0 };
	int mode = SCREEN_IDLE_MODE_KEEP_AWAKE;
	if (screen_create_window(&w, ctx) != 0)
		fail("screen_create_window");
	screen_set_window_property_iv(w, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_TRANSPARENCY, &transp);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
	screen_set_window_property_cv(w, SCREEN_PROPERTY_ID_STRING, 10, "on-probe");
	if (screen_set_window_property_iv(w, SCREEN_PROPERTY_IDLE_MODE, &mode) != 0)
		fail("set IDLE_MODE KEEP_AWAKE");
	if (screen_create_window_buffers(w, 1) != 0)
		fail("screen_create_window_buffers");
	screen_buffer_t buf;
	screen_get_window_property_pv(w, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf);
	unsigned char *p = NULL;
	int stride = 0;
	screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&p);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
	if (p)
		memset(p, 0, (size_t)stride * size[1]);
	int dirty[4] = { 0, 0, size[0], size[1] };
	if (screen_post_window(w, buf, 1, dirty, 0) != 0)
		fail("screen_post_window");
	printf("keep-awake window posted; watching %d s\n", secs);

	double start = now_s();
	while (now_s() - start < secs) {
		sleep(5);
		printf("t=%5.1f  ", now_s() - start);
		print_idle(d);
		fflush(stdout);
	}
	screen_destroy_window(w);
	screen_destroy_context(ctx);
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 2 && strcmp(argv[1], "status") == 0)
		return cmd_status();
	if (argc >= 3 && strcmp(argv[1], "watch") == 0)
		return cmd_watch(atoi(argv[2]), argc >= 4 && strcmp(argv[3], "rearm") == 0);
	if (argc == 3 || argc == 7) {
		if (strcmp(argv[1], "capture") == 0)
			return cmd_capture(argv[2], argc - 3, argv + 3);
	}
	if (argc >= 3 && strcmp(argv[1], "wm") == 0)
		return cmd_wm(atoi(argv[2]), argc >= 4 && strcmp(argv[3], "dm") == 0);
	if (argc >= 3 && strcmp(argv[1], "keepawake") == 0)
		return cmd_keepawake(atoi(argv[2]));
	fprintf(stderr, "usage: screen_probe status | watch SECS [rearm] | capture OUT.ppm [X Y W H] | wm SECS [dm] | "
			"keepawake SECS\n");
	return 2;
}
