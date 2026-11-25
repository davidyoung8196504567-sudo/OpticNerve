/*
 * core_test: host tests of agent/core (HT01 DES and VncAuth, HT04 unit tests of the core: handshake, slots,
 * client messages, tile marks and the schedule step). Runs anywhere the core builds; prints one line per check and
 * exits non-zero on any failure.
 *
 * Expected values for DES and VncAuth come from outside this code: the FIPS 46 worked example, and the Python
 * `cryptography` library as our VNC tool uses it (host/tools/vnc_snapshot.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "on_core.h"
#include "on_des.h"
#include "on_mouse.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static void hex(const char *s, uint8_t *out)
{
	for (size_t i = 0; s[2 * i]; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
}

#define MS 1000000LL
#define W 200
#define H 130 /* 4 x 3 tiles, the last row and column partial */

static struct on_core *core;
static const uint8_t pw_text[] = "opt1cNrv";
static uint8_t frame[W * H * 4];

static struct on_core *new_core(void)
{
	struct on_config cfg = { W, H, "OpticNerve test", 10000 * MS, 30000 * MS, 33 * MS, 2000 * MS, 60000 * MS, 0 };
	struct on_core *c = on_core_new(&cfg);
	uint8_t pw[8];
	on_vnc_password((const char *)pw_text, pw);
	on_set_key(c, pw, 1);
	return c;
}

/* Takes everything queued for the slot. */
static size_t drain(int slot, uint8_t *buf, size_t cap, on_time now)
{
	const uint8_t *p;
	size_t n = on_output(core, slot, &p);
	if (n > cap)
		n = cap;
	if (buf)
		memcpy(buf, p, n);
	on_sent(core, slot, n, now);
	return n;
}

static int count_actions(enum on_action_type t)
{
	struct on_action a;
	int n = 0;
	while (on_next_action(core, &a))
		n += a.type == t;
	return n;
}

static int login(const uint8_t *challenge, const char *password, on_time now)
{
	uint8_t buf[512], resp[16], pw[8];
	int s = on_accept(core, challenge, now);
	if (s < 0)
		return -1;
	CHECK(drain(s, buf, sizeof(buf), now) == 12 && memcmp(buf, "RFB 003.008\n", 12) == 0, "server version");
	on_input(core, s, (const uint8_t *)"RFB 003.008\n", 12, now);
	size_t n = drain(s, buf, sizeof(buf), now);
	CHECK(n == 2 && buf[0] == 1 && buf[1] == 2, "security types are one buffer 01 02 (got %zu bytes)", n);
	on_input(core, s, (const uint8_t[]){ 2 }, 1, now);
	CHECK(drain(s, buf, sizeof(buf), now) == 16 && memcmp(buf, challenge, 16) == 0, "challenge sent");
	on_vnc_password(password, pw);
	on_vnc_response(pw, challenge, resp);
	on_input(core, s, resp, 16, now);
	return s;
}

static void test_des(void)
{
	uint8_t k[8], p[8], want[8], got[8];
	hex("133457799BBCDFF1", k);
	hex("0123456789ABCDEF", p);
	hex("85E813540F0AB405", want);
	on_des_encrypt(k, p, got);
	CHECK(memcmp(got, want, 8) == 0, "HT01 DES worked example");

	uint8_t pw[8], ch[16], r[16], rwant[16];
	on_vnc_password("opt1cNrv", pw);
	for (int i = 0; i < 16; i++)
		ch[i] = (uint8_t)i;
	hex("6e31cec0c3644ed039af361fbc702549", rwant);
	on_vnc_response(pw, ch, r);
	CHECK(memcmp(r, rwant, 16) == 0, "HT01 VncAuth response equals the Python tool's");
}

static void test_handshake(void)
{
	core = new_core();
	uint8_t ch[16] = { 1, 2, 3 }, buf[512];
	on_time t = 1000 * MS;

	int s = login(ch, "opt1cNrv", t);
	CHECK(s >= 0, "accepted");
	size_t n = drain(s, buf, sizeof(buf), t);
	CHECK(n == 4 && buf[3] == 0, "SecurityResult OK");
	on_input(core, s, (const uint8_t[]){ 1 }, 1, t); /* ClientInit, shared */
	n = drain(s, buf, sizeof(buf), t);
	CHECK(n == 24 + 15 && buf[0] == 0 && buf[1] == W && buf[3] == H && buf[4] == 32 && buf[14] == 16 &&
	      memcmp(buf + 24, "OpticNerve test", 15) == 0, "ServerInit: size, 32 bpp, red shift 16, name");

	/* a wrong password: failure with a reason, the slot lingers, then closes */
	int s2 = login(ch, "wrong", t);
	n = drain(s2, buf, sizeof(buf), t);
	CHECK(n > 8 && buf[3] == 1 && memcmp(buf + 8, "authentication failed", 21) == 0, "wrong password: reason sent");
	struct on_action a;
	int shut = 0;
	while (on_next_action(core, &a))
		shut += a.type == ON_ACT_SHUTDOWN_SEND && a.slot == s2;
	CHECK(shut == 1, "send side shut down after the reason");
	struct on_stats st;
	on_stats(core, &st);
	CHECK(st.logins_failed == 1, "failed login counted");
	on_schedule(core, t + 2001 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 1, "lingering slot closed after 2 s");

	/* a client that names a security type that was not offered (None) is refused */
	int s3 = on_accept(core, ch, t);
	drain(s3, NULL, 512, t);
	on_input(core, s3, (const uint8_t *)"RFB 003.008\n", 12, t);
	drain(s3, NULL, 512, t);
	on_input(core, s3, (const uint8_t[]){ 1 }, 1, t);
	n = drain(s3, buf, sizeof(buf), t);
	CHECK(n > 4 && buf[3] == 1, "HS10: type None refused with SecurityResult 1");
	on_closed(core, s3, t);
	on_schedule(core, t);
	count_actions(ON_ACT_NONE);

	/* two handshake slots: a third connection that has not logged in is refused at accept */
	int h1 = on_accept(core, ch, t), h2 = on_accept(core, ch, t), h3 = on_accept(core, ch, t);
	CHECK(h1 >= 0 && h2 >= 0 && h3 < 0, "third handshake refused at accept");
	on_schedule(core, t + 10001 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 2, "handshake deadline closes both");

	/* I6: once the challenge is sent (a viewer asks its user for the password) the 10 s deadline no longer applies;
	 * after 60 s without an answer the login fails with a reason, the slot lingers, then closes */
	t = 100000 * MS;
	int p = on_accept(core, ch, t);
	drain(p, NULL, 512, t);
	on_input(core, p, (const uint8_t *)"RFB 003.008\n", 12, t + 1 * MS);
	drain(p, NULL, 512, t + 1 * MS);
	on_input(core, p, (const uint8_t[]){ 2 }, 1, t + 2 * MS);
	drain(p, NULL, 512, t + 2 * MS);
	on_time when = on_schedule(core, t + 10001 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 0, "waiting for the password: still open after 10 s");
	CHECK(when == t + 2 * MS + 60000 * MS, "next wake-up is the password deadline");
	on_schedule(core, t + 2 * MS + 60000 * MS);
	n = drain(p, buf, sizeof(buf), t + 60002 * MS);
	CHECK(n == 48 && buf[3] == 1 && buf[4] == 0 && buf[5] == 0 && buf[6] == 0 && buf[7] == 40 &&
	      memcmp(buf + 8, "login timed out: no password within 60 s", 40) == 0,
	      "after 60 s: SecurityResult 1, reason length 40 and the reason (got %zu bytes)", n);
	shut = 0;
	while (on_next_action(core, &a))
		shut += a.type == ON_ACT_SHUTDOWN_SEND && a.slot == p;
	CHECK(shut == 1, "send side shut down after the timeout reason");
	on_stats(core, &st);
	CHECK(st.logins_timed_out == 1 && st.logins_failed == 2, /* 2: the wrong password and the type None above */
	      "timeout counted on its own, not as a failed login (failed %llu)", (unsigned long long)st.logins_failed);
	on_schedule(core, t + 62003 * MS);
	int closed_p = 0, closed_other = 0;
	while (on_next_action(core, &a))
		if (a.type == ON_ACT_CLOSE) {
			closed_p += a.slot == p;
			closed_other += a.slot != p;
		}
	CHECK(closed_p == 1 && closed_other == 0, "the timed-out slot, and only it, closed after the linger");

	/* the point of the 60 s: a correct password typed late but in time (59 s) logs in */
	t = 150000 * MS;
	int late = on_accept(core, ch, t);
	drain(late, NULL, 512, t);
	on_input(core, late, (const uint8_t *)"RFB 003.008\n", 12, t);
	drain(late, NULL, 512, t);
	on_input(core, late, (const uint8_t[]){ 2 }, 1, t);
	drain(late, NULL, 512, t);
	on_schedule(core, t + 30000 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 0, "still waiting for the password at 30 s");
	uint8_t pwk[8], resp[16];
	on_vnc_password((const char *)pw_text, pwk);
	on_vnc_response(pwk, ch, resp);
	on_input(core, late, resp, 16, t + 59000 * MS);
	n = drain(late, buf, sizeof(buf), t + 59000 * MS);
	CHECK(n == 4 && buf[3] == 0, "a correct password at 59 s: SecurityResult OK (got %zu bytes)", n);
	/* logged in but no ClientInit: a program's step, so the 10 s deadline applies again from the login */
	on_schedule(core, t + 59000 * MS + 9999 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 0, "no ClientInit yet: still open just before 10 s after the login");
	on_schedule(core, t + 59000 * MS + 10001 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 1, "no ClientInit within 10 s of the login: closed");

	/* a connection that has sent its version but not chosen a security type is still closed at 10 s, silently */
	t = 200000 * MS;
	int q = on_accept(core, ch, t);
	drain(q, NULL, 512, t);
	on_input(core, q, (const uint8_t *)"RFB 003.008\n", 12, t);
	drain(q, NULL, 512, t);
	on_schedule(core, t + 10001 * MS);
	const uint8_t *qp;
	size_t qn = on_output(core, q, &qp);
	CHECK(qn == 0, "no security type chosen: nothing queued, no reason (got %zu bytes)", qn);
	closed_p = 0;
	while (on_next_action(core, &a))
		closed_p += a.type == ON_ACT_CLOSE && a.slot == q;
	CHECK(closed_p == 1, "no security type chosen: closed at 10 s");

	/* a version other than 3.8 */
	int v = on_accept(core, ch, t);
	drain(v, NULL, 512, t);
	on_input(core, v, (const uint8_t *)"RFB 003.003\n", 12, t);
	n = drain(v, buf, sizeof(buf), t);
	CHECK(n > 5 && buf[0] == 0, "other version: zero security types and a reason");
	on_core_free(core);

	/* auth_timeout 0 (serve -logintimeout=0): no limit on answering the challenge */
	struct on_config cfg0 = { W, H, "OpticNerve test", 10000 * MS, 30000 * MS, 33 * MS, 2000 * MS, 0, 0 };
	core = on_core_new(&cfg0);
	uint8_t pw0[8];
	on_vnc_password((const char *)pw_text, pw0);
	on_set_key(core, pw0, 1);
	t = 1000 * MS;
	int u = on_accept(core, ch, t);
	drain(u, NULL, 512, t);
	on_input(core, u, (const uint8_t *)"RFB 003.008\n", 12, t);
	drain(u, NULL, 512, t);
	on_input(core, u, (const uint8_t[]){ 2 }, 1, t);
	drain(u, NULL, 512, t);
	int u2 = on_accept(core, ch, t); /* a second prompt, left open, then its viewer closes */
	drain(u2, NULL, 512, t);
	on_input(core, u2, (const uint8_t *)"RFB 003.008\n", 12, t);
	drain(u2, NULL, 512, t);
	on_input(core, u2, (const uint8_t[]){ 2 }, 1, t);
	drain(u2, NULL, 512, t);
	when = on_schedule(core, t + 3600000 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 0 && when == ON_NEVER, "no login limit: nothing to close after an hour, no wake-up");
	CHECK(on_accept(core, ch, t + 3600000 * MS) < 0, "no login limit: both prompts still hold their login slots");
	on_stats(core, &st);
	CHECK(st.logins_timed_out == 0, "no login limit: nothing timed out");
	on_closed(core, u2, t + 3600000 * MS);
	on_schedule(core, t + 3600000 * MS);
	count_actions(ON_ACT_NONE);
	int u3 = on_accept(core, ch, t + 3600000 * MS);
	CHECK(u3 >= 0, "no login limit: the slot of a viewer that closed is free again");
	on_closed(core, u3, t + 3600000 * MS);
	on_schedule(core, t + 3600000 * MS);
	count_actions(ON_ACT_NONE);
	on_vnc_response(pw0, ch, resp);
	on_input(core, u, resp, 16, t + 3600000 * MS);
	n = drain(u, buf, sizeof(buf), t + 3600000 * MS);
	CHECK(n == 4 && buf[3] == 0, "no login limit: a correct password after an hour logs in (got %zu bytes)", n);
	t += 3600000 * MS;
	on_schedule(core, t + 9999 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 0, "no login limit: no ClientInit yet, still open just before 10 s");
	on_schedule(core, t + 10001 * MS);
	CHECK(count_actions(ON_ACT_CLOSE) == 1, "no login limit: the 10 s ClientInit deadline still applies");
	on_core_free(core);
}

