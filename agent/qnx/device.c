/*
 * device.c: /dev/opticnerve, the agent's resource manager (private design 4.10). It runs on the same dispatch loop
 * as everything else. read() gives the status text; devctl "wait" and "current" are answered later, with a deferred
 * reply, from device_pass(), which the loop runs after every event.
 *
 * Every caller may use the device (mode 666). Reading is open to all; setting the pointer arrow or the mouse switch
 * needs root, since both change what a person at the target gets. Stopping the agent and reloading its password are
 * signals, which the system allows to root only (design 4.11).
 */
#define _QNX_SOURCE
struct _iofunc_ocb;
#define RESMGR_OCB_T struct _iofunc_ocb /* the handlers take the iofunc OCB, as the iofunc helpers do */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include "on_core.h"
#include "on_dev.h"
#include "device.h"

#define WAITERS 16
#define TEXTS 8
#define TEXT_MAX 4096

static resmgr_connect_funcs_t cfuncs;
static resmgr_io_funcs_t iofuncs;
static iofunc_attr_t attr;
static struct on_core *core;
static const struct device_info *info;
static on_time last_signal_time;
static uint64_t last_signal_count;

static struct waiter {
	int used, current; /* current: 1 a "current" request, 2 a snapshot, each waiting for a capture */
	rcvid_t rcvid;
	void *ocb;
	struct on_dev_wait w;
	struct on_dev_snapshot s;
	uint64_t start_count, capture_target;
	on_time deadline;
} waiters[WAITERS];

static struct text {
	void *ocb;
	size_t len;
	char buf[TEXT_MAX];
} texts[TEXTS];

/* Snapshots waiting to be read, one per open file, at most SNAPS at a time (design 4.10). */
#define SNAPS 2
static struct snap {
	void *ocb;
	uint8_t *buf;
	size_t len, off;
} snaps[SNAPS];

static struct snap *snap_for(void *ocb)
{
	for (int i = 0; i < SNAPS; i++)
		if (snaps[i].ocb == ocb)
			return &snaps[i];
	return NULL;
}

static void snap_free(struct snap *s)
{
	free(s->buf);
	memset(s, 0, sizeof(*s));
}

static struct text *text_for(void *ocb, int create)
{
	struct text *free_slot = NULL;
	for (int i = 0; i < TEXTS; i++) {
		if (texts[i].ocb == ocb)
			return &texts[i];
		if (!texts[i].ocb && !free_slot)
			free_slot = &texts[i];
	}
	if (create && free_slot)
		free_slot->ocb = ocb;
	return create ? free_slot : NULL;
}

static const char *state_name(int s)
{
	static const char *n[] = { "free", "handshake", "client", "closing" };
	return s >= 0 && s < 4 ? n[s] : "?";
}

static size_t status_text(char *b, size_t cap)
{
	struct on_stats st;
	on_stats(core, &st);
	int nw = 0, nt = 0;
	for (int i = 0; i < WAITERS; i++)
		nw += waiters[i].used;
	for (int i = 0; i < TEXTS; i++)
		nt += texts[i].ocb != NULL;
	int idle = -1, power = -1;
	if (info->display_state)
		info->display_state(&idle, &power);
	char mouse[512] = "";
	if (info->mouse_text)
		info->mouse_text(mouse, sizeof(mouse));
	size_t n = (size_t)snprintf(b, cap,
		"version %s\npid %d\ndisplay %dx%d\nport %d\n"
		"compositions %llu\nsignalled %d\ncaptures %llu\ntimed_captures %llu\nupdates_from_timed_capture %llu\n"
		"content %llu\nidle_state %d\npower_mode %d\nlogins_failed %llu\nlogins_timed_out %llu\n"
		"refused_at_accept %llu\nserver_full %llu\n"
		"cut_text_skipped %llu\nextended_cut_text %llu\nkeys_dropped %llu\npointer_dropped %llu\n"
		"input_handovers %llu\nwaiters %d\nstatus_readers %d\npointer %s\n%s",
		info->version, (int)getpid(), info->width, info->height, info->port, (unsigned long long)st.signals,
		st.signalled, (unsigned long long)st.captures, (unsigned long long)st.timed_captures,
		(unsigned long long)st.updates_from_timed_capture, (unsigned long long)st.content, idle, power,
		(unsigned long long)st.logins_failed, (unsigned long long)st.logins_timed_out,
		(unsigned long long)st.refused_at_accept,
		(unsigned long long)st.server_full, (unsigned long long)st.cut_text_skipped,
		(unsigned long long)st.extended_cut_text, (unsigned long long)st.keys_dropped,
		(unsigned long long)st.pointer_dropped, (unsigned long long)st.input_handovers, nw, nt,
		on_pointer(core) ? "on" : "off", mouse);
	for (int i = 0; i < ON_SLOTS && n < cap; i++) {
		struct on_client_stats cs;
		on_client_stats(core, i, &cs);
		if (!cs.state)
			continue;
		n += (size_t)snprintf(b + n, cap - n,
			"slot %d %s requests %llu updates %llu sent_seq %llu marked_tiles %d queued %zu pointer_encodings %d%s%s\n",
			i, state_name(cs.state), (unsigned long long)cs.requests, (unsigned long long)cs.updates,
			(unsigned long long)cs.sent_seq, cs.marked_tiles, cs.queued, cs.pointer_encodings,
			cs.input_owner ? " input_owner" : "", cs.gets_arrow ? " arrow" : "");
	}
	return n < cap ? n : cap - 1;
}

