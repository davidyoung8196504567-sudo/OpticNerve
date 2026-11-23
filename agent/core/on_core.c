/*
 * on_core.c: the OpticNerve core (see on_core.h). RFB 3.8 server side, client table, tile marks and the schedule
 * step that decides captures and updates. Plain C11, no I/O, no clock, no QNX header.
 *
 * Rules carried from the design (private docs/DESIGN_SERVER.md, sections 4.3, 4.5 and 4.7):
 *   - A slot is freed in one place only, slot_free(), and only in the reap steps of on_schedule().
 *   - What a client is sent is decided on state: one "not yet sent" mark per tile and client.
 *   - At most one capture is in flight; the next starts no sooner than the hold-off after the last one ended, and
 *     the hold-off is never shorter than the last capture took.
 *   - The pixel bytes of an update are copied into the client's queue when the update is decided.
 *   - Only security type 2 (VncAuth) is offered and accepted; no client message is read before ServerInit.
 */
#include <stdlib.h>
#include <string.h>
#include "on_core.h"
#include "on_des.h"

#define MAX_CUT_TEXT (1u << 20)
#define FAIL_LINGER 2000000000LL /* 4.7: after a failure message, wait this long for the client to close */
#define ACTIONS 512

enum slot_state { S_FREE, S_HANDSHAKE, S_CLIENT, S_FAILING, S_CLOSING };
enum hs_stage { H_VERSION, H_SECTYPE, H_AUTH, H_INIT };

struct rect { int x, y, w, h; };

struct pixfmt {
	int bpp, big_endian, rmax, gmax, bmax, rshift, gshift, bshift;
};

struct slot {
	enum slot_state state;
	enum hs_stage stage;
	uint8_t challenge[16];
	uint8_t msg[20];          /* the fixed part of the message being read */
	size_t have, need;        /* bytes in msg, bytes the fixed part needs */
	uint32_t skip;            /* bytes of a variable part still to skip */
	int extended;             /* the variable part being skipped is an extended clipboard message */
	uint8_t *out;             /* output queue: out[head .. head+len) */
	size_t cap, head, len;
	on_time deadline;         /* handshake deadline, or the end of the linger after a failure */
	on_time last_progress;    /* last time queued output moved */
	struct pixfmt fmt;
	uint8_t *marks;           /* one byte per tile: 1 = not yet sent */
	int has_nonincr, has_incr;
	struct rect nonincr, incr;
	uint64_t nonincr_after;   /* the captures count when the non-incremental request arrived */
	on_time incr_since;
	int incr_timed;           /* a timed capture was taken while this incremental request waited */
	uint32_t held[ON_HELD_KEYS];
	int nheld, buttons;
	int sent_mask;            /* the button mask of this client's last pointer event, injected or not */
	uint64_t requests, updates, sent_seq;
};

struct on_core {
	struct on_config cfg;
	uint8_t key[8];
	int have_key;
	int tw, th, ntiles;       /* tiles across, down, total */
	uint8_t *cur;             /* our copy of the display, bytes B, G, R, 0 */
	struct slot slots[ON_SLOTS];
	int input_owner;          /* slot whose input is injected; -1 none */
	on_time now;              /* the time of the current on_schedule() */
	/* capture state */
	int in_flight, in_flight_timed, device_want;
	uint64_t captures_done;
	on_time last_end, last_dur;
	struct on_stats st;
	/* actions */
	struct on_action act[ACTIONS];
	int act_head, act_len;
};

static void push(struct on_core *c, struct on_action a)
{
	if (c->act_len == ACTIONS)
		return; /* not reached: a pass queues at most 6 slots x (32 keys + 2) releases, and the driver takes the
			 * actions of each input buffer it hands to on_input() before the next (agent/qnx/serve.c) */
	c->act[(c->act_head + c->act_len++) % ACTIONS] = a;
}

static int put_u16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return 2; }
static int put_u32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; return 4; }
static unsigned get_u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t get_u32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* Appends to a slot's queue. Returns 0 if it does not fit (the caller treats that as a fault of its own sizing). */
static int queue(struct slot *s, const void *data, size_t n)
{
	if (s->head + s->len + n > s->cap) {
		memmove(s->out, s->out + s->head, s->len);
		s->head = 0;
		if (s->len + n > s->cap)
			return 0;
	}
	memcpy(s->out + s->head + s->len, data, n);
	s->len += n;
	return 1;
}

