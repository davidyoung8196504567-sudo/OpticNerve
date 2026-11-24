/*
 * on_mouse.h: the target's own USB mouse while the agent holds it (ON-AG-001 phase 2), the part with no threads and
 * no QNX header, so the tests run it anywhere.
 *
 * While a viewer that gets the arrow is connected, the agent takes the USB mouse from the system (agent/qnx/mouse.c)
 * and injects every move itself, so the arrow follows the mouse as well as the viewer. Reports arrive on another
 * thread; they are kept here in a short queue until the agent's loop injects them:
 *   - motion joins the last entry only if that entry is motion alone with the same buttons, so a press and a release
 *     land where they happened, however late the loop drains;
 *   - a full queue folds into its last entry (motion and wheel added, the newest buttons) and counts an overflow.
 * The position is kept in half pixels: one count of the mouse moves the pointer 1.5 pixels, with no acceleration.
 * Buttons are the RFB mask (1 left, 2 middle, 4 right), as a VNC client sends them.
 */
#ifndef ON_MOUSE_H
#define ON_MOUSE_H

#include <stdint.h>
#include "on_core.h"

#define ON_MOUSE_QUEUE 64

struct on_mouse_entry {
	int32_t dx, dy, wheel; /* counts; wheel: + is a notch up, as a HID mouse reports it */
	int buttons;           /* RFB mask after this entry */
	int motion_only;       /* no button change and no wheel: later motion with the same buttons may join it */
	on_time t;             /* when its first report arrived */
};

struct on_mouse_queue {
	struct on_mouse_entry e[ON_MOUSE_QUEUE];
	int n;
	int buttons;        /* the buttons of the last report queued */
	uint64_t overflows;
};

void on_mouse_queue_reset(struct on_mouse_queue *q);
void on_mouse_add(struct on_mouse_queue *q, int dx, int dy, int wheel, int buttons, on_time t);

/* HID button usages (1 left, 2 right, 3 middle) to the RFB mask. */
int on_mouse_hid_button(int usage);

struct on_mouse_pos {
	int width, height;
	int known;
	int64_t hx, hy; /* half pixels */
};

/* A position from elsewhere (a VNC client's move): the mouse carries on from there. */
void on_mouse_set(struct on_mouse_pos *p, int x, int y);
/* Moves by an entry's counts, clamped to the screen; until a position is known the mouse starts at the middle.
 * Returns the position in pixels. */
void on_mouse_move(struct on_mouse_pos *p, int32_t dx, int32_t dy, int *x, int *y);

/* When to hold the mouse: the driver calls on_mouse_level() once a round with want = (the switch is on, and a client
 * gets the arrow) and acts on what it returns. A take waits until want has held for ON_MOUSE_DWELL; a take that
 * failed is not tried again until want has dropped; a let-go waits, at most ON_MOUSE_LETGO_WAIT, for the buttons the
 * held mouse has down (usb_held) to come up, unless failed (injections are refused) is set. *wake: when to call again
 * with nothing else happening (ON_NEVER for no need). */
#define ON_MOUSE_DWELL 1000000000LL
#define ON_MOUSE_LETGO_WAIT 5000000000LL
enum { ON_MOUSE_NONE, ON_MOUSE_TAKE, ON_MOUSE_LET_GO };
struct on_mouse_level {
	on_time want_since, letgo_since; /* 0: not running */
	int take_blocked;
};
int on_mouse_level(struct on_mouse_level *l, on_time now, int want, int holding, int usb_held, int failed,
		   on_time *wake);
void on_mouse_take_failed(struct on_mouse_level *l);

/* One pointer event from both sources: a VNC client's mask (bits 8 and 16 are its wheel, never kept as held) and the
 * buttons the held mouse has down. Returns the RFB buttons to inject; *wheel: the VNC wheel as notches (-1 up). */
int on_mouse_compose(int vnc_mask, int usb_held, int *wheel);
/* The same for an event of the held mouse: the buttons a VNC client holds (low three bits) stay down, and its wheel
 * (+ a notch up, as HID reports it) becomes notches as the driver injects them (-1 up). */
int on_mouse_compose_usb(int vnc_buttons, const struct on_mouse_entry *e, int *wheel);

#endif
