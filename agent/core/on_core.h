/*
 * on_core.h: the OpticNerve core, the part of the agent that decides everything.
 *
 * The core speaks RFB 3.8 to its clients, keeps the client table, decides when to capture and what to send, and
 * says what to inject. It does no input or output, reads no clock and includes no QNX header, so the same code runs
 * on the target (driven by agent/qnx/) and on a host (driven by the tests and the fake server).
 *
 * How a driver uses it:
 *   - It tells the core what happened: a connection was accepted, bytes arrived, bytes were sent, a slot closed,
 *     Screen signalled a composition, a capture finished. Every call carries the time, from a monotonic clock.
 *   - After handling everything that has arrived, it calls on_schedule() once. That is the only place where the core
 *     decides captures and updates.
 *   - It then takes the core's actions with on_next_action() until there are none, and sends each slot's queued
 *     output with on_output() and on_sent().
 *   - It sleeps until the time on_schedule() returned, or until the next event.
 *
 * One thread only. No function here blocks or allocates after on_core_new(), except on_core_new() itself.
 */
#ifndef ON_CORE_H
#define ON_CORE_H

#include <stddef.h>
#include <stdint.h>

#define ON_TILE 64             /* tile edge, pixels */
#define ON_CLIENT_SLOTS 4      /* clients that have logged in */
#define ON_HANDSHAKE_SLOTS 2   /* connections that have not logged in yet */
#define ON_SLOTS (ON_CLIENT_SLOTS + ON_HANDSHAKE_SLOTS)
#define ON_HELD_KEYS 32        /* keys one client may hold down at once */

typedef int64_t on_time; /* nanoseconds of a monotonic clock; the driver's choice of origin */
#define ON_NEVER INT64_MAX

struct on_core;

struct on_config {
	int width, height;          /* framebuffer size, pixels */
	const char *desktop_name;   /* sent in ServerInit, ASCII */
	on_time handshake_timeout;  /* I6: a connection that has not logged in by then is closed */
	on_time stall_timeout;      /* I6: a client that reads nothing for this long while output is queued is closed */
	on_time holdoff_min;        /* 4.5: the least gap between two captures (never less than the last capture took) */
	on_time timed_capture;      /* 4.5: a capture when an incremental request has waited this long */
};

/* The VNC password, already reduced to the 8 bytes VncAuth uses. have_key 0 means: refuse every login. */
void on_set_key(struct on_core *c, const uint8_t key[8], int have_key);

struct on_core *on_core_new(const struct on_config *cfg);
void on_core_free(struct on_core *c);

/* A connection was accepted. challenge: 16 random bytes for VncAuth, from a source that does not block.
 * Returns the slot, or -1 if both handshake slots are taken: the driver then closes the connection at once. */
int on_accept(struct on_core *c, const uint8_t challenge[16], on_time now);

/* Bytes arrived on a slot. The core consumes all of them (it keeps no more than a message's fixed part). */
void on_input(struct on_core *c, int slot, const uint8_t *data, size_t len, on_time now);

/* The slot's queued output: on_output() gives the next contiguous bytes, on_sent() says how many were sent. */
size_t on_output(struct on_core *c, int slot, const uint8_t **data);
void on_sent(struct on_core *c, int slot, size_t n, on_time now);

/* The peer closed, or the connection failed. The core frees the slot in its next on_schedule(). */
void on_closed(struct on_core *c, int slot, on_time now);

/* Screen signalled a composition on the display. */
void on_signalled(struct on_core *c, on_time now);

/* A capture the core asked for has finished. pixels: the whole display, 32 bits per pixel, bytes B, G, R, X in
 * memory, stride in bytes. started/ended: when the read began and ended. The core copies what it needs. */
void on_captured(struct on_core *c, const uint8_t *pixels, int stride, on_time started, on_time ended);

/* A caller on the device needs fresh pixels (design 4.5, consumer 3). Returns the value that on_stats().captures
 * will reach once a capture taken after this call has finished; the caller is answered then. */
uint64_t on_want_capture(struct on_core *c);

/* Reap, decide, reap. Returns when the core next needs to be called with nothing else having happened. */
on_time on_schedule(struct on_core *c, on_time now);

enum on_action_type {
	ON_ACT_NONE,
	ON_ACT_CAPTURE, /* read the display, then call on_captured() */
	ON_ACT_CLOSE,   /* close the slot's connection (the core has already freed the slot) */
	ON_ACT_SHUTDOWN_SEND, /* stop sending on the slot, keep reading: a failure reason is on its way (4.7) */
	ON_ACT_KEY,     /* inject a key: keysym, down */
	ON_ACT_POINTER, /* inject a pointer event: x, y, buttons (RFB mask) */
};

struct on_action {
	enum on_action_type type;
	int slot;
	uint32_t keysym;
	int down;
	int x, y, buttons;
};

/* Takes the next action. Returns 0 when there is none. */
int on_next_action(struct on_core *c, struct on_action *a);

/* Status counters, for the status text and the tests. */
struct on_stats {
	uint64_t signals, captures, timed_captures, updates_from_timed_capture;
	uint64_t content;          /* content number of the last capture that changed something */
	uint64_t logins_failed, refused_at_accept, server_full;
	uint64_t cut_text_skipped, extended_cut_text, keys_dropped;
	int signalled;             /* a composition was signalled since the last capture started */
};
struct on_client_stats {
	int state;                 /* 0 free, 1 handshake, 2 client, 3 closing */
	uint64_t requests, updates, sent_seq;
	int marked_tiles;
	size_t queued;
};
void on_stats(const struct on_core *c, struct on_stats *s);
void on_client_stats(const struct on_core *c, int slot, struct on_client_stats *s);

#endif