static void fill(uint8_t base)
{
	for (int i = 0; i < W * H; i++) {
		frame[4 * i] = (uint8_t)(base + i);
		frame[4 * i + 1] = (uint8_t)(base + 2 * i);
		frame[4 * i + 2] = (uint8_t)(base + 3 * i);
		frame[4 * i + 3] = 0xff; /* padding the core must ignore */
	}
}

static void request(int s, int incremental, int x, int y, int w, int h, on_time t)
{
	uint8_t m[10] = { 3, (uint8_t)incremental, (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 8), (uint8_t)y,
			  (uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(h >> 8), (uint8_t)h };
	on_input(core, s, m, 10, t);
}

/* Runs one pass: schedule, and if a capture is asked for, deliver `frame` and schedule again. */
static int pass(on_time t)
{
	on_schedule(core, t);
	struct on_action a;
	int captured = 0;
	while (on_next_action(core, &a))
		if (a.type == ON_ACT_CAPTURE) {
			on_captured(core, frame, W * 4, t, t + 30 * MS);
			captured = 1;
		}
	if (captured)
		on_schedule(core, t + 30 * MS);
	return captured;
}

/* The arrow the core is expected to draw (ON-AG-001), defined here independently of the core's copy: X black,
 * . white, space not drawn, hotspot at the top left. */
static const char *const ARROW[19] = {
	"X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
	"X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX",
	"X...X..X    ", "X..XX..X    ", "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ",
	"       XX   ",
};
static int arrow_x = -1, arrow_y = -1; /* where check_update() expects the arrow; -1: no arrow */

/* The expected pixel at x,y: the frame's, or the arrow's where it is drawn. */
static const uint8_t *expected(int x, int y)
{
	static const uint8_t white[4] = { 255, 255, 255, 0 }, black[4] = { 0, 0, 0, 0 };
	if (arrow_x >= 0 && x >= arrow_x && x < arrow_x + 12 && y >= arrow_y && y < arrow_y + 19) {
		char k = ARROW[y - arrow_y][x - arrow_x];
		if (k != ' ')
			return k == '.' ? white : black;
	}
	return frame + ((size_t)y * W + x) * 4;
}

/* Parses an update and checks every pixel against `frame`, with the arrow where arrow_x/arrow_y say. Returns the
 * rectangle count, -1 on a bad pixel. */
static int check_update(const uint8_t *u, size_t n)
{
	if (n < 4 || u[0] != 0)
		return -2;
	int count = u[2] << 8 | u[3];
	size_t at = 4;
	for (int r = 0; r < count; r++) {
		int x = u[at] << 8 | u[at + 1], y = u[at + 2] << 8 | u[at + 3];
		int w = u[at + 4] << 8 | u[at + 5], h = u[at + 6] << 8 | u[at + 7];
		at += 12;
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++, at += 4) {
				const uint8_t *f = expected(x + i, y + j);
				if (u[at] != f[0] || u[at + 1] != f[1] || u[at + 2] != f[2] || u[at + 3] != 0)
					return -1;
			}
	}
	return at == n ? count : -3;
}

/* 1 if the update holds a rectangle that covers the pixel x,y. */
static int update_covers(const uint8_t *u, size_t n, int px, int py)
{
	if (n < 4)
		return 0;
	int count = u[2] << 8 | u[3];
	size_t at = 4;
	for (int r = 0; r < count && at + 12 <= n; r++) {
		int x = u[at] << 8 | u[at + 1], y = u[at + 2] << 8 | u[at + 3];
		int w = u[at + 4] << 8 | u[at + 5], h = u[at + 6] << 8 | u[at + 7];
		if (px >= x && px < x + w && py >= y && py < y + h)
			return 1;
		at += 12 + (size_t)w * h * 4;
	}
	return 0;
}

static uint8_t big[4 + 12 * 12 + W * H * 4 + 64];