static void mark_all(struct on_core *c, struct slot *s) { memset(s->marks, 1, (size_t)c->ntiles); }

static int count_free(const struct on_core *c, enum slot_state st, int *n_out)
{
	int n = 0, free_slot = -1;
	for (int i = 0; i < ON_SLOTS; i++) {
		if (c->slots[i].state == st)
			n++;
		if (c->slots[i].state == S_FREE && free_slot < 0)
			free_slot = i;
	}
	if (n_out)
		*n_out = n;
	return free_slot;
}

/* Queues a failure with its reason and lets the slot linger (4.7). Before the security types the failure is a count
 * of zero types; after them it is SecurityResult 1. Either way a reason string follows. */
static void fail_slot(struct on_core *c, struct slot *s, const char *reason, on_time now)
{
	uint8_t b[64];
	size_t n = strlen(reason);
	int k = 0;
	if (s->stage == H_VERSION)
		b[k++] = 0;
	else
		k = put_u32(b, 1);
	k += put_u32(b + k, (uint32_t)n);
	memcpy(b + k, reason, n);
	s->head = s->len = 0;
	queue(s, b, (size_t)k + n);
	s->state = S_FAILING;
	s->deadline = now + FAIL_LINGER;
	(void)c;
}

static void close_slot(struct slot *s) { if (s->state != S_FREE) s->state = S_CLOSING; }

/* "login timed out: no password within N s", N the whole seconds of t. No printf in the core. */
static const char *timeout_reason(on_time t)
{
	static char r[48];
	const char *a = "login timed out: no password within ";
	size_t n = strlen(a);
	memcpy(r, a, n);
	char d[20];
	int k = 0;
	long long sec = t / 1000000000LL;
	do
		d[k++] = (char)('0' + sec % 10);
	while ((sec /= 10) && k < 6);
	while (k)
		r[n++] = d[--k];
	memcpy(r + n, " s", 3);
	return r;
}

static int clip(const struct on_core *c, struct rect *r)
{
	int x1 = r->x + r->w, y1 = r->y + r->h;
	if (x1 > c->cfg.width) x1 = c->cfg.width;
	if (y1 > c->cfg.height) y1 = c->cfg.height;
	if (r->x >= x1 || r->y >= y1)
		return 0;
	r->w = x1 - r->x;
	r->h = y1 - r->y;
	return 1;
}

static struct rect unite(struct rect a, struct rect b)
{
	int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
	int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (struct rect){ x0, y0, x1 - x0, y1 - y0 };
}

static void mark_area(struct on_core *c, struct slot *s, struct rect r)
{
	for (int ty = r.y / ON_TILE; ty <= (r.y + r.h - 1) / ON_TILE; ty++)
		for (int tx = r.x / ON_TILE; tx <= (r.x + r.w - 1) / ON_TILE; tx++)
			s->marks[ty * c->tw + tx] = 1;
}

/* --- input from a client ------------------------------------------------------------------------------------ */

/* 4.8: the client that sends input owns it, unless the owner holds a key or a button: a press is never split from
 * its release, and a tester's viewer and a test script can take turns without either disconnecting. */
static int take_input(struct on_core *c, int idx)
{
	int o = c->input_owner;
	if (o == idx)
		return 1;
	if (o >= 0 && (c->slots[o].nheld || c->slots[o].buttons))
		return 0;
	if (o >= 0)
		c->st.input_handovers++;
	c->input_owner = idx;
	return 1;
}

static void key_event(struct on_core *c, int idx, uint32_t sym, int down)
{
	struct slot *s = &c->slots[idx];
	int at = -1;
	for (int i = 0; i < s->nheld; i++)
		if (s->held[i] == sym)
			at = i;
	if (!down && at < 0)
		return; /* a release of a key this client does not hold: nothing to undo, and no reason to take input */
	if (!take_input(c, idx)) {
		c->st.keys_dropped++;
		return;
	}
	if (down) {
		if (at < 0) {
			if (s->nheld == ON_HELD_KEYS) {
				c->st.keys_dropped++;
				return;
			}
			s->held[s->nheld++] = sym;
		}
	} else
		s->held[at] = s->held[--s->nheld];
	push(c, (struct on_action){ .type = ON_ACT_KEY, .slot = idx, .keysym = sym, .down = down });
}

