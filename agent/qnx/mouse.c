/*
 * mouse.c: holding the target's USB mouse (ON-AG-001 phase 2; private design 7).
 *
 * While a viewer that gets the arrow is connected, serve.c takes the mouse: it connects to io-hid and attaches every
 * mouse's input report exclusively, so the system no longer moves its pointer from it, and serve.c injects every move
 * itself. When the last such viewer goes, it lets go and the mouse is the system's again.
 *
 * The second thread. libhiddi calls insertion, removal and report on a thread of its own. That thread touches only
 * what is below under `mu`: the queue, the device table, the flags and the counters; never the core, Screen, stdio or
 * the device manager. Each callback runs with cancellation disabled. serve.c's loop takes the queue with mouse_drain()
 * after a PULSE_HID. No call that talks to io-hid (connect, attach, disconnect) is made while `mu` is held; the report
 * parsers are local and are called under it.
 *
 * Which mice. Only those io-hid announces within 250 ms of the connect (the ones plugged in already), with relative X
 * and Y, one attach per device. A mouse plugged in during a hold is left to the system until the next take (private
 * design 7.3).
 *
 * Fail-safe. If a report finds the oldest queued entry over 5 s old, and still there 200 ms later, the loop is stuck
 * and the person at the target has no mouse: the process ends, which gives the mouse back (private design 7.3.5).
 */
#define _QNX_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/hiddi.h>
#include <sys/hidut.h>
#include <sys/neutrino.h>
#include "mouse.h"

#define MICE 4
#define ENUM_WINDOW 250000000LL
#define STUCK 5000000000LL
#define STUCK_CONFIRM 200000000LL

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct hidd_connection *conn; /* the loop thread's only */
static int pulse_coid, pulse_code, pulse_prio;
static on_time (*clock_now)(void);

/* under mu */
static int holding, pending;
static unsigned hold_no;          /* counts takes: an attach that began in an earlier hold is not kept */
static uint32_t left_devno[8];    /* mice left to the system in this hold, each counted once */
static int left_n;
static on_time connect_at, stuck_seen;
static struct on_mouse_queue q;
static struct {
	int used;
	uint32_t devno;
	int buttons;
	struct hidd_report *handle;       /* set once the attach has returned: a report before that is dropped */
	struct hidd_report_instance *rep;
} mice[MICE];
static struct mouse_stats st;

static int all_buttons(void)
{
	int b = 0;
	for (int i = 0; i < MICE; i++)
		if (mice[i].used)
			b |= mice[i].buttons;
	return b;
}

/* under mu: a mouse left to the system, counted once per hold however often it is announced */
static void left_to_system(uint32_t devno)
{
	for (int i = 0; i < left_n; i++)
		if (left_devno[i] == devno)
			return;
	if (left_n < 8)
		left_devno[left_n++] = devno;
	st.left_to_system++;
}

/* under mu: wake the loop once per drain */
static void wake(void)
{
	if (!pending && MsgSendPulse(pulse_coid, pulse_prio, pulse_code, 0) == 0)
		pending = 1;
}

static void stuck_check(on_time now)
{
	if (!q.n || now - q.e[0].t < STUCK) {
		stuck_seen = 0;
		return;
	}
	if (!stuck_seen) {
		stuck_seen = now;
		return;
	}
	if (now - stuck_seen >= STUCK_CONFIRM) {
		static const char msg[] = "opticnerve: the loop is stuck with the target's mouse held: exiting to give it back\n";
		ssize_t r = write(2, msg, sizeof(msg) - 1);
		(void)r;
		_exit(3);
	}
}

