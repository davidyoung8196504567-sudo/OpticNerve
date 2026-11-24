/* device.h: /dev/opticnerve inside the agent (device.c). */
#ifndef DEVICE_H
#define DEVICE_H

#include <sys/dispatch.h>
#include "on_core.h"

struct device_info {
	const char *version;
	int width, height, port;
	on_time (*now)(void);
	void (*display_state)(int *idle, int *power); /* read from Screen on request */
	int (*mouse_switch)(int set);                 /* the mouse switch: set 1 on, 0 off, -1 leave it; returns it */
	void (*mouse_text)(char *b, size_t cap);      /* the mouse lines of the status text */
};

void device_init(dispatch_t *dpp, struct on_core *core, const struct device_info *info);
void device_signal(on_time now);   /* a composition was signalled (after on_signalled) */
on_time device_pass(on_time now);  /* answers waiters that are due; returns its next deadline */

#endif