static void pointer_event(struct on_core *c, int idx, int buttons, int x, int y)
{
	struct slot *s = &c->slots[idx];
	int was = s->sent_mask;
	s->sent_mask = buttons;
	/* a button this client pressed while another owned input stays dropped until it is let go: taking over in the
	 * middle of that drag would put a press down where the user never pressed */
	if ((c->input_owner != idx && (buttons & was)) || !take_input(c, idx)) {
		c->st.pointer_dropped++;
		return;
	}
	if (x >= c->cfg.width) x = c->cfg.width - 1;
	if (y >= c->cfg.height) y = c->cfg.height - 1;
	s->buttons = buttons;
	push(c, (struct on_action){ .type = ON_ACT_POINTER, .slot = idx, .x = x, .y = y, .buttons = buttons });
}

static int set_pixel_format(struct slot *s, const uint8_t *m)
{
	struct pixfmt f = { m[4], m[6] != 0, (int)get_u16(m + 8), (int)get_u16(m + 10), (int)get_u16(m + 12),
			    m[14], m[15], m[16] };
	int truecolour = m[7] != 0;
	if (!truecolour || f.bpp != 32) /* 8 and 16 bpp are on the cut list (design section 8, item 4) */
		return 0;
	int maxes[3] = { f.rmax, f.gmax, f.bmax }, shifts[3] = { f.rshift, f.gshift, f.bshift };
	for (int i = 0; i < 3; i++) {
		if (maxes[i] == 0 || (maxes[i] & (maxes[i] + 1)) != 0 || shifts[i] > 31)
			return 0;
		int bits = 0;
		for (int v = maxes[i]; v; v >>= 1)
			bits++;
		if (shifts[i] + bits > 32)
			return 0;
	}
	s->fmt = f;
	return 1;
}

/* A complete fixed part of a client message is in s->msg. */
static void client_message(struct on_core *c, int idx, on_time now)
{
	struct slot *s = &c->slots[idx];
	const uint8_t *m = s->msg;
	switch (m[0]) {
	case 0:
		if (!set_pixel_format(s, m))
			close_slot(s);
		else
			mark_all(c, s);
		break;
	case 2:
		s->skip = 4u * get_u16(m + 2); /* the encodings: Raw is always allowed, nothing else is used yet */
		break;
	case 3: {
		struct rect r = { (int)get_u16(m + 2), (int)get_u16(m + 4), (int)get_u16(m + 6), (int)get_u16(m + 8) };
		s->requests++;
		if (!clip(c, &r))
			break;
		if (m[1]) {
			s->incr = s->has_incr ? unite(s->incr, r) : r;
			if (!s->has_incr) {
				s->incr_since = now;
				s->incr_timed = 0;
			}
			s->has_incr = 1;
		} else {
			s->nonincr = s->has_nonincr ? unite(s->nonincr, r) : r;
			s->has_nonincr = 1;
			s->nonincr_after = c->captures_done;
			mark_area(c, s, r);
		}
		break;
	}
	case 4:
		key_event(c, idx, get_u32(m + 4), m[1] != 0);
		break;
	case 5:
		pointer_event(c, idx, m[1], (int)get_u16(m + 2), (int)get_u16(m + 4));
		break;
	case 6: {
		int32_t n = (int32_t)get_u32(m + 4);
		if (n < 0) {
			s->skip = (uint32_t)(-(int64_t)n);
			s->extended = 1;
			c->st.extended_cut_text++;
		} else if ((uint32_t)n > MAX_CUT_TEXT) {
			close_slot(s);
		} else {
			s->skip = (uint32_t)n;
			c->st.cut_text_skipped++;
		}
		break;
	}
	default:
		close_slot(s);
	}
}

static size_t fixed_size(uint8_t type)
{
	switch (type) {
	case 0: return 20;
	case 2: return 4;
	case 3: return 10;
	case 4: return 8;
	case 5: return 6;
	case 6: return 8;
	default: return 1;
	}
}