static void test_schedule(void)
{
	core = new_core();
	uint8_t ch[16] = { 9 };
	on_time t = 5000 * MS;
	int s = login(ch, "opt1cNrv", t);
	drain(s, NULL, 64, t);
	on_input(core, s, (const uint8_t[]){ 1 }, 1, t);
	drain(s, NULL, 64, t);
	count_actions(ON_ACT_NONE);

	/* a non-incremental request is answered from a capture taken after it arrived */
	fill(10);
	request(s, 0, 0, 0, W, H, t);
	CHECK(pass(t) == 1, "non-incremental request asks for a capture");
	size_t n = drain(s, big, sizeof(big), t);
	CHECK(check_update(big, n) == 12, "full update: 12 tiles, every pixel right, padding byte 0");

	/* incremental, static screen: nothing is sent and no capture is taken until the timed capture is due */
	t += 100 * MS;
	request(s, 1, 0, 0, W, H, t);
	CHECK(pass(t) == 0, "incremental on a static screen: no capture");
	CHECK(drain(s, NULL, 64, t) == 0, "incremental on a static screen: nothing sent");

	/* a signal: capture, one changed tile, an update with one rectangle */
	frame[(70 * W + 130) * 4] ^= 0x55; /* tile (2,1) */
	t += 100 * MS;
	on_signalled(core, t);
	CHECK(pass(t) == 1, "signal with an incremental request pending: capture");
	n = drain(s, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "one changed tile sent");

	/* the comparison (masked 32-bit words since 2026-11-22): each colour byte alone, the last pixel of a full tile,
	 * the partial corner tile, a padding-only change, and a capture buffer whose stride is wider than the screen */
	struct { int x, y, byte; const char *what; } one[] = {
		{ 130, 70, 1, "G alone" }, { 130, 70, 2, "R alone" }, { 191, 127, 0, "last pixel of full tile (2,1)" },
		{ 199, 129, 2, "corner pixel of partial tile (3,2)" } };
	for (size_t k = 0; k < sizeof(one) / sizeof(one[0]); k++) {
		frame[(one[k].y * W + one[k].x) * 4 + one[k].byte] ^= 0x01;
		t += 100 * MS;
		request(s, 1, 0, 0, W, H, t);
		on_signalled(core, t);
		pass(t);
		n = drain(s, big, sizeof(big), t);
		CHECK(check_update(big, n) == 1, "one-byte change, %s: one tile sent", one[k].what);
	}
	for (int i = 0; i < W * H; i++)
		frame[4 * i + 3] ^= 0x5a; /* padding only */
	t += 100 * MS;
	request(s, 1, 0, 0, W, H, t);
	on_signalled(core, t);
	CHECK(pass(t) == 1, "padding-only change: a capture is taken");
	CHECK(drain(s, NULL, 64, t) == 0, "padding-only change: nothing sent");
	{
		static uint8_t wide[(W * 4 + 16) * H];
		frame[(30 * W + 20) * 4] ^= 0x01; /* tile (0,0) */
		for (int y = 0; y < H; y++) {
			memcpy(wide + (size_t)y * (W * 4 + 16), frame + (size_t)y * W * 4, W * 4);
			memset(wide + (size_t)y * (W * 4 + 16) + W * 4, 0xa5, 16); /* junk past the row: must be ignored */
		}
		t += 100 * MS;
		on_signalled(core, t); /* the incremental request from the padding case is still pending */
		on_schedule(core, t);
		struct on_action ca;
		int took = 0;
		while (on_next_action(core, &ca))
			if (ca.type == ON_ACT_CAPTURE) {
				on_captured(core, wide, W * 4 + 16, t, t + 30 * MS);
				took = 1;
			}
		on_schedule(core, t + 30 * MS);
		n = drain(s, big, sizeof(big), t);
		CHECK(took && check_update(big, n) == 1, "capture with a wider stride: one tile sent, junk ignored");
	}

	/* sequence A (UBUNTU24 finding 1): a change while the client's queue is full is sent when it drains */
	t += 100 * MS;
	request(s, 0, 0, 0, W, H, t);
	pass(t);                       /* full frame queued, not drained */
	request(s, 1, 0, 0, W, H, t);
	frame[(10 * W + 10) * 4] ^= 0x33; /* tile (0,0) */
	t += 100 * MS;
	on_signalled(core, t);
	pass(t);                       /* client is held back: queue not empty */
	n = drain(s, big, sizeof(big), t); /* the old full frame */
	CHECK(n > 0, "sequence A: first frame drained");
	t += 100 * MS;
	pass(t);
	n = drain(s, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "sequence A: the change made while the queue was full arrives (%zu bytes)", n);

	/* a request for part of the screen (ON-OX-013 corners): cut tiles stay marked for the next whole request */
	frame[(5 * W + 5) * 4] ^= 0x11; /* tile (0,0), outside the area below */
	t += 100 * MS;
	on_signalled(core, t);
	request(s, 1, 10, 10, 65527, 65527, t);
	pass(t);
	n = drain(s, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "partial request: tile (0,0) sent cut to the area");
	struct on_client_stats cs;
	on_client_stats(core, s, &cs);
	CHECK(cs.marked_tiles == 1, "partial request: the cut tile stays marked (%d)", cs.marked_tiles);
	t += 100 * MS;
	request(s, 1, 0, 0, W, H, t);
	pass(t);
	n = drain(s, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "whole request after it brings the rest of the tile");

	/* hold-off: a second capture waits at least as long as the last one took */
	t += 100 * MS;
	request(s, 0, 0, 0, W, H, t);
	on_schedule(core, t);
	struct on_action a;
	int cap = 0;
	while (on_next_action(core, &a))
		cap += a.type == ON_ACT_CAPTURE;
	on_captured(core, frame, W * 4, t, t + 80 * MS); /* a slow capture */
	on_schedule(core, t + 80 * MS);
	drain(s, NULL, sizeof(big), t + 80 * MS);
	request(s, 0, 0, 0, W, H, t + 90 * MS);
	on_time wake = on_schedule(core, t + 90 * MS);
	int cap2 = count_actions(ON_ACT_CAPTURE);
	CHECK(cap == 1 && cap2 == 0 && wake == t + 160 * MS, "hold-off: next capture not before end + duration");

	/* timed capture: an incremental request waiting 2 s with no signal gets one capture */
	pass(wake);
	drain(s, NULL, sizeof(big), wake);
	t = wake + 100 * MS;
	request(s, 1, 0, 0, W, H, t);
	frame[(100 * W + 190) * 4] ^= 0x77; /* tile (2,1): changed without a signal */
	CHECK(pass(t + 1000 * MS) == 0, "no timed capture before 2 s");
	CHECK(pass(t + 2100 * MS) == 1, "timed capture after 2 s");
	n = drain(s, big, sizeof(big), t + 2200 * MS);
	struct on_stats st;
	on_stats(core, &st);
	CHECK(check_update(big, n) == 1 && st.timed_captures == 1 && st.updates_from_timed_capture == 1,
	      "timed capture finds the unsignalled change and counts it");

	/* input: the first client to send input owns it; a disconnect releases what it holds */
	on_input(core, s, (const uint8_t[]){ 4, 1, 0, 0, 0, 0, 0xff, 0xe1 }, 8, t);  /* Shift down */
	on_input(core, s, (const uint8_t[]){ 5, 1, 0, 50, 0, 60 }, 6, t);           /* button 1 at 50,60 */
	int keys = 0, ptrs = 0;
	while (on_next_action(core, &a)) {
		keys += a.type == ON_ACT_KEY && a.keysym == 0xffe1 && a.down;
		ptrs += a.type == ON_ACT_POINTER && a.x == 50 && a.y == 60 && a.buttons == 1;
	}
	CHECK(keys == 1 && ptrs == 1, "key and pointer events become actions");
	on_closed(core, s, t);
	on_schedule(core, t);
	int up = 0, release = 0, closed = 0;
	while (on_next_action(core, &a)) {
		up += a.type == ON_ACT_KEY && a.keysym == 0xffe1 && !a.down;
		release += a.type == ON_ACT_POINTER && a.buttons == 0;
		closed += a.type == ON_ACT_CLOSE;
	}
	CHECK(up == 1 && release == 1 && closed == 1, "IN09/IN11: disconnect releases the held key and button");

	/* a reused slot starts clean */
	int s2 = login(ch, "opt1cNrv", t);
	on_client_stats(core, s2, &cs);
	CHECK(s2 == s && cs.requests == 0 && cs.marked_tiles == 0, "reused slot starts clean");

	/* message limits: an extended clipboard message is skipped and counted; a long plain text closes */
	drain(s2, NULL, 64, t);
	on_input(core, s2, (const uint8_t[]){ 1 }, 1, t);
	drain(s2, NULL, 64, t);
	const uint8_t ext[16] = { 6, 0, 0, 0, 0xff, 0xff, 0xff, 0xf8, 0x1f, 0, 0, 1, 0, 0, 0, 0 };
	on_input(core, s2, ext, 16, t);
	request(s2, 0, 0, 0, 8, 8, t);
	on_stats(core, &st);
	on_client_stats(core, s2, &cs);
	CHECK(st.extended_cut_text == 1 && cs.requests == 1 && cs.state == 2, "extended clipboard skipped, client kept");
	on_input(core, s2, (const uint8_t[]){ 6, 0, 0, 0, 0x00, 0x20, 0, 0 }, 8, t); /* 2 MiB of text */
	on_client_stats(core, s2, &cs);
	CHECK(cs.state == 3, "text over 1 MiB closes the client");
	on_input(core, s2, (const uint8_t[]){ 9 }, 1, t);
	on_core_free(core);
}

/* Runs every queued action; counts by type and slot. */
struct tally { int ptr[ON_SLOTS], key[ON_SLOTS], close, ptr_x90; };
static struct tally take(void)
{
	struct tally n = { { 0 }, { 0 }, 0, 0 };
	struct on_action a;
	while (on_next_action(core, &a)) {
		if (a.type == ON_ACT_POINTER) {
			n.ptr[a.slot]++;
			n.ptr_x90 += a.x == 90;
		}
		n.key[a.slot] += a.type == ON_ACT_KEY;
		n.close += a.type == ON_ACT_CLOSE;
	}
	return n;
}

/* 4.8 handover: a tester's viewer (v) and a test script (p) take turns; a press is never split from its release,
 * and no client releases what another holds */
