/*
 * input_log: the target test application in its first form, for spike S7 (input injection reaches an
 * application; see docs/DESIGN_SERVER.md, sections 4.8 and 6).
 *
 * Throwaway test program. It opens one window, fills it with one colour, and writes every Screen event the
 * window receives to standard output, one line per event. Not part of the agent.
 *
 *   input_log SECS [X Y W H]      window at X,Y with size WxH (default 200,150 800x500); runs SECS seconds
 *
 * Build (baseline SDP-BL-006):
 *   qcc -Vgcc_ntoaarch64le -std=c11 -Wall -Wextra -Werror -O2 -o input_log input_log.c -lscreen
 */
#define _QNX_SOURCE /* with -std=c11, for the POSIX and QNX declarations */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void fail(const char *what)
{
	fprintf(stderr, "FAIL %s: %s (errno %d)\n", what, strerror(errno), errno);
	exit(1);
}

static const char *event_name(int type)
{
	switch (type) {
	case SCREEN_EVENT_CREATE: return "CREATE";
	case SCREEN_EVENT_PROPERTY: return "PROPERTY";
	case SCREEN_EVENT_CLOSE: return "CLOSE";
	case SCREEN_EVENT_INPUT: return "INPUT";
	case SCREEN_EVENT_POINTER: return "POINTER";
	case SCREEN_EVENT_KEYBOARD: return "KEYBOARD";
	case SCREEN_EVENT_POST: return "POST";
	case SCREEN_EVENT_MTOUCH_TOUCH: return "MTOUCH_TOUCH";
	case SCREEN_EVENT_MTOUCH_MOVE: return "MTOUCH_MOVE";
	case SCREEN_EVENT_MTOUCH_RELEASE: return "MTOUCH_RELEASE";
	default: return "other";
	}
}

static void print_geometry(screen_window_t w, const char *when)
{
	int pos[2] = { -1, -1 }, size[2] = { -1, -1 }, vis = -1, z = 0;
	screen_get_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_VISIBLE, &vis);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_ZORDER, &z);
	printf("window %s: pos %d,%d size %dx%d visible %d zorder %d\n", when, pos[0], pos[1], size[0], size[1], vis,
	       z);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc != 2 && argc != 6) {
		fprintf(stderr, "usage: input_log SECS [X Y W H]\n");
		return 2;
	}
	int secs = atoi(argv[1]);
	int pos[2] = { 200, 150 }, size[2] = { 800, 500 };
	if (argc == 6) {
		pos[0] = atoi(argv[2]);
		pos[1] = atoi(argv[3]);
		size[0] = atoi(argv[4]);
		size[1] = atoi(argv[5]);
	}

	screen_context_t ctx;
	screen_window_t w;
	if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0)
		fail("screen_create_context(application)");
	if (screen_create_window(&w, ctx) != 0)
		fail("screen_create_window");
	int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
	int format = SCREEN_FORMAT_RGBA8888;
	screen_set_window_property_iv(w, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
	screen_set_window_property_cv(w, SCREEN_PROPERTY_ID_STRING, (int)strlen("opticnerve-input-log"),
				      "opticnerve-input-log");
	if (screen_create_window_buffers(w, 1) != 0)
		fail("screen_create_window_buffers");
	screen_buffer_t buf;
	if (screen_get_window_property_pv(w, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf) != 0)
		fail("get RENDER_BUFFERS");
	unsigned char *p = NULL;
	int stride = 0, bsize[2] = { size[0], size[1] };
	screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&p);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_BUFFER_SIZE, bsize);
	if (!p)
		fail("buffer pointer");
	/* One colour: bytes B, G, R, A = 0x40, 0x80, 0x20, 0xff (a green that 16 bits per pixel cannot hold). */
	for (int y = 0; y < bsize[1]; y++) {
		unsigned char *row = p + (size_t)y * stride;
		for (int x = 0; x < bsize[0]; x++) {
			row[4 * x] = 0x40;
			row[4 * x + 1] = 0x80;
			row[4 * x + 2] = 0x20;
			row[4 * x + 3] = 0xff;
		}
	}
	int dirty[4] = { 0, 0, bsize[0], bsize[1] };
	if (screen_post_window(w, buf, 1, dirty, 0) != 0)
		fail("screen_post_window");
	printf("input_log pid %d: asked for pos %d,%d size %dx%d; buffer %dx%d; listening %d s\n", (int)getpid(),
	       pos[0], pos[1], size[0], size[1], bsize[0], bsize[1], secs);
	print_geometry(w, "after post");

	screen_event_t ev;
	if (screen_create_event(&ev) != 0)
		fail("screen_create_event");
	double start = now_s();
	long n = 0, keys = 0, pointers = 0;
	while (now_s() - start < secs) {
		if (screen_get_event(ctx, ev, 500000000ULL) != 0)
			fail("screen_get_event");
		int type = SCREEN_EVENT_NONE;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		if (type == SCREEN_EVENT_NONE)
			continue;
		n++;
		printf("t=%7.3f %s(%d)", now_s() - start, event_name(type), type);
		if (type == SCREEN_EVENT_POINTER) {
			int wpos[2] = { -1, -1 }, spos[2] = { -1, -1 }, buttons = -1, wheel = 0, hwheel = 0;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_POSITION, wpos);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, spos);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_MOUSE_WHEEL, &wheel);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_MOUSE_HORIZONTAL_WHEEL, &hwheel);
			printf(" pos %d,%d source_pos %d,%d buttons 0x%x wheel %d hwheel %d", wpos[0], wpos[1], spos[0],
			       spos[1], buttons, wheel, hwheel);
			pointers++;
		} else if (type == SCREEN_EVENT_KEYBOARD) {
			int flags = 0, mods = 0, scan = 0, sym = 0, cap = 0;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &flags);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_MODIFIERS, &mods);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SCAN, &scan);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SYM, &sym);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_KEY_CAP, &cap);
			printf(" %s%s flags 0x%x modifiers 0x%x scan 0x%x sym 0x%x cap 0x%x",
			       (flags & KEY_DOWN) ? "down" : "up", (flags & KEY_REPEAT) ? " repeat" : "", flags, mods,
			       scan, sym, cap);
			keys++;
		} else if (type == SCREEN_EVENT_PROPERTY) {
			int name = -1;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_NAME, &name);
			printf(" property %d", name);
			if (name == SCREEN_PROPERTY_FOCUS || name == SCREEN_PROPERTY_POINTER_FOCUS) {
				int focus = -1;
				screen_get_window_property_iv(w, name, &focus);
				printf(" focus %d", focus);
			}
		}
		screen_device_t dev = NULL;
		if ((type == SCREEN_EVENT_POINTER || type == SCREEN_EVENT_KEYBOARD) &&
		    screen_get_event_property_pv(ev, SCREEN_PROPERTY_DEVICE, (void **)&dev) == 0 && dev) {
			char id[64] = "";
			screen_get_device_property_cv(dev, SCREEN_PROPERTY_ID_STRING, sizeof(id), id);
			printf(" device '%s'", id);
		}
		printf("\n");
		if (type == SCREEN_EVENT_PROPERTY)
			print_geometry(w, "now");
	}
	printf("events %ld (pointer %ld, keyboard %ld) in %d s\n", n, pointers, keys, secs);
	screen_destroy_event(ev);
	screen_destroy_window(w);
	screen_destroy_context(ctx);
	return 0;
}