static void handshake_input(struct on_core *c, int idx, on_time now)
{
	struct slot *s = &c->slots[idx];
	const uint8_t *m = s->msg;
	switch (s->stage) {
	case H_VERSION:
		if (memcmp(m, "RFB 003.008\n", 12) != 0) {
			fail_slot(c, s, "only RFB 3.8 is supported", now);
			return;
		}
		queue(s, (const uint8_t[]){ 1, 2 }, 2);
		s->stage = H_SECTYPE;
		s->need = 1;
		break;
	case H_SECTYPE:
		if (m[0] != 2) {
			c->st.logins_failed++;
			fail_slot(c, s, "security type not offered", now);
			return;
		}
		queue(s, s->challenge, 16);
		s->stage = H_AUTH;
		s->need = 16;
		/* I6: a viewer now asks its user for the password */
		s->deadline = c->cfg.auth_timeout > 0 ? now + c->cfg.auth_timeout : ON_NEVER;
		break;
	case H_AUTH: {
		uint8_t want[16], diff = 0;
		on_vnc_response(c->key, s->challenge, want);
		for (int i = 0; i < 16; i++)
			diff |= (uint8_t)(want[i] ^ m[i]);
		memset(want, 0, sizeof(want));
		if (diff || !c->have_key) {
			c->st.logins_failed++;
			fail_slot(c, s, "authentication failed", now);
			return;
		}
		int clients = 0;
		count_free(c, S_CLIENT, &clients);
		if (clients >= ON_CLIENT_SLOTS) {
			c->st.server_full++;
			fail_slot(c, s, "server full", now);
			return;
		}
		uint8_t ok[4];
		put_u32(ok, 0);
		queue(s, ok, 4);
		s->state = S_CLIENT; /* a client slot from here on: the handshake count no longer includes it */
		s->stage = H_INIT;
		s->need = 1;
		s->deadline = now + c->cfg.handshake_timeout; /* ClientInit is a program's step again: 10 s, not 60 */
		break;
	}
	case H_INIT: {
		/* ClientInit: the shared flag is read and ignored. Then ServerInit, before any client message is read. */
		size_t nl = strlen(c->cfg.desktop_name);
		uint8_t b[24 + 256];
		if (nl > 256)
			nl = 256;
		int k = put_u16(b, (unsigned)c->cfg.width);
		k += put_u16(b + k, (unsigned)c->cfg.height);
		const uint8_t pf[16] = { 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0 };
		memcpy(b + k, pf, 16);
		k += 16;
		k += put_u32(b + k, (uint32_t)nl);
		memcpy(b + k, c->cfg.desktop_name, nl);
		queue(s, b, (size_t)k + nl);
		s->fmt = (struct pixfmt){ 32, 0, 255, 255, 255, 16, 8, 0 };
		mark_all(c, s);
		s->stage = H_INIT + 1;
		s->need = 1;
		s->deadline = ON_NEVER;
		break;
	}
	}
}

void on_input(struct on_core *c, int idx, const uint8_t *data, size_t len, on_time now)
{
	struct slot *s = &c->slots[idx];
	while (len > 0) {
		if (s->state == S_FAILING || s->state == S_CLOSING || s->state == S_FREE)
			return; /* dropped: nothing more is read from a slot that is going */
		if (s->skip) {
			size_t n = len < s->skip ? len : s->skip;
			s->skip -= (uint32_t)n;
			data += n;
			len -= n;
			if (!s->skip)
				s->extended = 0;
			continue;
		}
		size_t n = s->need - s->have;
		if (n > len)
			n = len;
		memcpy(s->msg + s->have, data, n);
		s->have += n;
		data += n;
		len -= n;
		if (s->state == S_CLIENT && s->stage > H_INIT && s->have == 1)
			s->need = fixed_size(s->msg[0]);
		if (s->have < s->need)
			continue;
		s->have = 0;
		if (s->stage <= H_INIT) {
			handshake_input(c, idx, now);
		} else {
			client_message(c, idx, now);
			s->need = 1;
		}
	}
}

/* --- the slot table ------------------------------------------------------------------------------------------- */