static void test_handover(void)
{
	core = new_core();
	uint8_t ch[16] = { 7 };
	on_time t = 1000 * MS;
	struct on_stats st;
	struct on_client_stats cs;
	int v = login(ch, "opt1cNrv", t), p = login(ch, "opt1cNrv", t);
	drain(v, NULL, 64, t);
	drain(p, NULL, 64, t);
	on_input(core, v, (const uint8_t[]){ 1 }, 1, t);
	on_input(core, p, (const uint8_t[]){ 1 }, 1, t);
	drain(v, NULL, 512, t);
	drain(p, NULL, 512, t);
	take();

	on_input(core, v, (const uint8_t[]){ 5, 1, 0, 10, 0, 10 }, 6, t);           /* v: button 1 down */
	on_input(core, p, (const uint8_t[]){ 5, 0, 0, 90, 0, 90 }, 6, t);           /* p: a move, during v's press */
	on_input(core, p, (const uint8_t[]){ 4, 1, 0, 0, 0, 0, 0, 0x61 }, 8, t);   /* p: 'a', during v's press */
	struct tally n = take();
	on_stats(core, &st);
	on_client_stats(core, v, &cs);
	CHECK(n.ptr[v] == 1 && n.ptr[p] == 0 && n.key[p] == 0 && st.pointer_dropped == 1 && st.keys_dropped == 1 &&
	      st.input_handovers == 0 && cs.input_owner,
	      "while v holds a button, p's pointer and key are dropped and counted; v still owns input");

	on_input(core, v, (const uint8_t[]){ 5, 0, 0, 10, 0, 10 }, 6, t);           /* v: button up */
	on_input(core, p, (const uint8_t[]){ 5, 1, 0, 90, 0, 90 }, 6, t);           /* p: click at 90,90 */
	on_input(core, p, (const uint8_t[]){ 5, 0, 0, 90, 0, 90 }, 6, t);
	n = take();
	on_stats(core, &st);
	on_client_stats(core, p, &cs);
	CHECK(n.ptr[v] == 1 && n.ptr[p] == 2 && n.ptr_x90 == 2 && st.input_handovers == 1 && cs.input_owner,
	      "once v let go, p's click is injected and p owns input (handovers %llu)", (unsigned long long)st.input_handovers);

	on_input(core, v, (const uint8_t[]){ 5, 0, 0, 20, 0, 20 }, 6, t);           /* v: the tester moves the mouse */
	n = take();
	on_stats(core, &st);
	CHECK(n.ptr[v] == 1 && st.input_handovers == 2, "and the tester's viewer can take it back at once");

	/* a release of a key never pressed (a viewer losing focus) neither takes input nor counts */
	on_input(core, p, (const uint8_t[]){ 4, 0, 0, 0, 0, 0, 0xff, 0xe3 }, 8, t); /* p: Control up */
	n = take();
	on_stats(core, &st);
	on_client_stats(core, v, &cs);
	CHECK(n.key[p] == 0 && st.input_handovers == 2 && st.keys_dropped == 1 && cs.input_owner,
	      "an unheld key's release does nothing: v still owns input, nothing counted");

	/* a held key blocks the handover too, and the other client cannot release it */
	on_input(core, v, (const uint8_t[]){ 4, 1, 0, 0, 0, 0, 0xff, 0xe1 }, 8, t); /* v: Shift down */
	on_input(core, p, (const uint8_t[]){ 5, 1, 0, 90, 0, 90 }, 6, t);           /* p: press during v's Shift */
	on_input(core, p, (const uint8_t[]){ 4, 0, 0, 0, 0, 0, 0xff, 0xe1 }, 8, t); /* p: Shift up */
	n = take();
	on_stats(core, &st);
	on_client_stats(core, v, &cs);
	CHECK(n.key[v] == 1 && n.ptr[p] == 0 && n.key[p] == 0 && st.pointer_dropped == 2 && cs.input_owner,
	      "while v holds Shift, p's press is dropped and p cannot release v's Shift");

	/* p's button is still down (its press was dropped): when v lets go, p's drag does not take over mid-way */
	on_input(core, v, (const uint8_t[]){ 4, 0, 0, 0, 0, 0, 0xff, 0xe1 }, 8, t); /* v: Shift up */
	on_input(core, p, (const uint8_t[]){ 5, 1, 0, 95, 0, 95 }, 6, t);           /* p: drag motion, button still down */
	n = take();
	on_stats(core, &st);
	CHECK(n.key[v] == 1 && n.ptr[p] == 0 && st.pointer_dropped == 3 && st.input_handovers == 2,
	      "a drag that began without input does not become a press at a new place");
	on_input(core, p, (const uint8_t[]){ 5, 0, 0, 95, 0, 95 }, 6, t);           /* p: lets go: a plain move, taken */
	on_input(core, p, (const uint8_t[]){ 5, 1, 0, 90, 0, 90 }, 6, t);           /* p: a new press is */
	n = take();
	on_stats(core, &st);
	CHECK(n.ptr[p] == 2 && st.input_handovers == 3, "after letting go, p's next events take input");

	/* the owner's held button is released when it disconnects; the other client's events cause nothing */
	on_closed(core, p, t);
	on_closed(core, v, t);
	on_schedule(core, t);
	struct on_action a;
	int rel_p = 0, rel_v = 0, closed = 0;
	while (on_next_action(core, &a)) {
		rel_p += a.type == ON_ACT_POINTER && a.slot == p && a.buttons == 0;
		rel_v += (a.type == ON_ACT_POINTER || a.type == ON_ACT_KEY) && a.slot == v;
		closed += a.type == ON_ACT_CLOSE;
	}
	CHECK(rel_p == 1 && rel_v == 0 && closed == 2, "only the owner's held button is released; both closed");
	on_core_free(core);
}

/* --- the pointer arrow (ON-AG-001) ------------------------------------------------------------------------------ */

/* A logged-in client after ClientInit, its ServerInit drained. */
static int client(uint8_t seed, on_time t)
{
	uint8_t ch[16] = { seed };
	int s = login(ch, "opt1cNrv", t);
	drain(s, NULL, 64, t);
	on_input(core, s, (const uint8_t[]){ 1 }, 1, t);
	drain(s, NULL, 512, t);
	return s;
}

/* Sends SetEncodings with the n encodings in e, whole or one byte at a time; extra bytes follow in the same input. */
static void encodings(int s, const int32_t *e, int n, int bytewise, const uint8_t *extra, size_t nextra, on_time t)
{
	static uint8_t m[4 + 4 * 65535 + 16];
	size_t k = 0;
	m[k++] = 2;
	m[k++] = 0;
	m[k++] = (uint8_t)(n >> 8);
	m[k++] = (uint8_t)n;
	for (int i = 0; i < n; i++) {
		uint32_t v = (uint32_t)e[i];
		m[k++] = (uint8_t)(v >> 24);
		m[k++] = (uint8_t)(v >> 16);
		m[k++] = (uint8_t)(v >> 8);
		m[k++] = (uint8_t)v;
	}
	if (nextra)
		memcpy(m + k, extra, nextra);
	k += nextra;
	if (bytewise)
		for (size_t i = 0; i < k; i++)
			on_input(core, s, m + i, 1, t);
	else
		on_input(core, s, m, k, t);
}

static int bits_of(int s)
{
	struct on_client_stats cs;
	on_client_stats(core, s, &cs);
	return cs.pointer_encodings;
}

static void pointer_to(int s, int x, int y, on_time t)
{
	on_input(core, s, (const uint8_t[]){ 5, 0, (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 8), (uint8_t)y }, 6, t);
	count_actions(ON_ACT_NONE);
}

static void test_pointer_parser(void)
{
	core = new_core();
	on_time t = 1000 * MS;
	int s = client(21, t);
	const uint8_t req[10] = { 3, 1, 0, 0, 0, 0, 0, 8, 0, 8 }; /* a FramebufferUpdateRequest after each list */
	struct on_client_stats cs;
	uint64_t before;

	/* OculiX's own list, as tapped in 317 connections (OculiX 4.0.0 with tigervnc-java 2.0.4) */
	static const int32_t tool[] = { -307, -224, -313, -312, -258, -1063131698, 16, 1, 7, 5, 2, 1, 0, -254 };
	for (int bw = 0; bw < 2; bw++) {
		on_client_stats(core, s, &cs);
		before = cs.requests;
		encodings(s, tool, 14, bw, req, 10, t);
		on_client_stats(core, s, &cs);
		CHECK(bits_of(s) == 0 && cs.requests == before + 1, "a test tool's list (%s): no pointer encoding, the request after it read",
		      bw ? "byte by byte" : "whole");
	}
	static const int32_t cur[] = { -239 }, both[] = { -240, 0, 16, -314 }, last[] = { 0, 1, -239 };
	encodings(s, cur, 1, 0, NULL, 0, t);
	CHECK(bits_of(s) == 1, "Cursor (-239) alone: asked for (%d)", bits_of(s));
	encodings(s, tool, 14, 0, NULL, 0, t);
	CHECK(bits_of(s) == 0, "a later list without it: no longer asked for");
	encodings(s, cur, 1, 1, NULL, 0, t);
	CHECK(bits_of(s) == 1, "Cursor alone, byte by byte: asked for");
	encodings(s, both, 4, 1, NULL, 0, t);
	CHECK(bits_of(s) == 6, "XCursor first and CursorWithAlpha last, byte by byte: both (%d)", bits_of(s));
	encodings(s, last, 3, 0, NULL, 0, t);
	CHECK(bits_of(s) == 1, "Cursor last in a list");
	on_client_stats(core, s, &cs);
	before = cs.requests;
	encodings(s, NULL, 0, 0, req, 10, t);
	on_client_stats(core, s, &cs);
	CHECK(bits_of(s) == 0 && cs.requests == before + 1, "an empty list after one with Cursor: no longer asked for, the request after it read");
	static const int32_t near[] = { -223, -224, -238, -241, -313, -315 };
	encodings(s, near, 6, 0, NULL, 0, t);
	CHECK(bits_of(s) == 0, "the neighbours of the pointer encodings are not taken for them");
	static const int32_t misaligned[] = { 0x000000ff, (int32_t)0xffff1100 }; /* FF FF FF 11 across an entry boundary */
	encodings(s, misaligned, 2, 1, NULL, 0, t);
	CHECK(bits_of(s) == 0, "the bytes of -239 across two entries are not taken for it");

	/* the longest list: 65,535 entries, Cursor the last, in pieces of 1000 bytes; not committed before its end */
	static int32_t longest[65535];
	memset(longest, 0, sizeof(longest));
	longest[0] = -239; /* first: a list committed entry by entry would show it long before the end */
	{
		static uint8_t m[4 + 4 * 65535 + 10];
		size_t k = 0;
		m[k++] = 2; m[k++] = 0; m[k++] = 0xff; m[k++] = 0xff;
		for (int i = 0; i < 65535; i++) {
			uint32_t v = (uint32_t)longest[i];
			m[k++] = (uint8_t)(v >> 24); m[k++] = (uint8_t)(v >> 16); m[k++] = (uint8_t)(v >> 8); m[k++] = (uint8_t)v;
		}
		memcpy(m + k, req, 10);
		k += 10;
		on_client_stats(core, s, &cs);
		before = cs.requests;
		int early = 0;
		for (size_t i = 0; i < k; i += 1000) {
			on_input(core, s, m + i, k - i < 1000 ? k - i : 1000, t);
			if (i + 1000 < k - 14)
				early |= bits_of(s) != 0;
		}
		on_client_stats(core, s, &cs);
		CHECK(!early && bits_of(s) == 1 && cs.requests == before + 1 && cs.state == 2,
		      "65,535 entries in pieces, Cursor first: taken only at the end of the list, and the request after it read");
	}

	/* a close in the middle of a list; the slot reused starts without it */
	{
		const uint8_t part[] = { 2, 0, 0, 2, 0xff, 0xff, 0xff };
		on_input(core, s, part, sizeof(part), t);
		on_closed(core, s, t);
		on_schedule(core, t);
		count_actions(ON_ACT_NONE);
		int s2 = client(22, t);
		on_client_stats(core, s2, &cs);
		CHECK(s2 == s && bits_of(s2) == 0 && cs.requests == 0, "a slot closed in the middle of a list is reused clean");
		on_input(core, s2, req, 10, t);
		on_client_stats(core, s2, &cs);
		CHECK(cs.requests == 1 && cs.state == 2, "and reads its messages from the start");
	}
	on_core_free(core);
}