static int dev_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb)
{
	int nonblock, rc = iofunc_read_verify(ctp, msg, ocb, &nonblock);
	if (rc != EOK)
		return rc;
	struct snap *sn = snap_for(ocb);
	if (sn && sn->buf) { /* after a snapshot, read() returns its bytes once */
		size_t n = sn->len - sn->off;
		if (n > msg->i.nbytes)
			n = msg->i.nbytes;
		_IO_SET_READ_NBYTES(ctp, n);
		MsgReply(ctp->rcvid, (long)n, sn->buf + sn->off, n);
		sn->off += n;
		if (sn->off >= sn->len)
			snap_free(sn);
		return _RESMGR_NOREPLY;
	}
	struct text *t = text_for(ocb, 1);
	if (!t)
		return EBUSY;
	if (ocb->offset == 0 || !t->len) /* made once at offset 0, so a reader in pieces gets one consistent text */
		t->len = status_text(t->buf, sizeof(t->buf));
	size_t off = (size_t)ocb->offset, n = 0;
	if (off < t->len) {
		n = t->len - off;
		if (n > msg->i.nbytes)
			n = msg->i.nbytes;
	}
	ocb->offset += n;
	_IO_SET_READ_NBYTES(ctp, n);
	MsgReply(ctp->rcvid, (long)n, t->buf + off, n);
	return _RESMGR_NOREPLY;
}

static void reply_wait(struct waiter *w, uint64_t count, int result)
{
	struct {
		struct _io_devctl_reply hdr;
		struct on_dev_wait w;
	} r;
	memset(&r, 0, sizeof(r));
	r.hdr.nbytes = sizeof(r.w);
	r.w = w->w;
	r.w.count = count;
	r.w.result = result;
	MsgReply(w->rcvid, EOK, &r, sizeof(r));
	w->used = 0;
}

static void reply_current(struct waiter *w)
{
	struct {
		struct _io_devctl_reply hdr;
		struct on_dev_current c;
	} r;
	memset(&r, 0, sizeof(r));
	r.hdr.nbytes = sizeof(r.c);
	struct on_stats st;
	on_stats(core, &st);
	r.c.content = st.content;
	for (int i = 0; i < ON_SLOTS && i < 6; i++) {
		struct on_client_stats cs;
		on_client_stats(core, i, &cs);
		if (cs.state != 2)
			continue;
		r.c.marked[i] = cs.marked_tiles;
		r.c.queued[i] = (uint32_t)cs.queued;
		r.c.state[i] = !st.signalled && cs.marked_tiles == 0 && cs.queued == 0 ? 1 : 2;
	}
	MsgReply(w->rcvid, EOK, &r, sizeof(r));
	w->used = 0;
}