int on_accept(struct on_core *c, const uint8_t challenge[16], on_time now)
{
	int handshakes = 0;
	int idx = count_free(c, S_HANDSHAKE, &handshakes);
	if (idx < 0 || handshakes >= ON_HANDSHAKE_SLOTS) {
		c->st.refused_at_accept++;
		return -1;
	}
	struct slot *s = &c->slots[idx];
	uint8_t *out = s->out, *marks = s->marks;
	size_t cap = s->cap;
	memset(s, 0, sizeof(*s)); /* a reused slot starts clean: nothing held, nothing pending */
	s->out = out;
	s->marks = marks;
	s->cap = cap;
	memset(marks, 0, (size_t)c->ntiles);
	s->state = S_HANDSHAKE;
	s->stage = H_VERSION;
	s->need = 12;
	memcpy(s->challenge, challenge, 16);
	s->deadline = now + c->cfg.handshake_timeout;
	s->last_progress = now;
	queue(s, "RFB 003.008\n", 12);
	return idx;
}

void on_closed(struct on_core *c, int idx, on_time now)
{
	(void)now;
	close_slot(&c->slots[idx]);
}

size_t on_output(struct on_core *c, int idx, const uint8_t **data)
{
	struct slot *s = &c->slots[idx];
	*data = s->out + s->head;
	return s->state == S_FREE ? 0 : s->len;
}

void on_sent(struct on_core *c, int idx, size_t n, on_time now)
{
	struct slot *s = &c->slots[idx];
	if (n > s->len)
		n = s->len;
	s->head += n;
	s->len -= n;
	if (!s->len)
		s->head = 0;
	if (n)
		s->last_progress = now;
	if (s->state == S_FAILING && !s->len)
		push(c, (struct on_action){ .type = ON_ACT_SHUTDOWN_SEND, .slot = idx });
}

/* The one place a slot is freed (I4): release what the client holds, then close, then reset. */
static void slot_free(struct on_core *c, int idx)
{
	struct slot *s = &c->slots[idx];
	if (c->input_owner == idx) {
		for (int i = 0; i < s->nheld; i++)
			push(c, (struct on_action){ .type = ON_ACT_KEY, .slot = idx, .keysym = s->held[i], .down = 0 });
		if (s->buttons)
			push(c, (struct on_action){ .type = ON_ACT_POINTER, .slot = idx, .x = -1, .y = -1, .buttons = 0 });
		c->input_owner = -1;
	}
	push(c, (struct on_action){ .type = ON_ACT_CLOSE, .slot = idx });
	s->state = S_FREE;
	s->nheld = 0;
	s->buttons = 0;
	s->len = s->head = 0;
}

static void reap(struct on_core *c)
{
	for (int i = 0; i < ON_SLOTS; i++)
		if (c->slots[i].state == S_CLOSING)
			slot_free(c, i);
}

/* --- capture and updates --------------------------------------------------------------------------------------- */

void on_signalled(struct on_core *c, on_time now)
{
	(void)now;
	c->st.signals++;
	c->st.signalled = 1;
}