static void test_pointer_arrow(void)
{
	core = new_core();
	on_time t = 1000 * MS;
	static const int32_t cur[] = { 0, -239 }, plain[] = { 0 };
	int v = client(31, t), p = client(32, t); /* v: a viewer that asks for a pointer; p: a test tool */
	encodings(v, cur, 2, 0, NULL, 0, t);
	encodings(p, plain, 1, 0, NULL, 0, t);
	struct on_client_stats cs;
	struct on_stats st;
	size_t n;
	fill(40);

	/* nothing drawn before a position is known */
	request(v, 0, 0, 0, W, H, t);
	request(p, 0, 0, 0, W, H, t);
	pass(t);
	arrow_x = arrow_y = -1;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 12, "no position yet: the viewer's frame has no arrow");
	drain(p, NULL, sizeof(big), t);

	/* p moves the pointer: the viewer gets the arrow there, the test tool gets nothing for it */
	t += 100 * MS;
	request(v, 1, 0, 0, W, H, t);
	request(p, 1, 0, 0, W, H, t);
	pointer_to(p, 30, 30, t);
	on_stats(core, &st);
	uint64_t content = st.content, captures = st.captures;
	on_schedule(core, t);
	CHECK(count_actions(ON_ACT_CAPTURE) == 0, "a pointer move asks for no capture");
	arrow_x = 30, arrow_y = 30;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "the viewer gets the arrow at 30,30, every pixel right (%d)", check_update(big, n));
	CHECK(drain(p, NULL, sizeof(big), t) == 0, "the test tool, with a request waiting, gets nothing for the move");
	on_stats(core, &st);
	CHECK(st.content == content && st.captures == captures, "a pointer-only update leaves content and captures alone");
	on_client_stats(core, v, &cs);
	CHECK(cs.gets_arrow == 1 && cs.pointer_encodings == 1, "the viewer is shown as getting the arrow");
	on_client_stats(core, p, &cs);
	CHECK(cs.gets_arrow == 0 && cs.pointer_encodings == 0, "the test tool is shown as not getting it");

	/* the test tool's own full frame has no arrow */
	request(p, 0, 0, 0, W, H, t);
	pass(t);
	arrow_x = -1;
	n = drain(p, big, sizeof(big), t);
	CHECK(check_update(big, n) == 12, "the test tool's full frame: no arrow anywhere");

	/* a move away from where the viewer was last sent the arrow: the old place is sent again, clean */
	t += 100 * MS;
	pointer_to(p, 150, 100, t); /* tile (2,1); the arrow was in tile (0,0) */
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = 150, arrow_y = 100;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 2 && update_covers(big, n, 30, 30) && update_covers(big, n, 150, 100),
	      "a move away: the old place sent clean and the new one with the arrow (%d)", check_update(big, n));

	/* three moves with no update between: one update, the arrow only at the last, the old places clean */
	t += 100 * MS;
	pointer_to(p, 10, 10, t);
	pointer_to(p, 150, 20, t);
	pointer_to(p, 60, 60, t); /* across the corner of four tiles */
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = 60, arrow_y = 60;
	n = drain(v, big, sizeof(big), t);
	int r = check_update(big, n);
	CHECK(r == 6 && update_covers(big, n, 10, 10) && update_covers(big, n, 150, 20) && update_covers(big, n, 63, 63) &&
	      update_covers(big, n, 64, 64), "three moves: one update, every old place sent clean, the arrow at 60,60 across four tiles (%d)", r);

	/* the screen changes under the resting arrow: the arrow stays */
	t += 100 * MS;
	frame[(70 * W + 70) * 4] ^= 0x5a; /* tile (1,1), under the arrow */
	request(v, 1, 0, 0, W, H, t);
	on_signalled(core, t);
	pass(t);
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "a change under the resting arrow: sent with the arrow over it");
	drain(p, NULL, sizeof(big), t);

	/* a request for part of the screen that cuts the arrow */
	t += 100 * MS;
	pointer_to(p, 20, 20, t);
	request(v, 1, 0, 0, 25, 25, t);
	pass(t);
	arrow_x = 20, arrow_y = 20;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) >= 1 && update_covers(big, n, 20, 20) && !update_covers(big, n, 26, 26),
	      "an area that cuts the arrow: its part of the arrow, nothing outside the area");
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) >= 1, "the rest of the cut tiles follows, with the arrow");

	/* the viewer stops asking for a pointer: the arrow is taken back without a move; asks again: back at once */
	t += 100 * MS;
	encodings(v, plain, 1, 0, NULL, 0, t);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = -1;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1 && update_covers(big, n, 20, 20), "the viewer no longer asks: the arrow's tile sent clean");
	encodings(v, cur, 2, 0, NULL, 0, t);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = 20;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1 && update_covers(big, n, 20, 20), "it asks again: the arrow is sent at once");

	/* the switch: off takes the arrow back, on brings it */
	t += 100 * MS;
	on_set_pointer(core, 0);
	CHECK(on_pointer(core) == 0, "the arrow switched off");
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = -1;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "switched off: the arrow's tile sent clean");
	pointer_to(p, 100, 100, t);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	CHECK(drain(v, NULL, sizeof(big), t) == 0, "switched off: a move sends the viewer nothing");
	on_client_stats(core, v, &cs);
	CHECK(cs.gets_arrow == 0 && cs.pointer_encodings == 1, "switched off: still asks, does not get it");
	on_set_pointer(core, 1);
	pass(t);
	arrow_x = 100, arrow_y = 100;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) == 1, "switched on: the arrow appears where the pointer went while it was off");

	/* a move while the viewer's queue is full arrives when it drains */
	t += 100 * MS;
	request(v, 0, 0, 0, W, H, t);
	pass(t); /* a full frame queued, not drained */
	request(v, 1, 0, 0, W, H, t);
	pointer_to(p, 140, 30, t);
	pass(t);
	drain(v, big, sizeof(big), t); /* the old full frame, with the arrow where it was */
	pass(t);
	arrow_x = 140, arrow_y = 30;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) >= 1 && update_covers(big, n, 140, 30), "a move while the queue was full arrives after it drains");

	/* events that are dropped, and the release when a client goes, do not move the arrow */
	t += 100 * MS;
	on_input(core, p, (const uint8_t[]){ 5, 1, 0, 140, 0, 30 }, 6, t); /* p holds button 1 */
	on_input(core, v, (const uint8_t[]){ 5, 0, 0, 10, 0, 100 }, 6, t); /* v's move: dropped, p holds a button */
	count_actions(ON_ACT_NONE);
	on_closed(core, p, t); /* releases p's button: a pointer action at -1,-1 */
	on_schedule(core, t);
	count_actions(ON_ACT_NONE);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	CHECK(drain(v, NULL, sizeof(big), t) == 0, "a dropped move and the release on disconnect leave the arrow where it was");

	/* the viewer's own moves move it too; a pixel format other than the native one */
	t += 100 * MS;
	const uint8_t bgr[20] = { 0, 0, 0, 0, 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 0, 8, 16, 0, 0, 0 }; /* red at 0, blue at 16 */
	on_input(core, v, bgr, 20, t);
	pointer_to(v, 30, 40, t);
	request(v, 0, 0, 0, W, H, t);
	pass(t);
	n = drain(v, big, sizeof(big), t);
	{
		int ok = n > 16 && big[0] == 0 && (big[2] << 8 | big[3]) == 12;
		size_t at = 4;
		for (int k = 0; ok && k < 12; k++) {
			int x = big[at] << 8 | big[at + 1], y = big[at + 2] << 8 | big[at + 3];
			int w = big[at + 4] << 8 | big[at + 5], h = big[at + 6] << 8 | big[at + 7];
			at += 12;
			for (int j = 0; ok && j < h; j++)
				for (int i = 0; ok && i < w; i++, at += 4) {
					arrow_x = 30, arrow_y = 40;
					const uint8_t *e = expected(x + i, y + j); /* B, G, R */
					ok = big[at] == e[2] && big[at + 1] == e[1] && big[at + 2] == e[0] && big[at + 3] == 0;
				}
		}
		CHECK(ok, "red-at-0 format: the frame and the arrow in the client's format, after its own move to 30,40");
	}

	/* a format the conversion visibly changes (10 bits a channel: white is 1023): the arrow goes through it too */
	{
		const uint8_t ten[20] = { 0, 0, 0, 0, 32, 30, 0, 1, 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, 20, 10, 0, 0, 0, 0 };
		t += 100 * MS; /* past the hold-off after the last capture */
		on_input(core, v, ten, 20, t);
		request(v, 0, 0, 0, W, H, t);
		pass(t);
		n = drain(v, big, sizeof(big), t);
		int ok = n > 16 && (big[2] << 8 | big[3]) == 12, arrow_px = 0;
		size_t at = 4;
		for (int k = 0; ok && k < 12; k++) {
			int x = big[at] << 8 | big[at + 1], y = big[at + 2] << 8 | big[at + 3];
			int w = big[at + 4] << 8 | big[at + 5], h = big[at + 6] << 8 | big[at + 7];
			at += 12;
			for (int j = 0; ok && j < h; j++)
				for (int i = 0; ok && i < w; i++, at += 4) {
					arrow_x = 30, arrow_y = 40;
					const uint8_t *e = expected(x + i, y + j);
					uint32_t want = (uint32_t)e[2] * 1023 / 255 << 20 | (uint32_t)e[1] * 1023 / 255 << 10 |
							(uint32_t)e[0] * 1023 / 255;
					uint32_t got = (uint32_t)big[at] | (uint32_t)big[at + 1] << 8 | (uint32_t)big[at + 2] << 16 |
						       (uint32_t)big[at + 3] << 24;
					ok = got == want;
					arrow_px += want == 0x3fffffff && x + i == 31 && y + j == 42; /* a white arrow pixel */
				}
		}
		CHECK(ok && arrow_px == 1, "10 bits a channel: the frame and the arrow converted (white arrow pixel 0x3fffffff)");
		const uint8_t native32[20] = { 0, 0, 0, 0, 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0 };
		on_input(core, v, native32, 20, t);
		pass(t);
		drain(v, NULL, sizeof(big), t);
	}

	/* the arrow cut by the right and bottom edges of a partial tile, pixel by pixel */
	t += 100 * MS;
	pointer_to(v, 195, 125, t);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	arrow_x = 195, arrow_y = 125;
	n = drain(v, big, sizeof(big), t);
	CHECK(check_update(big, n) >= 1 && update_covers(big, n, 199, 129), "the arrow cut at the corner, every pixel right");

	/* positions the driver reports are kept within the screen */
	t += 100 * MS;
	on_pointer_at(core, -5, 500, t);
	request(v, 1, 0, 0, W, H, t);
	pass(t);
	n = drain(v, big, sizeof(big), t);
	CHECK(n > 4 && update_covers(big, n, 0, H - 1), "a reported position off the screen is drawn at its edge");
	on_core_free(core);

	/* the edges, at a size made of whole tiles (128 x 128: 2 x 2): the arrow is cut, no tile outside is marked */
	struct on_config cfg = { 128, 128, "edges", 10000 * MS, 30000 * MS, 33 * MS, 2000 * MS, 60000 * MS, 0 };
	core = on_core_new(&cfg);
	uint8_t pw[8];
	on_vnc_password((const char *)pw_text, pw);
	on_set_key(core, pw, 1);
	static uint8_t frame128[128 * 128 * 4];
	int e = client(41, t);
	encodings(e, cur, 2, 0, NULL, 0, t);
	request(e, 0, 0, 0, 128, 128, t);
	on_schedule(core, t);
	struct on_action a;
	while (on_next_action(core, &a))
		if (a.type == ON_ACT_CAPTURE)
			on_captured(core, frame128, 128 * 4, t, t + MS);
	on_schedule(core, t + MS);
	drain(e, NULL, sizeof(big), t);
	struct { int x, y, tiles; const char *what; } edge[] = {
		{ 127, 127, 1, "the bottom right corner" }, { 127, 60, 2, "the right edge, across two tiles" },
		{ 60, 127, 2, "the bottom edge, across two tiles" }, { 120, 0, 1, "the top right" }, { 63, 63, 4, "the middle, across four" } };
	int px = -1, py = -1;
	for (size_t k = 0; k < sizeof(edge) / sizeof(edge[0]); k++) {
		pointer_to(e, edge[k].x, edge[k].y, t);
		on_client_stats(core, e, &cs);
		int marked = cs.marked_tiles;
		/* exactly the tiles under the arrow's old and new places, each cut to the screen, are marked */
		int want = 0;
		for (int ty = 0; ty < 2; ty++)
			for (int tx = 0; tx < 2; tx++) {
				int x0 = tx * 64, y0 = ty * 64, hit = 0;
				for (int w = 0; w < 2; w++) {
					int ax = w ? edge[k].x : px, ay = w ? edge[k].y : py;
					if (ax < 0)
						continue;
					int ax1 = ax + 12 < 128 ? ax + 12 : 128, ay1 = ay + 19 < 128 ? ay + 19 : 128;
					hit |= ax < x0 + 64 && ax1 > x0 && ay < y0 + 64 && ay1 > y0;
				}
				want += hit;
			}
		CHECK(marked == want, "edge, %s: the tiles marked are those under the old and new arrow (%d, want %d)",
		      edge[k].what, marked, want);
		px = edge[k].x;
		py = edge[k].y;
		request(e, 1, 0, 0, 128, 128, t);
		on_schedule(core, t);
		n = drain(e, big, sizeof(big), t);
		/* the previous place is marked too, so count the new place's tiles from the update */
		int covered = 0;
		for (int ty = 0; ty < 2; ty++)
			for (int tx = 0; tx < 2; tx++) {
				int x0 = tx * 64, y0 = ty * 64;
				int in = edge[k].x < x0 + 64 && edge[k].x + 12 > x0 && edge[k].y < y0 + 64 && edge[k].y + 19 > y0;
				covered += in && update_covers(big, n, x0 + (edge[k].x > x0 ? edge[k].x - x0 : 0),
							       y0 + (edge[k].y > y0 ? edge[k].y - y0 : 0));
			}
		on_client_stats(core, e, &cs);
		CHECK(marked <= 4 && covered == edge[k].tiles && cs.marked_tiles == 0,
		      "edge, %s: %d tile(s) with the arrow sent, none left marked (marked %d before)", edge[k].what, covered, marked);
	}
	on_core_free(core);
	arrow_x = arrow_y = -1;
}