static void on_report(struct hidd_connection *c, struct hidd_report *handle, void *data, _Uint32t len, _Uint32t flags,
		      void *user)
{
	(void)c;
	(void)len;
	(void)user;
	int old;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
	pthread_mutex_lock(&mu);
	int m = -1;
	for (int i = 0; i < MICE && holding && m < 0; i++)
		if (mice[i].used && mice[i].handle == handle)
			m = i;
	if (m >= 0) {
		struct hidd_report_instance *rep = mice[m].rep;
		_Int32t x = 0, y = 0, w = 0;
		hidd_get_signed_usage_value(rep, NULL, HIDD_PAGE_DESKTOP, HIDD_USAGE_X, data, &x);
		hidd_get_signed_usage_value(rep, NULL, HIDD_PAGE_DESKTOP, HIDD_USAGE_Y, data, &y);
		hidd_get_signed_usage_value(rep, NULL, HIDD_PAGE_DESKTOP, HIDD_USAGE_WHEEL, data, &w);
		_Uint16t usages[16], n = 16;
		int b = 0;
		if (hidd_get_buttons(rep, NULL, HIDD_PAGE_BUTTONS, data, usages, &n) == EOK)
			for (int i = 0; i < n; i++)
				b |= on_mouse_hid_button(usages[i]);
		mice[m].buttons = b;
		on_time now = clock_now();
		stuck_check(now);
		on_mouse_add(&q, x, y, w, all_buttons(), now);
		st.reports++;
		if (flags & HIDD_REPORT_BUFFER_OVERFLOW)
			st.overflows++;
		wake();
	}
	pthread_mutex_unlock(&mu);
	pthread_setcancelstate(old, NULL);
}

/* X and Y relative: a mouse, not a tablet or a touch screen (those stay the system's). The flag is tested on
 * data_properties directly. */
static int relative_xy(struct hidd_report_instance *rep)
{
	hidd_report_props_t props[32];
	_Uint16t n = 0, len;
	if (hidd_get_num_props(rep, &n) != EOK)
		return 0;
	len = n > 32 ? 32 : n;
	if (hidd_get_report_props(rep, props, &len) != EOK)
		return 0;
	int x = 0, y = 0;
	for (int i = 0; i < len; i++) {
		if (props[i].usage_page != HIDD_PAGE_DESKTOP || !(props[i].data_properties & HIDD_DATA_RELATIVE))
			continue;
		x |= props[i].usage_min <= HIDD_USAGE_X && HIDD_USAGE_X <= props[i].usage_max;
		y |= props[i].usage_min <= HIDD_USAGE_Y && HIDD_USAGE_Y <= props[i].usage_max;
	}
	return x && y;
}

static void attach(struct hidd_connection *c, hidd_device_instance_t *inst, struct hidd_collection *coll, int depth)
{
	struct hidd_report_instance *rep;
	if (hidd_get_report_instance(coll, 0, HID_INPUT_REPORT, &rep) != EOK) {
		/* the X and Y may sit in a Pointer collection inside the Mouse one */
		struct hidd_collection **sub;
		_Uint16t n = 0;
		if (depth == 0 && hidd_get_collections(NULL, coll, &sub, &n) == EOK && n > 0)
			attach(c, inst, sub[0], 1);
		return;
	}
	if (!relative_xy(rep))
		return;
	pthread_mutex_lock(&mu);
	unsigned my_hold = hold_no;
	int m = -1, dup = 0;
	for (int i = 0; i < MICE; i++) {
		dup |= mice[i].used && mice[i].devno == inst->devno; /* announced twice: one attach */
		if (!mice[i].used && m < 0)
			m = i;
	}
	if (holding && !dup && m >= 0) {
		memset(&mice[m], 0, sizeof(mice[m]));
		mice[m].used = 1;
		mice[m].devno = inst->devno;
		mice[m].rep = rep;
	} else {
		if (holding && !dup)
			left_to_system(inst->devno); /* a fifth mouse */
		m = -1;
	}
	pthread_mutex_unlock(&mu);
	if (m < 0)
		return;
	struct hidd_report *handle;
	int rc = hidd_report_attach(c, inst, rep, HIDD_REPORT_EXCLUSIVE, 0, &handle);
	pthread_mutex_lock(&mu);
	if (rc != EOK || !holding || hold_no != my_hold || !mice[m].used) { /* refused, or let go meanwhile */
		mice[m].used = 0;
		if (rc != EOK)
			st.attach_failures++;
	} else {
		mice[m].handle = handle;
		st.devices++;
	}
	pthread_mutex_unlock(&mu);
}

static void on_insertion(struct hidd_connection *c, hidd_device_instance_t *inst)
{
	int old;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
	pthread_mutex_lock(&mu);
	int take = holding && clock_now() - connect_at <= ENUM_WINDOW;
	pthread_mutex_unlock(&mu);
	struct hidd_collection **colls;
	_Uint16t n = 0;
	if (hidd_get_collections(inst, NULL, &colls, &n) == EOK)
		for (int i = 0; i < n; i++) {
			_Uint16t page = 0, usage = 0;
			hidd_collection_usage(colls[i], &page, &usage);
			if (page != HIDD_PAGE_DESKTOP || usage != HIDD_USAGE_MOUSE)
				continue;
			if (take) {
				attach(c, inst, colls[i], 0);
			} else {
				pthread_mutex_lock(&mu);
				if (holding)
					left_to_system(inst->devno);
				pthread_mutex_unlock(&mu);
			}
		}
	pthread_setcancelstate(old, NULL);
}