void on_captured(struct on_core *c, const uint8_t *px, int stride, on_time started, on_time ended)
{
	int changed = 0;
	size_t row = (size_t)c->cfg.width * 4;
	/* The comparison runs over every pixel of every capture, so it is written to be fast: each pixel as one 32-bit
	 * word, XOR against our copy, ORed over the tile's row, and only then the padding byte masked out (it is never
	 * compared), with no branch inside a row. On the Pi at 1920 x 1080 (2026-11-22) this brought the comparison of a
	 * frame from about 89 ms, byte by byte, to under 5 ms on the copy that qnx/serve.c passes in. */
	const uint8_t mb[4] = { 0xff, 0xff, 0xff, 0 };
	uint32_t mask;
	memcpy(&mask, mb, 4);
	for (int ty = 0; ty < c->th; ty++)
		for (int tx = 0; tx < c->tw; tx++) {
			int x0 = tx * ON_TILE, y0 = ty * ON_TILE;
			int w = c->cfg.width - x0 < ON_TILE ? c->cfg.width - x0 : ON_TILE;
			int h = c->cfg.height - y0 < ON_TILE ? c->cfg.height - y0 : ON_TILE;
			int diff = 0;
			for (int y = y0; y < y0 + h && !diff; y++) {
				const uint8_t *a = px + (size_t)y * stride + (size_t)x0 * 4, *b = c->cur + y * row + (size_t)x0 * 4;
				uint32_t acc = 0;
				for (int x = 0; x < w; x++) {
					uint32_t pa, pb;
					memcpy(&pa, a + 4 * x, 4);
					memcpy(&pb, b + 4 * x, 4);
					acc |= pa ^ pb;
				}
				diff = (acc & mask) != 0;
			}
			if (!diff)
				continue;
			changed = 1;
			for (int y = y0; y < y0 + h; y++) {
				const uint8_t *a = px + (size_t)y * stride + (size_t)x0 * 4;
				uint8_t *b = c->cur + y * row + (size_t)x0 * 4;
				for (int x = 0; x < w; x++) {
					b[4 * x] = a[4 * x];
					b[4 * x + 1] = a[4 * x + 1];
					b[4 * x + 2] = a[4 * x + 2];
					b[4 * x + 3] = 0;
				}
			}
			for (int i = 0; i < ON_SLOTS; i++)
				if (c->slots[i].state == S_CLIENT)
					c->slots[i].marks[ty * c->tw + tx] = 1;
		}
	if (changed)
		c->st.content++;
	if (changed && c->in_flight_timed)
		for (int i = 0; i < ON_SLOTS; i++)
			if (c->slots[i].state == S_CLIENT && c->slots[i].has_incr)
				c->slots[i].incr_timed = 1;
	c->in_flight = 0;
	c->captures_done++;
	c->st.captures++;
	c->last_end = ended;
	c->last_dur = ended - started;
}

static void put_pixel(const struct pixfmt *f, const uint8_t *bgr, uint8_t *o)
{
	uint32_t r = bgr[2], g = bgr[1], b = bgr[0];
	if (f->rmax != 255) r = r * (uint32_t)f->rmax / 255;
	if (f->gmax != 255) g = g * (uint32_t)f->gmax / 255;
	if (f->bmax != 255) b = b * (uint32_t)f->bmax / 255;
	uint32_t v = r << f->rshift | g << f->gshift | b << f->bshift;
	if (f->big_endian)
		put_u32(o, v);
	else {
		o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); o[2] = (uint8_t)(v >> 16); o[3] = (uint8_t)(v >> 24);
	}
}

static int native(const struct pixfmt *f)
{
	return !f->big_endian && f->rmax == 255 && f->gmax == 255 && f->bmax == 255 && f->rshift == 16 &&
	       f->gshift == 8 && f->bshift == 0;
}

/* Builds one FramebufferUpdate of the marked tiles inside `area`, in Raw, into the slot's queue. */
static void send_update(struct on_core *c, int idx, struct rect area)
{
	struct slot *s = &c->slots[idx];
	size_t at = s->len; /* the header's offset from head; queue() may move the data but keeps head-relative order */
	uint8_t hdr[4] = { 0, 0, 0, 0 };
	if (!s->len)
		s->last_progress = c->now;
	queue(s, hdr, 4);
	unsigned count = 0;
	size_t row = (size_t)c->cfg.width * 4;
	for (int ty = area.y / ON_TILE; ty <= (area.y + area.h - 1) / ON_TILE; ty++)
		for (int tx = area.x / ON_TILE; tx <= (area.x + area.w - 1) / ON_TILE; tx++) {
			if (!s->marks[ty * c->tw + tx])
				continue;
			int x0 = tx * ON_TILE, y0 = ty * ON_TILE;
			int x1 = x0 + ON_TILE < c->cfg.width ? x0 + ON_TILE : c->cfg.width;
			int y1 = y0 + ON_TILE < c->cfg.height ? y0 + ON_TILE : c->cfg.height;
			int whole = x0 >= area.x && y0 >= area.y && x1 <= area.x + area.w && y1 <= area.y + area.h;
			if (x0 < area.x) x0 = area.x;
			if (y0 < area.y) y0 = area.y;
			if (x1 > area.x + area.w) x1 = area.x + area.w;
			if (y1 > area.y + area.h) y1 = area.y + area.h;
			uint8_t r[12];
			int k = put_u16(r, (unsigned)x0);
			k += put_u16(r + k, (unsigned)y0);
			k += put_u16(r + k, (unsigned)(x1 - x0));
			k += put_u16(r + k, (unsigned)(y1 - y0));
			put_u32(r + k, 0); /* Raw */
			queue(s, r, 12);
			for (int y = y0; y < y1; y++) {
				const uint8_t *src = c->cur + y * row + (size_t)x0 * 4;
				if (native(&s->fmt)) {
					queue(s, src, (size_t)(x1 - x0) * 4);
				} else {
					uint8_t px[ON_TILE * 4];
					for (int x = 0; x < x1 - x0; x++)
						put_pixel(&s->fmt, src + 4 * x, px + 4 * x);
					queue(s, px, (size_t)(x1 - x0) * 4);
				}
			}
			if (whole)
				s->marks[ty * c->tw + tx] = 0;
			count++;
		}
	if (!count) { /* nothing marked: no update; take back the header */
		s->len -= 4;
		return;
	}
	put_u16(s->out + s->head + at + 2, count);
	s->updates++;
	s->sent_seq = c->st.content;
	if (s->incr_timed)
		c->st.updates_from_timed_capture++;
	s->has_nonincr = s->has_incr = 0;
	s->incr_timed = 0;
}

