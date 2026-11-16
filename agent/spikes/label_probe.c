/*
 * label_probe: spike S10 (label window placement). Throwaway test program, not part of the agent.
 *
 *   label_probe SECS X Y W H [never|nofocus|none] [awake]
 *
 * Creates one window of the asked size and position, painted white, with SCREEN_PROPERTY_SENSITIVITY set to
 * SCREEN_SENSITIVITY_NEVER or _NO_FOCUS (or not set), and with keep-awake if asked. Prints where the window really
 * is, whether it is visible and whether it has focus, at once and every second, and every event it receives. The
 * question: does the window manager leave a small window small, and does such a window stay out of the input path?
 *
 * Build (baseline SDP-BL-006):
 *   qcc -Vgcc_ntoaarch64le -std=c11 -Wall -Wextra -Werror -O2 -o label_probe label_probe.c -lscreen
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <screen/screen.h>

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void show(screen_window_t w, double t, const char *why)
{
	int pos[2] = { -1, -1 }, size[2] = { -1, -1 }, vis = -1, focus = -1, z = 0, sens = -1, idle = -1;
	screen_get_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_VISIBLE, &vis);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_FOCUS, &focus);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_ZORDER, &z);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_SENSITIVITY, &sens);
	screen_get_window_property_iv(w, SCREEN_PROPERTY_IDLE_MODE, &idle);
	printf("L: t=%5.2f %-10s pos %d,%d size %dx%d visible %d focus %d zorder %d sensitivity %d idle_mode %d\n", t,
	       why, pos[0], pos[1], size[0], size[1], vis, focus, z, sens, idle);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 6) {
		fprintf(stderr, "usage: label_probe SECS X Y W H [never|nofocus|none] [awake]\n");
		return 2;
	}
	int secs = atoi(argv[1]), pos[2] = { atoi(argv[2]), atoi(argv[3]) }, size[2] = { atoi(argv[4]), atoi(argv[5]) };
	const char *mode = argc > 6 ? argv[6] : "none";
	int awake = argc > 7 && strcmp(argv[7], "awake") == 0;
	screen_context_t ctx;
	screen_window_t w;
	if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0 || screen_create_window(&w, ctx) != 0) {
		perror("screen");
		return 1;
	}
	int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE, format = SCREEN_FORMAT_RGBA8888;
	screen_set_window_property_iv(w, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_set_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
	screen_set_window_property_cv(w, SCREEN_PROPERTY_ID_STRING, 16, "opticnerve-label");
	int sens = strcmp(mode, "never") == 0 ? SCREEN_SENSITIVITY_NEVER
		 : strcmp(mode, "nofocus") == 0 ? SCREEN_SENSITIVITY_NO_FOCUS : -1;
	if (sens >= 0 && screen_set_window_property_iv(w, SCREEN_PROPERTY_SENSITIVITY, &sens) != 0)
		perror("set SENSITIVITY");
	int idle = SCREEN_IDLE_MODE_KEEP_AWAKE;
	if (awake && screen_set_window_property_iv(w, SCREEN_PROPERTY_IDLE_MODE, &idle) != 0)
		perror("set IDLE_MODE");
	if (screen_create_window_buffers(w, 1) != 0) {
		perror("buffers");
		return 1;
	}
	screen_buffer_t buf;
	unsigned char *p = NULL;
	int stride = 0;
	screen_get_window_property_pv(w, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf);
	screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&p);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
	for (int y = 0; y < size[1]; y++)
		memset(p + (size_t)y * stride, 0xff, (size_t)size[0] * 4);
	int dirty[4] = { 0, 0, size[0], size[1] };
	screen_post_window(w, buf, 1, dirty, 0);
	screen_flush_context(ctx, 0);
	double t0 = now_s(), next = 0;
	printf("L: asked pos %d,%d size %dx%d sensitivity %s awake %d\n", pos[0], pos[1], size[0], size[1], mode, awake);
	show(w, 0, "posted");
	screen_event_t ev;
	screen_create_event(&ev);
	while (now_s() - t0 < secs) {
		screen_get_event(ctx, ev, 200000000ULL);
		int type = SCREEN_EVENT_NONE;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		double t = now_s() - t0;
		if (type == SCREEN_EVENT_POINTER || type == SCREEN_EVENT_KEYBOARD)
			printf("L: t=%5.2f received %s\n", t, type == SCREEN_EVENT_POINTER ? "POINTER" : "KEYBOARD");
		if (t >= next) {
			show(w, t, "tick");
			next += 1.0;
		}
	}
	screen_destroy_window(w);
	screen_destroy_context(ctx);
	return 0;
}