/* The level the driver holds the target's mouse by (phase 2): clients that get the arrow, counted anew each pass. */
static void test_arrow_clients(void)
{
	core = new_core();
	on_time t = 1000 * MS;
	CHECK(on_arrow_clients(core) == 0, "no client: 0");
	int a = client(31, t);
	CHECK(on_arrow_clients(core) == 0, "a client before SetEncodings: 0");
	static const int32_t cursor[] = { 0, -239 }, xcursor[] = { -240 }, tool[] = { 0, 1, -224 };
	encodings(a, cursor, 2, 0, NULL, 0, t);
	CHECK(on_arrow_clients(core) == 1, "a viewer asking for Cursor: 1, got %d", on_arrow_clients(core));
	int b = client(32, t);
	encodings(b, tool, 3, 0, NULL, 0, t);
	CHECK(on_arrow_clients(core) == 1, "a test client beside it: still 1, got %d", on_arrow_clients(core));
	encodings(b, xcursor, 1, 0, NULL, 0, t);
	CHECK(on_arrow_clients(core) == 2, "the second asks for XCursor: 2, got %d", on_arrow_clients(core));
	on_set_pointer(core, 0);
	CHECK(on_arrow_clients(core) == 0, "arrow off: 0, got %d", on_arrow_clients(core));
	on_set_pointer(core, 1);
	CHECK(on_arrow_clients(core) == 2, "arrow on again: 2, got %d", on_arrow_clients(core));
	encodings(b, NULL, 0, 0, NULL, 0, t);
	CHECK(on_arrow_clients(core) == 1, "an empty list from the second: 1, got %d", on_arrow_clients(core));
	on_closed(core, a, t);
	CHECK(on_arrow_clients(core) == 0, "the viewer closes: 0 at once, got %d", on_arrow_clients(core));
	on_schedule(core, t);
	count_actions(ON_ACT_NONE);
	CHECK(on_arrow_clients(core) == 0, "after the reap: 0, got %d", on_arrow_clients(core));
	on_core_free(core);
}

/* The queue between libhiddi's thread and the loop, and the position (agent/core/on_mouse.c). */
static void test_mouse(void)
{
	struct on_mouse_queue q;
	memset(&q, 0, sizeof(q));
	for (int i = 0; i < 3; i++)
		on_mouse_add(&q, 1, -1, 0, 0, i);
	CHECK(q.n == 1 && q.e[0].dx == 3 && q.e[0].dy == -3 && q.e[0].t == 0, "motion joins: n %d dx %d", q.n, q.e[0].dx);

	/* a press and a release land where they happened, however late the loop drains */
	on_mouse_queue_reset(&q);
	static const int dx[] = { 1, 2, 3, 4, 5, 6 }, b[] = { 0, 1, 1, 1, 0, 0 };
	for (int i = 0; i < 6; i++)
		on_mouse_add(&q, dx[i], 0, 0, b[i], i);
	int sum = 0, press_at = -1, release_at = -1, was = 0;
	for (int i = 0; i < q.n; i++) {
		sum += q.e[i].dx;
		if (q.e[i].buttons && !was)
			press_at = sum;
		if (!q.e[i].buttons && was)
			release_at = sum;
		was = q.e[i].buttons;
	}
	CHECK(press_at == 3, "the press after 1+2 counts, got %d", press_at);
	CHECK(release_at == 15, "the release after 1+2+3+4+5 counts, got %d", release_at);
	CHECK(sum == 21 && q.n == 5, "all motion kept: %d in %d entries", sum, q.n);

	/* a wheel notch is an entry of its own, and motion after it does not join it */
	on_mouse_queue_reset(&q);
	on_mouse_add(&q, 2, 0, 0, 0, 0);
	on_mouse_add(&q, 0, 0, 1, 0, 1);
	on_mouse_add(&q, 3, 0, 0, 0, 2);
	CHECK(q.n == 3 && q.e[1].wheel == 1 && q.e[1].dx == 0 && q.e[2].dx == 3, "wheel apart: n %d", q.n);
	on_mouse_add(&q, 0, 0, 3, 0, 3);
	CHECK(q.n == 4 && q.e[3].wheel == 3, "a wheel of 3 kept as 3");

	/* a full queue folds into its last entry */
	on_mouse_queue_reset(&q);
	uint64_t ov = q.overflows;
	for (int i = 0; i < ON_MOUSE_QUEUE; i++)
		on_mouse_add(&q, 1, 0, 0, i & 1, i);
	CHECK(q.n == ON_MOUSE_QUEUE && q.overflows == ov, "full without overflow");
	on_mouse_add(&q, 5, 7, 2, 4, 99);
	CHECK(q.n == ON_MOUSE_QUEUE && q.overflows == ov + 1, "the next folds: overflows %llu",
	      (unsigned long long)(q.overflows - ov));
	struct on_mouse_entry *l = &q.e[ON_MOUSE_QUEUE - 1];
	CHECK(l->dx == 6 && l->dy == 7 && l->wheel == 2 && l->buttons == 4 && !l->motion_only,
	      "folded: dx %d dy %d wheel %d buttons %d", l->dx, l->dy, l->wheel, l->buttons);

	CHECK(on_mouse_hid_button(1) == 1 && on_mouse_hid_button(2) == 4 && on_mouse_hid_button(3) == 2 &&
	      on_mouse_hid_button(4) == 0, "HID 1/2/3 to RFB left/right/middle");

	/* position: half pixels, 1.5 a count, coalesced or not; clamped; carries on from a viewer's position */
	struct on_mouse_pos p = { W, H, 0, 0, 0 }, p2;
	int x, y;
	on_mouse_move(&p, 0, 0, &x, &y);
	CHECK(x == W / 2 && y == H / 2, "unknown: starts at the middle, got %d,%d", x, y);
	on_mouse_set(&p, 0, 0);
	p2 = p;
	for (int i = 0; i < 10; i++)
		on_mouse_move(&p, 1, 1, &x, &y);
	CHECK(x == 15 && y == 15, "ten single counts: 15 pixels, got %d,%d", x, y);
	on_mouse_move(&p2, 10, 10, &x, &y);
	CHECK(x == 15 && y == 15, "one count of ten: 15 pixels, got %d,%d", x, y);
	on_mouse_move(&p, -1000, -1000, &x, &y);
	CHECK(x == 0 && y == 0, "clamped at the top left, got %d,%d", x, y);
	on_mouse_move(&p, 100000, 100000, &x, &y);
	CHECK(x == W - 1 && y == H - 1, "clamped at the bottom right, got %d,%d", x, y);
	on_mouse_move(&p, 1, 1, &x, &y);
	CHECK(x == W - 1 && y == H - 1, "stays inside, got %d,%d", x, y);
	on_mouse_set(&p, 50, 40);
	on_mouse_move(&p, 2, -2, &x, &y);
	CHECK(x == 53 && y == 37, "from a viewer's position, got %d,%d", x, y);
}