size_t on_read_region(const struct on_core *c, int x, int y, int *w, int *h, uint8_t *rgb, size_t cap)
{
	struct rect r = { x, y, *w, *h };
	if (x < 0 || y < 0 || *w <= 0 || *h <= 0 || !clip(c, &r)) {
		*w = *h = 0;
		return 0;
	}
	size_t need = (size_t)r.w * r.h * 3;
	if (need > cap) {
		*w = *h = 0;
		return 0;
	}
	size_t row = (size_t)c->cfg.width * 4, k = 0;
	for (int j = 0; j < r.h; j++) {
		const uint8_t *s = c->cur + (size_t)(r.y + j) * row + (size_t)r.x * 4;
		for (int i = 0; i < r.w; i++, s += 4) {
			rgb[k++] = s[2];
			rgb[k++] = s[1];
			rgb[k++] = s[0];
		}
	}
	*w = r.w;
	*h = r.h;
	return k;
}

uint64_t on_want_capture(struct on_core *c)
{
	c->device_want = 1;
	return c->captures_done + (c->in_flight ? 2 : 1);
}

on_time on_schedule(struct on_core *c, on_time now)
{
	on_time wake = ON_NEVER;
	c->now = now;
	/* deadlines: handshake, stall, linger after a failure */
	for (int i = 0; i < ON_SLOTS; i++) {
		struct slot *s = &c->slots[i];
		if (!s->len)
			s->last_progress = now; /* a stall is counted from when output was first waiting */
		if (s->state == S_HANDSHAKE && s->stage == H_AUTH && now >= s->deadline) {
			/* the password was not answered in time: say so, as for a wrong one, then linger (4.7) */
			c->st.logins_timed_out++;
			fail_slot(c, s, timeout_reason(c->cfg.auth_timeout), now);
		} else if ((s->state == S_HANDSHAKE || s->state == S_FAILING || (s->state == S_CLIENT && s->stage <= H_INIT)) &&
			   now >= s->deadline)
			close_slot(s);
		if (s->len && now - s->last_progress >= c->cfg.stall_timeout)
			close_slot(s);
	}
	reap(c);

	/* updates: every client with an empty queue, whoever caused the last capture */
	int want = 0, want_timed = 0;
	for (int i = 0; i < ON_SLOTS; i++) {
		struct slot *s = &c->slots[i];
		if (s->state != S_CLIENT || s->stage <= H_INIT || s->len)
			continue;
		if (s->has_nonincr && c->captures_done <= s->nonincr_after) {
			want = 1; /* a non-incremental request is answered from a capture taken after it arrived */
			continue;
		}
		if (s->has_nonincr || s->has_incr) {
			struct rect area = s->has_nonincr && s->has_incr ? unite(s->nonincr, s->incr)
					   : s->has_nonincr ? s->nonincr : s->incr;
			send_update(c, i, area);
		}
		if (s->has_incr) {
			if (c->st.signalled)
				want = 1;
			else if (now - s->incr_since >= c->cfg.timed_capture && now - c->last_end >= c->cfg.timed_capture)
				want = want_timed = 1;
			else {
				on_time t = s->incr_since + c->cfg.timed_capture;
				if (c->last_end + c->cfg.timed_capture > t)
					t = c->last_end + c->cfg.timed_capture;
				if (t < wake)
					wake = t;
			}
		}
	}

	if (c->device_want)
		want = 1;
	/* at most one capture in flight, and the hold-off after the last one */
	if (want && !c->in_flight) {
		on_time gap = c->last_dur > c->cfg.holdoff_min ? c->last_dur : c->cfg.holdoff_min;
		on_time ok = c->captures_done ? c->last_end + gap : now;
		if (now >= ok) {
			c->in_flight = 1;
			c->in_flight_timed = want_timed && !c->st.signalled;
			if (c->in_flight_timed)
				c->st.timed_captures++;
			c->st.signalled = 0;
			c->device_want = 0;
			push(c, (struct on_action){ .type = ON_ACT_CAPTURE, .slot = -1 });
		} else if (ok < wake) {
			wake = ok;
		}
	}

	for (int i = 0; i < ON_SLOTS; i++) {
		struct slot *s = &c->slots[i];
		if (s->state == S_HANDSHAKE || s->state == S_FAILING || (s->state == S_CLIENT && s->stage <= H_INIT))
			if (s->deadline < wake)
				wake = s->deadline;
		if (s->len && s->last_progress + c->cfg.stall_timeout < wake)
			wake = s->last_progress + c->cfg.stall_timeout;
	}
	reap(c);
	return wake;
}