static void reply_snapshot(struct waiter *w)
{
	struct {
		struct _io_devctl_reply hdr;
		struct on_dev_snapshot s;
	} r;
	memset(&r, 0, sizeof(r));
	r.hdr.nbytes = sizeof(r.s);
	r.s = w->s;
	struct snap *sn = snap_for(w->ocb);
	struct on_stats st;
	on_stats(core, &st);
	int width = w->s.w, height = w->s.h;
	size_t cap = (size_t)info->width * info->height * 3;
	uint8_t *buf = sn ? malloc(cap) : NULL;
	size_t n = buf ? on_read_region(core, w->s.x, w->s.y, &width, &height, buf, cap) : 0;
	if (!sn || !buf || !n) {
		free(buf);
		if (sn)
			snap_free(sn);
		MsgError(w->rcvid, !sn || buf ? EINVAL : ENOMEM);
		w->used = 0;
		return;
	}
	sn->buf = buf;
	sn->len = n;
	sn->off = 0;
	r.s.w = width;
	r.s.h = height;
	r.s.content = st.content;
	r.s.bytes = (uint32_t)n;
	MsgReply(w->rcvid, EOK, &r, sizeof(r));
	w->used = 0;
}

static int caller_is_root(resmgr_context_t *ctp)
{
	struct _client_info *ci = NULL;
	if (iofunc_client_info_ext(ctp, 0, &ci, 0) != EOK || !ci)
		return 0;
	int root = ci->cred.euid == 0;
	iofunc_client_info_ext_free(&ci);
	return root;
}

static int dev_devctl(resmgr_context_t *ctp, io_devctl_t *msg, iofunc_ocb_t *ocb)
{
	int rc = iofunc_devctl_default(ctp, msg, ocb);
	if (rc != _RESMGR_DEFAULT)
		return rc;
	if (msg->i.dcmd == ON_DCMD_POINTER || msg->i.dcmd == ON_DCMD_MOUSE) {
		/* answered at once; the loop's next pass sends what changes, and takes or gives back the mouse */
		if (msg->i.nbytes < sizeof(struct on_dev_pointer)) /* the same layout as struct on_dev_mouse */
			return EINVAL;
		int32_t set = ((struct on_dev_pointer *)_DEVCTL_DATA(msg->i))->set; /* read before the reply overwrites it */
		if ((set == 0 || set == 1) && !caller_is_root(ctp))
			return EPERM;
		int on;
		if (msg->i.dcmd == ON_DCMD_POINTER) {
			if (set == 0 || set == 1)
				on_set_pointer(core, set);
			on = on_pointer(core);
		} else {
			on = info->mouse_switch ? info->mouse_switch(set) : 0;
		}
		struct on_dev_pointer out = { set, on };
		memset(&msg->o, 0, sizeof(msg->o));
		msg->o.nbytes = sizeof(out);
		memcpy(_DEVCTL_DATA(msg->o), &out, sizeof(out));
		return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + sizeof(out));
	}
	struct waiter *w = NULL;
	for (int i = 0; i < WAITERS && !w; i++)
		if (!waiters[i].used)
			w = &waiters[i];
	if (msg->i.dcmd != ON_DCMD_WAIT && msg->i.dcmd != ON_DCMD_CURRENT && msg->i.dcmd != ON_DCMD_SNAPSHOT)
		return ENOSYS;
	if (!w)
		return EAGAIN; /* the table is full: an error of its own, not a timeout */
	memset(w, 0, sizeof(*w));
	w->rcvid = ctp->rcvid;
	w->ocb = ocb;
	on_time now = info->now();
	struct on_stats st;
	on_stats(core, &st);
	if (msg->i.dcmd == ON_DCMD_WAIT) {
		if (msg->i.nbytes < sizeof(struct on_dev_wait))
			return EINVAL;
		w->w = *(struct on_dev_wait *)_DEVCTL_DATA(msg->i);
		if (w->w.timeout_ms < 0 || w->w.quiet_ms < 0)
			return EINVAL;
		w->start_count = w->w.since >= 0 ? (uint64_t)w->w.since : st.signals;
		w->deadline = now + (on_time)w->w.timeout_ms * 1000000;
	} else if (msg->i.dcmd == ON_DCMD_SNAPSHOT) {
		if (msg->i.nbytes < sizeof(struct on_dev_snapshot))
			return EINVAL;
		w->s = *(struct on_dev_snapshot *)_DEVCTL_DATA(msg->i);
		struct snap *sn = snap_for(ocb);
		if (sn)
			snap_free(sn); /* a new snapshot replaces one this open file has not read */
		sn = snap_for(NULL);
		if (!sn)
			return EMFILE; /* two snapshots are held already: an error of its own */
		sn->ocb = ocb;
		w->current = 2;
		w->capture_target = on_want_capture(core);
		w->deadline = now + 5000000000LL;
	} else {
		w->current = 1;
		w->capture_target = on_want_capture(core);
		w->deadline = now + 5000000000LL;
	}
	w->used = 1;
	return _RESMGR_NOREPLY; /* answered from device_pass() */
}