/* When the driver takes and gives back the mouse, and what one pointer event carries from both sources. */
static void test_mouse_level(void)
{
	struct on_mouse_level l = { 0, 0, 0 };
	on_time t = 5000 * MS, w;
	CHECK(on_mouse_level(&l, t, 0, 0, 0, 0, &w) == ON_MOUSE_NONE && w == ON_NEVER, "no viewer: nothing");
	CHECK(on_mouse_level(&l, t, 1, 0, 0, 0, &w) == ON_MOUSE_NONE && w == t + ON_MOUSE_DWELL,
	      "a viewer arrives: wait the dwell, wake at its end");
	CHECK(on_mouse_level(&l, t + 999 * MS, 1, 0, 0, 0, &w) == ON_MOUSE_NONE, "999 ms: still waiting");
	CHECK(on_mouse_level(&l, t + 1000 * MS, 1, 0, 0, 0, &w) == ON_MOUSE_TAKE, "1 s: take");
	CHECK(on_mouse_level(&l, t + 1100 * MS, 1, 1, 0, 0, &w) == ON_MOUSE_NONE && w == ON_NEVER, "held: nothing");

	/* a viewer that comes and goes inside the dwell never takes it */
	struct on_mouse_level f = { 0, 0, 0 };
	on_mouse_level(&f, t, 1, 0, 0, 0, &w);
	on_mouse_level(&f, t + 500 * MS, 0, 0, 0, 0, &w);
	CHECK(on_mouse_level(&f, t + 1200 * MS, 1, 0, 0, 0, &w) == ON_MOUSE_NONE && w == t + 2200 * MS,
	      "back after a gap: the dwell starts again");

	/* a failed take is not tried again until no viewer has been there */
	on_mouse_take_failed(&f);
	CHECK(on_mouse_level(&f, t + 9000 * MS, 1, 0, 0, 0, &w) == ON_MOUSE_NONE && w == ON_NEVER, "blocked after a failure");
	on_mouse_level(&f, t + 9100 * MS, 0, 0, 0, 0, &w);
	on_mouse_level(&f, t + 9200 * MS, 1, 0, 0, 0, &w);
	CHECK(on_mouse_level(&f, t + 10200 * MS, 1, 0, 0, 0, &w) == ON_MOUSE_TAKE, "tried again after the level dropped");

	/* the viewer goes: let go at once with no button down */
	CHECK(on_mouse_level(&l, t + 2000 * MS, 0, 1, 0, 0, &w) == ON_MOUSE_LET_GO, "viewer gone: let go");
	/* with a button down: wait for it to come up, at most ON_MOUSE_LETGO_WAIT */
	struct on_mouse_level b = { 0, 0, 0 };
	CHECK(on_mouse_level(&b, t, 0, 1, 1, 0, &w) == ON_MOUSE_NONE && w == t + ON_MOUSE_LETGO_WAIT,
	      "button down: wait, wake at the limit");
	CHECK(on_mouse_level(&b, t + 1000 * MS, 0, 1, 0, 0, &w) == ON_MOUSE_LET_GO, "button up: let go");
	struct on_mouse_level b2 = { 0, 0, 0 };
	on_mouse_level(&b2, t, 0, 1, 1, 0, &w);
	CHECK(on_mouse_level(&b2, t + 4999 * MS, 0, 1, 1, 0, &w) == ON_MOUSE_NONE, "still down before the limit: wait");
	CHECK(on_mouse_level(&b2, t + 5000 * MS, 0, 1, 1, 0, &w) == ON_MOUSE_LET_GO, "at the limit: let go anyway");
	/* a viewer coming back while the let-go waits keeps the mouse */
	struct on_mouse_level b3 = { 0, 0, 0 };
	on_mouse_level(&b3, t, 0, 1, 1, 0, &w);
	CHECK(on_mouse_level(&b3, t + 100 * MS, 1, 1, 1, 0, &w) == ON_MOUSE_NONE && b3.letgo_since == 0,
	      "viewer back: the wait is over, still held");
	/* injections refused: let go at once, button or not, viewer or not; not taken again */
	struct on_mouse_level x = { 0, 0, 0 };
	CHECK(on_mouse_level(&x, t, 1, 1, 1, 1, &w) == ON_MOUSE_LET_GO, "failed: let go at once");
	CHECK(on_mouse_level(&x, t + 5000 * MS, 1, 0, 0, 1, &w) == ON_MOUSE_NONE, "failed: not taken again");

	/* one event, both sources: the held mouse's buttons stay down under a viewer's event, and the viewer's under the
	 * mouse's (the driver ORs vnc_buttons into the mouse's events); a viewer's wheel is not kept as held */
	int wheel;
	CHECK(on_mouse_compose(0, 1, &wheel) == 1 && wheel == 0, "viewer move, mouse holds left: left stays down");
	CHECK(on_mouse_compose(4, 1, &wheel) == 5 && wheel == 0, "viewer right + mouse left");
	CHECK(on_mouse_compose(8, 0, &wheel) == 0 && wheel == -1, "viewer wheel up: -1, no button");
	CHECK(on_mouse_compose(16 | 1, 0, &wheel) == 1 && wheel == 1, "viewer wheel down with left");
	CHECK(on_mouse_compose(0, 0, &wheel) == 0 && wheel == 0, "nothing held");
	struct on_mouse_entry e = { 3, 0, 1, 2, 0, 0 };
	CHECK(on_mouse_compose_usb(1, &e, &wheel) == 3 && wheel == -1, "mouse middle + viewer left; HID notch up is -1");
	e.wheel = -2;
	e.buttons = 0;
	CHECK(on_mouse_compose_usb(4 | 8, &e, &wheel) == 4 && wheel == 2, "viewer right stays, its wheel bit does not");
}

/* --- the QNX key chord (ON-AG-004) ------------------------------------------------------------------------------- */

static void key(int s, uint32_t sym, int down, on_time t)
{
	on_input(core, s, (const uint8_t[]){ 4, (uint8_t)down, 0, 0, (uint8_t)(sym >> 24), (uint8_t)(sym >> 16),
					 (uint8_t)(sym >> 8), (uint8_t)sym }, 8, t);
}

/* Keys sent as "qv" (q down) and "q^" (q up), one character for the key: a letter as itself, A Alt, T Tab, D Delete,
 * S Shift, C Control. Each is sent at time *t, then *t moves on by the number after it in ms ("qv200 nv q^"). */
static void keys(int s, const char *script, on_time *t)
{
	for (const char *p = script; *p;) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		char k = *p++;
		int down = *p++ == 'v';
		uint32_t sym = k == 'A' ? 0xffe9 : k == 'T' ? 0xff09 : k == 'D' ? 0xffff : k == 'S' ? 0xffe1
			       : k == 'C' ? 0xffe3 : (uint32_t)(unsigned char)k;
		key(s, sym, down, *t);
		int ms = 0;
		while (*p >= '0' && *p <= '9')
			ms = ms * 10 + (*p++ - '0');
		*t += ms * MS;
	}
}

/* The key actions queued, in the same notation, space-separated; other actions as "P" (pointer) and "X" (close). */
static const char *injected(void)
{
	static char b[512];
	size_t n = 0;
	struct on_action a;
	b[0] = 0;
	while (on_next_action(core, &a) && n < sizeof(b) - 4) {
		if (a.type == ON_ACT_KEY) {
			uint32_t k = a.keysym;
			b[n++] = k == 0xffe9 ? 'A' : k == 0xff09 ? 'T' : k == 0xffff ? 'D' : k == 0xffe1 ? 'S' : k == 0xffe3 ? 'C'
				 : (char)k;
			b[n++] = a.down ? 'v' : '^';
		} else if (a.type == ON_ACT_POINTER || a.type == ON_ACT_CLOSE)
			b[n++] = a.type == ON_ACT_POINTER ? 'P' : 'X';
		else
			continue;
		b[n++] = ' ';
	}
	if (n)
		n--;
	b[n] = 0;
	return b;
}

static struct on_core *chord_core(on_time hold)
{
	struct on_config cfg = { W, H, "OpticNerve test", 10000 * MS, 30000 * MS, 33 * MS, 2000 * MS, 60000 * MS, hold };
	struct on_core *c = on_core_new(&cfg);
	uint8_t pw[8];
	on_vnc_password((const char *)pw_text, pw);
	on_set_key(c, pw, 1);
	return c;
}

static int chord_of(int s)
{
	struct on_client_stats cs;
	on_client_stats(core, s, &cs);
	return cs.chord;
}

/* One script against the core with the chord on; the key actions it gives. */
static const char *chord_run(const char *script)
{
	static char out[512];
	on_core_free(core);
	core = chord_core(150 * MS);
	on_time t = 1000 * MS;
	int s = client(3, t);
	injected();
	keys(s, script, &t);
	snprintf(out, sizeof(out), "%s", injected());
	return out;
}