int on_next_action(struct on_core *c, struct on_action *a)
{
	if (!c->act_len)
		return 0;
	*a = c->act[c->act_head];
	c->act_head = (c->act_head + 1) % ACTIONS;
	c->act_len--;
	return 1;
}

/* --- set-up --------------------------------------------------------------------------------------------------- */

void on_set_key(struct on_core *c, const uint8_t key[8], int have_key)
{
	memcpy(c->key, key, 8);
	c->have_key = have_key;
}

struct on_core *on_core_new(const struct on_config *cfg)
{
	struct on_core *c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;
	c->cfg = *cfg;
	c->tw = (cfg->width + ON_TILE - 1) / ON_TILE;
	c->th = (cfg->height + ON_TILE - 1) / ON_TILE;
	c->ntiles = c->tw * c->th;
	c->input_owner = -1;
	c->cur = calloc((size_t)cfg->width * cfg->height, 4);
	/* the largest update: a header, one rectangle header per tile, and the whole frame */
	size_t cap = 4 + (size_t)c->ntiles * 12 + (size_t)cfg->width * cfg->height * 4 + 512;
	int ok = c->cur != NULL;
	for (int i = 0; i < ON_SLOTS && ok; i++) {
		c->slots[i].out = malloc(cap);
		c->slots[i].marks = calloc((size_t)c->ntiles, 1);
		c->slots[i].cap = cap;
		ok = c->slots[i].out && c->slots[i].marks;
	}
	if (!ok) {
		on_core_free(c);
		return NULL;
	}
	return c;
}

void on_core_free(struct on_core *c)
{
	if (!c)
		return;
	for (int i = 0; i < ON_SLOTS; i++) {
		free(c->slots[i].out);
		free(c->slots[i].marks);
	}
	free(c->cur);
	memset(c->key, 0, sizeof(c->key));
	free(c);
}

void on_stats(const struct on_core *c, struct on_stats *s) { *s = c->st; }

void on_client_stats(const struct on_core *c, int idx, struct on_client_stats *o)
{
	const struct slot *s = &c->slots[idx];
	static const int map[] = { 0, 1, 2, 3, 3 };
	o->state = map[s->state];
	o->requests = s->requests;
	o->updates = s->updates;
	o->sent_seq = s->sent_seq;
	o->queued = s->len;
	o->input_owner = c->input_owner == idx;
	o->marked_tiles = 0;
	if (s->state == S_CLIENT)
		for (int i = 0; i < c->ntiles; i++)
			o->marked_tiles += s->marks[i];
}
