/* on_dev.h: the requests of /dev/opticnerve, shared by the agent and the subcommands (private design 4.10). */
#ifndef ON_DEV_H
#define ON_DEV_H

#include <stdint.h>
#include <devctl.h>

#define ON_DEV_PATH "/dev/opticnerve"

/* wait: blocks until a composition is signalled, the screen has been quiet for quiet_ms (with since >= 0: quiet after a
 * change past since), or timeout_ms passes. */
struct on_dev_wait {
	int64_t since;      /* in: return at once if the composition count is already greater; -1 for none */
	int32_t timeout_ms; /* in */
	int32_t quiet_ms;   /* in: 0 for none */
	uint64_t count;     /* out: the composition count */
	int32_t result;     /* out: ON_WAIT_CHANGE, ON_WAIT_QUIET or ON_WAIT_TIMEOUT */
	int32_t pad;
};
enum { ON_WAIT_CHANGE = 0, ON_WAIT_QUIET = 1, ON_WAIT_TIMEOUT = 2 };

/* current: the agent captures, compares, and then answers for each client slot. */
struct on_dev_current {
	uint64_t content;   /* out: content number after the capture */
	int32_t state[6];   /* out: per slot, 0 none, 1 current, 2 not current */
	int32_t marked[6];  /* out: tiles not yet sent */
	uint32_t queued[6]; /* out: bytes queued */
};

/* snapshot: the agent captures, then keeps the region for this open file; read() then returns its RGB bytes once.
 * At most two open files hold a snapshot at a time. */
struct on_dev_snapshot {
	int32_t x, y, w, h; /* in: the region; out: w and h after clipping to the screen */
	uint64_t content;   /* out: content number of the capture */
	uint32_t bytes;     /* out: bytes that read() will return (w * h * 3) */
	int32_t pad;
};

#define ON_DCMD_WAIT __DIOTF(_DCMD_MISC, 0x40, struct on_dev_wait)
#define ON_DCMD_SNAPSHOT __DIOTF(_DCMD_MISC, 0x42, struct on_dev_snapshot)
#define ON_DCMD_CURRENT __DIOTF(_DCMD_MISC, 0x41, struct on_dev_current)

#endif