#define CHORD(script, want, what) do { const char *got_ = chord_run(script); \
	CHECK(strcmp(got_, want) == 0, "%s: %s gives \"%s\", want \"%s\"", what, script, got_, want); } while (0)

static void test_chord(void)
{
	core = NULL;
	/* typing: what step 0 saw (2026-11-25), and rollover */
	CHORD("qv70 q^178 nv117 n^190 xv113 x^", "qv q^ nv n^ xv x^", "fast \"qnx\" without overlap types qnx");
	CHORD("qv100 nv40 q^30 xv20 n^30 x^", "qv nv q^ xv n^ x^", "\"qnx\" rolled, N 100 ms after Q, types qnx");
	CHORD("qv200 nv40 q^30 xv20 n^30 x^", "qv nv q^ xv n^ x^", "Q up before N up is a roll even after 200 ms");
	CHORD("qv149 nv n^ q^", "qv nv n^ q^", "N 149 ms after Q: typed");
	CHORD("qv500 qv50 qv50 q^", "qv q^", "Q alone, auto-repeated: one q, at its release");
	CHORD("Qv80 Q^", "Qv Q^", "Q with Caps Lock: Q, the same keysym down and up");
	CHORD("Cv qv q^ C^", "Cv qv q^ C^", "Ctrl+Q passes at once");
	CHORD("Sv Qv Q^ S^", "Sv Qv Q^ S^", "Shift+Q passes at once");
	CHORD("qv20 av a^ q^", "qv av a^ q^", "another key while Q is held back: q, then that key");
	CHORD("av qv300 nv n^ q^ a^", "av qv nv n^ q^ a^", "Q while another key is held: no chord, typed");
	CHORD("qv6000 nv n^ q^", "qv nv n^ q^", "N more than 5 s after Q: typed");

	/* the chord: Q held, N tapped (Alt+Tab); N held then X (Alt+Tab+Del with Tab held) */
	CHORD("qv150 nv100 n^200 q^", "Av Tv T^ A^", "Q held 150 ms, N tapped: Alt+Tab, Alt up at Q up");
	CHORD("qv300 nv80 n^100 nv80 n^100 q^", "Av Tv T^ Tv T^ A^", "each further N is one Tab");
	CHORD("qv300 nv500 nv40 nv40 n^100 q^", "Av Tv T^ A^", "N held (auto-repeat) begins the chord; its repeats are one Tab");
	CHORD("qv300 nv80 n^200 nv300 xv80 x^50 n^100 q^", "Av Tv T^ Tv Dv D^ T^ A^",
	      "Q held, N tapped, then N held and X: Delete with Tab held");
	CHORD("qv300 nv200 xv x^ n^ q^", "Av Tv Dv D^ T^ A^", "X while the first N is held: Alt, Tab and Delete");
	CHORD("qv300 nv80 n^ nv xv500 xv40 xv40 x^ n^ q^", "Av Tv T^ Tv Dv D^ T^ A^", "X held: one Delete");
	CHORD("qv300 nv80 n^ xv x^ q^", "Av Tv T^ A^", "X with N up types nothing and sends no Delete");
	CHORD("qv300 nv80 n^ nv xv q^50 nv40 xv40 n^ x^ av a^", "Av Tv T^ Tv Dv D^ T^ A^ av a^",
	      "Q up with N and X down: Delete, Tab, Alt up; N and X repeats after it type nothing");
	CHORD("qv300 nv80 n^ av a^ q^", "Av Tv T^ A^ av a^", "another key ends the chord, then types");
	CHORD("qv300 nv80 n^ qv qv q^", "Av Tv T^ A^", "Q repeats in the chord: nothing");
	CHORD("qv300 Nv80 N^ Q^", "Av Tv T^ A^", "Caps Lock: Q and N in capitals");
	CHORD("qv300 nv80 n^ q^ qv100 q^", "Av Tv T^ A^ qv q^", "after the chord, q types again");
	CHORD("qv300 nv av a^ n^ q^", "qv nv av a^ n^ q^", "Q and N held back, then another key: q, n, then the key");
	CHORD("qv300 nv80 n^ nv xv av nv xv n^ x^ a^ qv qv q^ qv q^", "Av Tv T^ Tv Dv D^ T^ A^ av a^ qv q^",
	      "another key ends the chord with Q, N and X down: their repeats and ups type nothing; then q types");
	CHORD("qv300 nv80 n^ b^ q^", "Av Tv T^ A^", "an up of a key not held does not end the chord");
	CHORD("qv b^ q^", "qv q^", "an up of a key not held keeps Q held back");
	CHORD("qv300 nv b^ n^ q^", "Av Tv T^ A^", "an up of a key not held keeps Q and N held back");

	on_time t = 1000 * MS;
	struct on_stats st;
	on_core_free(core);
	core = chord_core(150 * MS);
	int v = client(3, t), p = client(4, t);
	injected();
	keys(v, "qv300 nv80 n^ q^", &t);
	on_stats(core, &st);
	CHECK(strcmp(injected(), "Av Tv T^ A^") == 0 && st.chords == 1 && on_chord(core), "a chord is counted");

	/* Q and N in one input buffer: one time, so N is not 150 ms after Q: typed */
	on_input(core, v, (const uint8_t[]){ 4, 1, 0, 0, 0, 0, 0, 'q', 4, 1, 0, 0, 0, 0, 0, 'n' }, 16, t);
	keys(v, "n^ q^", &t);
	CHECK(strcmp(injected(), "qv nv n^ q^") == 0, "Q and N read together are typed");

	/* the core's last schedule long before: the time is that of the input */
	on_schedule(core, t);
	t += 5000 * MS;
	keys(v, "qv200 nv80 n^ q^", &t);
	CHECK(strcmp(injected(), "Av Tv T^ A^") == 0, "the hold is measured from the key events' own times");

	/* pointer: a move keeps Q held back; a click first types it */
	keys(v, "qv200", &t);
	pointer_to(v, 30, 30, t);
	keys(v, "nv80 n^ q^", &t);
	CHECK(strcmp(injected(), "Av Tv T^ A^") == 0 && chord_of(v) == 0, "a pointer move during a held-back Q keeps the chord");
	keys(v, "qv100", &t);
	on_input(core, v, (const uint8_t[]){ 5, 1, 0, 30, 0, 30 }, 6, t);
	on_input(core, v, (const uint8_t[]){ 5, 0, 0, 30, 0, 30 }, 6, t);
	keys(v, "q^", &t);
	CHECK(strcmp(injected(), "qv P P q^") == 0, "a click while Q is held back: q first, then the click");
	keys(v, "qv200 nv", &t);
	on_input(core, v, (const uint8_t[]){ 5, 1, 0, 30, 0, 30 }, 6, t);
	on_input(core, v, (const uint8_t[]){ 5, 0, 0, 30, 0, 30 }, 6, t);
	keys(v, "n^ q^", &t);
	CHECK(strcmp(injected(), "qv nv P P n^ q^") == 0, "a click while Q and N are held back: q, n first");

	/* input ownership: a held-back Q holds input like a key; another client's keys wait */
	keys(v, "qv", &t);
	on_stats(core, &st);
	uint64_t dropped = st.keys_dropped;
	keys(p, "av a^", &t);
	on_stats(core, &st);
	CHECK(strcmp(injected(), "") == 0 && st.keys_dropped == dropped + 1 && chord_of(v) == 1,
	      "while v's Q is held back, p's key is dropped and counted");
	keys(v, "q^", &t);
	CHECK(strcmp(injected(), "qv q^") == 0, "v's q is typed at its release");
	keys(v, "qv200 nv", &t);
	keys(p, "av a^", &t);
	keys(v, "n^", &t);
	keys(p, "bv b^", &t);
	keys(v, "q^", &t);
	on_stats(core, &st);
	CHECK(strcmp(injected(), "Av Tv T^ A^") == 0 && st.keys_dropped == dropped + 3,
	      "while v holds Q and N back, and in its chord, p's keys are dropped and counted");
	dropped = st.keys_dropped;
	keys(v, "av", &t);
	injected();
	keys(p, "qv400 nv n^ q^", &t);
	on_stats(core, &st);
	CHECK(strcmp(injected(), "") == 0 && chord_of(p) == 0 && st.keys_dropped == dropped + 2,
	      "p's Q and N while v holds a key: dropped and counted as any key; their releases count nothing");
	keys(v, "a^", &t);
	injected();

	/* disconnect: in the chord, Delete, Tab, Alt up then close; with Q held back, nothing injected */
	keys(v, "qv300 nv200 xv", &t);
	CHECK(strcmp(injected(), "Av Tv Dv") == 0 && chord_of(v) == 3, "in the chord");
	on_closed(core, v, t);
	on_schedule(core, t);
	CHECK(strcmp(injected(), "D^ T^ A^ X") == 0, "a disconnect in the chord releases Delete, Tab, then Alt");
	keys(p, "qv300 nv", &t);
	CHECK(chord_of(p) == 2, "Q and N held back");
	on_closed(core, p, t);
	on_schedule(core, t);
	CHECK(strcmp(injected(), "X") == 0, "a disconnect with Q and N held back injects nothing");
	v = client(5, t);
	injected();
	keys(v, "qv80 q^", &t);
	CHECK(strcmp(injected(), "qv q^") == 0 && chord_of(v) == 0, "a reused slot starts without a chord");

	/* chord_hold 0: today's behaviour */
	on_core_free(core);
	core = chord_core(0);
	v = client(3, t);
	injected();
	keys(v, "qv300 nv80 n^200 nv300 xv80 x^ n^ q^ qv500 qv50 q^", &t);
	on_stats(core, &st);
	CHECK(strcmp(injected(), "qv nv n^ nv xv x^ n^ q^ qv qv q^") == 0 && st.chords == 0 && !on_chord(core),
	      "chord off: every key passes at once, as before");
	on_core_free(core);
}

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	test_des();
	test_handshake();
	test_schedule();
	test_handover();
	test_pointer_parser();
	test_pointer_arrow();
	test_arrow_clients();
	test_mouse();
	test_mouse_level();
	test_chord();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
