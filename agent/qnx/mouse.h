/* mouse.h: holding the target's USB mouse (mouse.c; ON-AG-001 phase 2, private design 7). */
#ifndef MOUSE_H
#define MOUSE_H

#include "on_core.h"
#include "on_mouse.h"

/* coid and code: where libhiddi's thread sends its wake-up pulse; prio: the loop thread's priority. now: the
 * loop's clock. */
void mouse_init(int coid, int code, int prio, on_time (*now)(void));

int mouse_take(void);     /* returns 0, or an errno: then the mouse stays the system's */
void mouse_let_go(void);  /* gives the mouse back; the caller injects the release of what it held */
void mouse_abandon(void); /* from die(): give it back without touching anything else */
int mouse_holding(void);  /* 1 between a take and a let-go */

/* Takes everything queued, at most max entries (the queue's size), and allows the next wake-up. */
int mouse_drain(struct on_mouse_entry *out, int max);

struct mouse_stats {
	int holding, devices;  /* devices: mice attached in this hold */
	uint64_t takes, let_gos, take_failures, attach_failures, reports, overflows;
	uint64_t left_to_system; /* mice announced during a hold (or a fifth one): left to the system */
};
void mouse_stats(struct mouse_stats *s);

#endif
