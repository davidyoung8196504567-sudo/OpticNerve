/*
 * demo_panel: the OpticNerve demo application. A full-screen QNX Screen application that looks like a small
 * instrument panel, for a test tool to see and drive: three buttons, a counter, a status lamp and a line of typed
 * text. Everything is drawn by this program, with a 5x7 bitmap font of our own.
 *
 *   demo_panel [-nomarker] [SECS]      runs until killed, or for SECS seconds; -nomarker: no pointer marker
 *
 * The pointer marker is a ring at the pointer's latest position; a move redraws it at most 10 times a second (see
 * below).
 *
 * Buttons: "+1" adds one to the counter, "RESET" sets it to 0, clears the text line and turns the lamp green, "LAMP" switches the lamp between green and red.
 * An X in the top right corner of the title bar closes the panel, as in a windowed GUI: a press must start on it,
 * the box is red while the pointer is on it with the button held, and only a release on it closes.
 * Keys: letters, digits and space are added to the text line; Backspace deletes; Return clears it.
 *
 * Build (baseline SDP-BL-006): qcc -Vgcc_ntoaarch64le -std=c11 -Wall -Wextra -Werror -O2 -o demo_panel demo_panel.c
 * -lscreen
 */
#define _QNX_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

#include "panel_draw.h"

struct button {
	int x, y, w, h;
	const char *label;
};
static const struct button BUTTONS[3] = {
	{ 160, 700, 420, 200, "+1" }, { 750, 700, 420, 200, "RESET" }, { 1340, 700, 420, 200, "LAMP" } };

/* The close box: the right end of the title bar, square. */
#define TITLE_H 110
#define CLOSE_X (W - TITLE_H)

static int counter, lamp_red, close_pressed, close_lit; /* close_lit: pressed and the pointer on the box */
static char line[25];

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/*
 * A pointer marker of our own, so that a viewer (on the monitor and in VNC frames, which carry no pointer image) can
 * see where the pointer is: a ring at its latest position, yellow while a button is held. No trail and no animation.
 * A move redraws the panel at most MARKER_HZ times a second: pointer events that come in between are read and only
 * their position is kept, so a stream of moves (a hand in a viewer sends about 60 a second) never queues redraws.
 * A press or a release is drawn at once, as is everything else the panel shows. MARKER_HIDE seconds after the pointer
 * stops, the ring is taken away (one redraw), so that a test that searches the screen for a button finds it as it is.
 */
#define MARKER_HZ 10
#define MARKER_HIDE 1.0
static int show_marker = 1; /* -nomarker: the panel only */
static int ptr_x = -1, ptr_y = -1, ptr_down, ptr_shown;

static void ring(int cx, int cy, int r, int th, unsigned rgb)
{
	int r2 = r * r, i2 = (r - th) * (r - th);
	for (int y = cy - r; y <= cy + r; y++)
		for (int x = cx - r; x <= cx + r; x++) {
			int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
			if (d2 <= r2 && d2 >= i2 && x >= 0 && y >= 0)
				fill(x, y, 1, 1, rgb);
		}
}

/* Draws the marker over the panel. */
static void overlay(void)
{
	if (ptr_x < 0 || !ptr_shown)
		return;
	ring(ptr_x, ptr_y, 34, 8, ptr_down ? 0xffd166 : 0xff2bd6);
	ring(ptr_x, ptr_y, 5, 5, 0xffffff);
}

