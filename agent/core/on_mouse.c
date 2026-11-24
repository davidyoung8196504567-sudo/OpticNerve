/* on_mouse.c: see on_mouse.h. */
#include "on_mouse.h"

void on_mouse_queue_reset(struct on_mouse_queue *q)
{
	q->n = 0;
	q->buttons = 0;
}

void on_mouse_add(struct on_mouse_queue *q, int dx, int dy, int wheel, int buttons, on_time t)
{
	int motion_only = buttons == q->buttons && wheel == 0;
	struct on_mouse_entry *last = q->n ? &q->e[q->n - 1] : NULL;
	q->buttons = buttons;
	if (last && motion_only && last->motion_only && last->buttons == buttons) {
		last->dx += dx;
		last->dy += dy;
		return;
	}
	if (q->n == ON_MOUSE_QUEUE) {
		q->overflows++;
		last->dx += dx;
		last->dy += dy;
		last->wheel += wheel;
		last->buttons = buttons;
		last->motion_only = 0;
		return;
	}
	q->e[q->n++] = (struct on_mouse_entry){ dx, dy, wheel, buttons, motion_only, t };
}

int on_mouse_hid_button(int usage)
{
	return usage == 1 ? 1 : usage == 2 ? 4 : usage == 3 ? 2 : 0;
}

void on_mouse_set(struct on_mouse_pos *p, int x, int y)
{
	p->known = 1;
	p->hx = (int64_t)x * 2;
	p->hy = (int64_t)y * 2;
}

void on_mouse_move(struct on_mouse_pos *p, int32_t dx, int32_t dy, int *x, int *y)
{
	if (!p->known)
		on_mouse_set(p, p->width / 2, p->height / 2);
	p->hx += (int64_t)dx * 3;
	p->hy += (int64_t)dy * 3;
	int64_t mx = (int64_t)(p->width - 1) * 2 + 1, my = (int64_t)(p->height - 1) * 2 + 1;
	p->hx = p->hx < 0 ? 0 : p->hx > mx ? mx : p->hx;
	p->hy = p->hy < 0 ? 0 : p->hy > my ? my : p->hy;
	*x = (int)(p->hx / 2);
	*y = (int)(p->hy / 2);
}

int on_mouse_level(struct on_mouse_level *l, on_time now, int want, int holding, int usb_held, int failed,
		   on_time *wake)
{
	*wake = ON_NEVER;
	if (want && !failed) {
		l->letgo_since = 0;
		if (holding || l->take_blocked)
			return ON_MOUSE_NONE;
		if (!l->want_since)
			l->want_since = now;
		if (now - l->want_since < ON_MOUSE_DWELL) {
			*wake = l->want_since + ON_MOUSE_DWELL;
			return ON_MOUSE_NONE;
		}
		return ON_MOUSE_TAKE;
	}
	l->want_since = 0;
	l->take_blocked = 0;
	if (!holding)
		return ON_MOUSE_NONE;
	if (usb_held && !failed) { /* a let-go with a button down would leave a press where nobody clicked */
		if (!l->letgo_since)
			l->letgo_since = now;
		if (now - l->letgo_since < ON_MOUSE_LETGO_WAIT) {
			*wake = l->letgo_since + ON_MOUSE_LETGO_WAIT;
			return ON_MOUSE_NONE;
		}
	}
	l->letgo_since = 0;
	return ON_MOUSE_LET_GO;
}

void on_mouse_take_failed(struct on_mouse_level *l)
{
	l->take_blocked = 1;
}

int on_mouse_compose(int vnc_mask, int usb_held, int *wheel)
{
	*wheel = (vnc_mask & 8) ? -1 : (vnc_mask & 16) ? 1 : 0;
	return (vnc_mask & 7) | usb_held;
}

int on_mouse_compose_usb(int vnc_buttons, const struct on_mouse_entry *e, int *wheel)
{
	*wheel = -e->wheel;
	return (vnc_buttons & 7) | e->buttons;
}
