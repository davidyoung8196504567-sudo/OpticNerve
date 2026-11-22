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
	struct on_config cfg = { W, H, "OpticNerve test", 10000 * MS, 30000 * MS, 33 * MS, 2000 * MS, 60000 * MS };
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

/* Parses an update and checks every pixel against `frame`. Returns the rectangle count, -1 on a bad pixel. */
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
				const uint8_t *f = frame + ((size_t)(y + j) * W + x + i) * 4;
				if (u[at] != f[0] || u[at + 1] != f[1] || u[at + 2] != f[2] || u[at + 3] != 0)
					return -1;
			}
	}
	return at == n ? count : -3;
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

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	test_des();
	test_handshake();
	test_schedule();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