static void draw(void)
{
	char buf[64];
	fill(0, 0, W, H, 0x1c2430);                                   /* background */
	fill(0, 0, W, TITLE_H, 0x0a3d62);                             /* title bar */
	text(60, 27, 8, "OPTICNERVE DEMO PANEL", 0xffffff);
	if (close_lit)
		fill(CLOSE_X, 0, TITLE_H, TITLE_H, 0xe81123);
	for (int d = 0; d < 40; d++) { /* the X: two strokes 40 px across, 4 px thick, in the middle of the close box */
		int x0 = CLOSE_X + (TITLE_H - 40) / 2, y0 = (TITLE_H - 40) / 2;
		fill(x0 + d - 1, y0 + d - 1, 4, 4, 0xffffff);
		fill(x0 + 39 - d - 1, y0 + d - 1, 4, 4, 0xffffff);
	}
	text(160, 200, 8, "COUNT", 0x9fb3c8);
	snprintf(buf, sizeof(buf), "%d", counter);
	fill(160, 280, 700, 180, 0x0f1720);
	text(190, 330, 12, buf, 0xffd166); /* scale 12: OCR read larger blocky digits wrongly */
	text(1100, 200, 8, "LAMP", 0x9fb3c8);
	fill(1100, 280, 180, 180, lamp_red ? 0xe63946 : 0x2a9d8f);
	text(1320, 340, 8, lamp_red ? "RED" : "GREEN", 0xffffff);
	text(160, 520, 6, "TEXT:", 0x9fb3c8);
	fill(400, 505, 1360, 80, 0x0f1720);
	text(420, 514, 8, line, 0xffffff);
	for (int i = 0; i < 3; i++) {
		const struct button *b = &BUTTONS[i];
		fill(b->x, b->y, b->w, b->h, 0x3a86ff);
		int tw = (int)strlen(b->label) * 7 * 10 - 20;
		text(b->x + (b->w - tw) / 2, b->y + (b->h - 70) / 2, 10, b->label, 0xffffff);
	}
}