/* A held mouse unplugged: what it held is let go (queued, so it is injected in order). */
static void on_removal(struct hidd_connection *c, hidd_device_instance_t *inst)
{
	(void)c;
	int old;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
	pthread_mutex_lock(&mu);
	for (int i = 0; i < MICE; i++)
		if (mice[i].used && mice[i].devno == inst->devno) {
			mice[i].used = 0;
			if (mice[i].handle)
				st.devices--;
			if (holding && mice[i].buttons) {
				mice[i].buttons = 0;
				on_mouse_add(&q, 0, 0, 0, all_buttons(), clock_now());
				wake();
			}
		}
	pthread_mutex_unlock(&mu);
	pthread_setcancelstate(old, NULL);
}

static hidd_funcs_t funcs = { _HIDDI_NFUNCS, on_insertion, on_removal, on_report, NULL };
static hidd_device_ident_t any = { HIDD_CONNECT_WILDCARD, HIDD_CONNECT_WILDCARD, HIDD_CONNECT_WILDCARD };

void mouse_init(int coid, int code, int prio, on_time (*now)(void))
{
	pulse_coid = coid;
	pulse_code = code;
	pulse_prio = prio;
	clock_now = now;
}

int mouse_take(void)
{
	if (conn)
		return 0;
	pthread_mutex_lock(&mu);
	holding = 1;
	hold_no++;
	left_n = 0;
	pending = 0;
	stuck_seen = 0;
	on_mouse_queue_reset(&q);
	memset(mice, 0, sizeof(mice));
	st.devices = 0;
	connect_at = clock_now();
	pthread_mutex_unlock(&mu);
	/* libhiddi's thread inherits this mask: SIGTERM, SIGINT and SIGHUP stay with the loop thread */
	sigset_t block, was;
	sigemptyset(&block);
	sigaddset(&block, SIGTERM);
	sigaddset(&block, SIGINT);
	sigaddset(&block, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &block, &was);
	/* connect_wait 0: io-hid answers at once or is not there (private design 7.3) */
	hidd_connect_parm_t parm = { NULL, HID_VERSION, HIDD_VERSION, 0, 0, &any, &funcs, 0, 0, { 0 } };
	int rc = hidd_connect(&parm, &conn);
	pthread_sigmask(SIG_SETMASK, &was, NULL);
	pthread_mutex_lock(&mu);
	if (rc != EOK) {
		conn = NULL;
		holding = 0;
		st.take_failures++;
	} else {
		st.takes++;
	}
	st.holding = holding;
	pthread_mutex_unlock(&mu);
	return rc;
}

void mouse_let_go(void)
{
	if (!conn)
		return;
	pthread_mutex_lock(&mu);
	holding = 0; /* a callback still in flight finds this and does nothing */
	pthread_mutex_unlock(&mu);
	hidd_disconnect(conn);
	conn = NULL;
	pthread_mutex_lock(&mu);
	on_mouse_queue_reset(&q);
	memset(mice, 0, sizeof(mice));
	pending = 0;
	st.devices = 0;
	st.holding = 0;
	st.let_gos++;
	pthread_mutex_unlock(&mu);
}

void mouse_abandon(void)
{
	if (!conn)
		return;
	pthread_mutex_lock(&mu);
	holding = 0;
	pthread_mutex_unlock(&mu);
	hidd_disconnect(conn);
	conn = NULL;
}

int mouse_holding(void)
{
	return conn != NULL;
}

int mouse_drain(struct on_mouse_entry *out, int max)
{
	pthread_mutex_lock(&mu);
	int n = q.n < max ? q.n : max;
	memcpy(out, q.e, sizeof(*out) * (size_t)n);
	memmove(q.e, q.e + n, sizeof(*out) * (size_t)(q.n - n));
	q.n -= n;
	if (!q.n)
		pending = 0;
	stuck_seen = 0;
	pthread_mutex_unlock(&mu);
	return n;
}

void mouse_stats(struct mouse_stats *s)
{
	pthread_mutex_lock(&mu);
	*s = st;
	s->overflows += q.overflows;
	pthread_mutex_unlock(&mu);
}