/* A caller that is interrupted or killed while it waits: drop its waiter. */
static int dev_unblock(resmgr_context_t *ctp, io_pulse_t *msg, iofunc_ocb_t *ocb)
{
	(void)msg;
	(void)ocb;
	for (int i = 0; i < WAITERS; i++)
		if (waiters[i].used && waiters[i].rcvid == ctp->rcvid) {
			waiters[i].used = 0;
			MsgError(ctp->rcvid, EINTR);
		}
	return _RESMGR_NOREPLY;
}

static int dev_close_ocb(resmgr_context_t *ctp, void *reserved, iofunc_ocb_t *ocb)
{
	struct text *t = text_for(ocb, 0);
	if (t)
		memset(t, 0, sizeof(*t));
	struct snap *sn = snap_for(ocb);
	if (sn)
		snap_free(sn);
	for (int i = 0; i < WAITERS; i++)
		if (waiters[i].used && waiters[i].ocb == ocb)
			waiters[i].used = 0;
	return iofunc_close_ocb_default(ctp, reserved, ocb);
}

void device_signal(on_time now)
{
	last_signal_time = now;
	struct on_stats st;
	on_stats(core, &st);
	last_signal_count = st.signals;
}

on_time device_pass(on_time now)
{
	struct on_stats st;
	on_stats(core, &st);
	on_time wake = ON_NEVER;
	for (int i = 0; i < WAITERS; i++) {
		struct waiter *w = &waiters[i];
		if (!w->used)
			continue;
		if (w->current) {
			if (st.captures >= w->capture_target) {
				if (w->current == 2)
					reply_snapshot(w);
				else
					reply_current(w);
			} else if (now >= w->deadline) {
				struct snap *sn = w->current == 2 ? snap_for(w->ocb) : NULL;
				if (sn)
					snap_free(sn);
				MsgError(w->rcvid, ETIMEDOUT);
				w->used = 0;
			}
			continue;
		}
		on_time quiet_at = ON_NEVER;
		if (w->w.quiet_ms) {
			on_time since = last_signal_time > 0 ? last_signal_time : 0;
			quiet_at = since + (on_time)w->w.quiet_ms * 1000000;
		}
		if (!w->w.quiet_ms && st.signals > w->start_count)
			reply_wait(w, st.signals, ON_WAIT_CHANGE);
		else if (w->w.quiet_ms && (w->w.since < 0 || st.signals > w->start_count) && now >= quiet_at)
			/* with -since, quiet counts only after a change past it, so a wait started before the screen
			 * responds cannot return on the stillness before the response */
			reply_wait(w, st.signals, ON_WAIT_QUIET);
		else if (now >= w->deadline)
			reply_wait(w, st.signals, ON_WAIT_TIMEOUT);
		else {
			if (w->deadline < wake)
				wake = w->deadline;
			if (quiet_at < wake)
				wake = quiet_at;
		}
	}
	return wake;
}

void device_init(dispatch_t *dpp, struct on_core *c, const struct device_info *di)
{
	core = c;
	info = di;
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cfuncs, _RESMGR_IO_NFUNCS, &iofuncs);
	iofuncs.read = dev_read;
	iofuncs.devctl = dev_devctl;
	iofuncs.unblock = dev_unblock;
	iofuncs.close_ocb = dev_close_ocb;
	iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);
	resmgr_attr_t rattr = { .nparts_max = 1, .msg_max_size = 4096 };
	if (resmgr_attach(dpp, &rattr, ON_DEV_PATH, _FTYPE_ANY, 0, &cfuncs, &iofuncs, &attr) == -1) {
		fprintf(stderr, "opticnerve: resmgr_attach %s: %s\n", ON_DEV_PATH, strerror(errno));
		_exit(1);
	}
}