int main(int argc, char **argv)
{
	int secs = 0;
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "-nomarker") == 0)
			show_marker = 0;
		else
			secs = atoi(argv[i]);
	screen_context_t ctx;
	screen_window_t win;
	if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0 || screen_create_window(&win, ctx) != 0) {
		perror("screen");
		return 1;
	}
	screen_display_t disp;
	screen_display_t *displays;
	int count = 0, size[2] = { 1920, 1080 };
	screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count);
	displays = calloc((size_t)(count > 0 ? count : 1), sizeof(*displays));
	screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays);
	disp = displays[0];
	free(displays);
	screen_get_display_property_iv(disp, SCREEN_PROPERTY_SIZE, size);
	W = size[0];
	H = size[1];
	int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE, format = SCREEN_FORMAT_RGBA8888, pos[2] = { 0, 0 };
	screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
	screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, 16, "opticnerve-demo");
	/* Two buffers: a frame is drawn into the one not on show and posted only when it is complete, so the monitor
	 * never shows a half-drawn frame. */
	if (screen_create_window_buffers(win, 2) != 0) {
		perror("buffers");
		return 1;
	}
	screen_buffer_t bufs[2];
	screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)bufs);
	int back = 0;
	int dirty[4] = { 0, 0, W, H };
	screen_get_buffer_property_pv(bufs[back], SCREEN_PROPERTY_POINTER, (void **)&fb);
	screen_get_buffer_property_iv(bufs[back], SCREEN_PROPERTY_STRIDE, &stride);
	draw();
	screen_post_window(win, bufs[back], 1, dirty, 0);
	back ^= 1;

	screen_event_t ev;
	screen_create_event(&ev);
	double t0 = now_s(), drawn = 0, moved = 0; /* when the panel was last drawn, when the pointer last moved */
	int pressed = -1, last_buttons = 0, move_pending = 0, closed = 0;
	while (!closed && (!secs || now_s() - t0 < secs)) {
		/* wait for events, but wake for a move not drawn yet (when its 1/MARKER_HZ s is up) or to hide the ring */
		double due = now_s() + 0.5;
		if (move_pending && drawn + 1.0 / MARKER_HZ < due)
			due = drawn + 1.0 / MARKER_HZ;
		if (ptr_shown && !move_pending && !ptr_down && moved + MARKER_HIDE < due)
			due = moved + MARKER_HIDE;
		double left = due - now_s();
		uint64_t wait = left > 0 ? (uint64_t)(left * 1e9) : 0;
		if (screen_get_event(ctx, ev, wait) != 0)
			break;
		int type = SCREEN_EVENT_NONE, changed = 0;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		if (type == SCREEN_EVENT_POINTER) {
			int p[2] = { 0, 0 }, buttons = 0;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, p);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
			if (show_marker && (p[0] != ptr_x || p[1] != ptr_y)) {
				ptr_x = p[0];
				ptr_y = p[1];
				move_pending = 1;
				moved = now_s();
			}
			if (buttons != last_buttons) {
				ptr_down = buttons != 0;
				moved = now_s();
				changed = show_marker; /* a press or a release is drawn at once */
			}
			int was_down = last_buttons & SCREEN_LEFT_MOUSE_BUTTON;
			last_buttons = buttons;
			int down = buttons & SCREEN_LEFT_MOUSE_BUTTON;
			int in_close = p[0] >= CLOSE_X && p[0] < W && p[1] >= 0 && p[1] < TITLE_H;
			if (down && pressed < 0 && !close_pressed) {
				for (int i = 0; i < 3; i++)
					if (p[0] >= BUTTONS[i].x && p[0] < BUTTONS[i].x + BUTTONS[i].w && p[1] >= BUTTONS[i].y &&
					    p[1] < BUTTONS[i].y + BUTTONS[i].h)
						pressed = i;
				if (in_close && !was_down) { /* the press itself, not a held button slid onto the box */
					close_pressed = close_lit = 1;
					changed = 1;
				}
			} else if (down && close_pressed) { /* slid off: no longer red; back on: red again */
				if (in_close != close_lit) {
					close_lit = in_close;
					changed = 1;
				}
			} else if (!down && close_pressed) { /* released inside: close; anywhere else: nothing */
				close_pressed = close_lit = 0;
				changed = 1;
				if (in_close) {
					printf("closed from the panel\n");
					closed = 1;
				}
			} else if (!down && pressed >= 0) { /* acts on release, as a button does */
				if (pressed == 0)
					counter++;
				else if (pressed == 1) {
					counter = 0;
					line[0] = 0; /* each test run starts from a clean panel */
					lamp_red = 0;
				} else
					lamp_red = !lamp_red;
				pressed = -1;
				changed = 1;
			}
		} else if (type == SCREEN_EVENT_KEYBOARD) {
			int flags = 0, sym = 0;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &flags);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SYM, &sym);
			size_t n = strlen(line);
			if (!(flags & KEY_DOWN))
				goto redraw;
			if (sym == KEYCODE_BACKSPACE && n)
				line[n - 1] = 0;
			else if (sym == KEYCODE_RETURN)
				line[0] = 0;
			else if (((sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z') || (sym >= '0' && sym <= '9') ||
				  sym == ' ') && n < sizeof(line) - 1) {
				line[n] = (char)sym;
				line[n + 1] = 0;
			} else
				goto redraw;
			changed = 1;
		}
	redraw:
		if (move_pending && now_s() - drawn >= 1.0 / MARKER_HZ)
			changed = 1;
		if (changed && show_marker)
			ptr_shown = 1;
		if (ptr_shown && !move_pending && !ptr_down && now_s() - moved >= MARKER_HIDE) {
			ptr_shown = 0; /* the pointer has rested: the panel without the ring */
			changed = 1;
		}
		if (changed) {
			screen_get_buffer_property_pv(bufs[back], SCREEN_PROPERTY_POINTER, (void **)&fb);
			screen_get_buffer_property_iv(bufs[back], SCREEN_PROPERTY_STRIDE, &stride);
			draw();
			if (show_marker)
				overlay();
			screen_post_window(win, bufs[back], 1, dirty, 0);
			back ^= 1;
			move_pending = 0;
			drawn = now_s();
		}
	}
	screen_destroy_window(win);
	screen_destroy_context(ctx);
	return 0;
}
