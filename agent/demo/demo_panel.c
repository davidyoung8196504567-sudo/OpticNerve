/*
 * demo_panel: the OpticNerve demo application. A full-screen QNX Screen application that looks like a small
 * instrument panel, for a test tool to see and drive: three buttons, a counter, a status lamp and a line of typed
 * text. Everything is drawn by this program, with a 5x7 bitmap font of our own.
 *
 *   demo_panel [SECS]      runs until killed, or for SECS seconds
 *
 * Buttons: "+1" adds one to the counter, "RESET" sets it to 0, "LAMP" switches the lamp between green and red.
 * Keys: letters, digits and space are added to the text line; Backspace deletes; Return clears it.
 *
 * Build (baseline SDP-BL-006): qcc -Vgcc_ntoaarch64le -std=c11 -Wall -Wextra -Werror -O2 -o demo_panel demo_panel.c
 * -lscreen
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

/* 5x7 glyphs, one byte per row, bit 4 is the leftmost column. Drawn for this program. */
static const char FONT_CHARS[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-:.";
static const unsigned char FONT[][7] = {
	{ 0, 0, 0, 0, 0, 0, 0 },                                 /* space */
	{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },            /* 0, plain: OCR read a slashed one as 5 */
	{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },            /* 1 */
	{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },            /* 2 */
	{ 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },            /* 3 */
	{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },            /* 4 */
	{ 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },            /* 5 */
	{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },            /* 6 */
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },            /* 7 */
	{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },            /* 8 */
	{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },            /* 9 */
	{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },            /* A */
	{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },            /* B */
	{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },            /* C */
	{ 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c },            /* D */
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },            /* E */
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },            /* F */
	{ 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },            /* G */
	{ 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },            /* H */
	{ 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },            /* I */
	{ 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c },            /* J */
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },            /* K */
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },            /* L */
	{ 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },            /* M */
	{ 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },            /* N */
	{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },            /* O */
	{ 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },            /* P */
	{ 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },            /* Q */
	{ 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },            /* R */
	{ 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },            /* S */
	{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },            /* T */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },            /* U */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },            /* V */
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },            /* W */
	{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },            /* X */
	{ 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },            /* Y */
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },            /* Z */
	{ 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 },            /* + */
	{ 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 },            /* - */
	{ 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 },            /* : */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c },            /* . */
};

static unsigned char *fb;
static int stride, W, H;

static void fill(int x, int y, int w, int h, unsigned rgb)
{
	for (int j = y; j < y + h && j < H; j++)
		for (int i = x; i < x + w && i < W; i++) {
			unsigned char *p = fb + (size_t)j * stride + (size_t)i * 4;
			p[0] = rgb & 0xff; /* B, G, R, A in memory */
			p[1] = (rgb >> 8) & 0xff;
			p[2] = (rgb >> 16) & 0xff;
			p[3] = 0xff;
		}
}

/* Draws text at x,y with each font pixel `scale` screen pixels square; unknown characters draw as space. */
static void text(int x, int y, int scale, const char *s, unsigned rgb)
{
	for (; *s; s++, x += 7 * scale) { /* two columns between letters: easier to read by OCR */
		char c = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
		const char *at = strchr(FONT_CHARS, c);
		int g = at && c ? (int)(at - FONT_CHARS) : 0;
		for (int row = 0; row < 7; row++)
			for (int col = 0; col < 5; col++)
				if (FONT[g][row] & (0x10 >> col))
					fill(x + col * scale, y + row * scale, scale, scale, rgb);
	}
}

struct button {
	int x, y, w, h;
	const char *label;
};
static const struct button BUTTONS[3] = {
	{ 160, 700, 420, 200, "+1" }, { 750, 700, 420, 200, "RESET" }, { 1340, 700, 420, 200, "LAMP" } };

static int counter, lamp_red;
static char line[25];

static void draw(void)
{
	char buf[64];
	fill(0, 0, W, H, 0x1c2430);                                   /* background */
	fill(0, 0, W, 110, 0x0a3d62);                                 /* title bar */
	text(60, 27, 8, "OPTICNERVE DEMO PANEL", 0xffffff);
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

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 0;
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
	if (screen_create_window_buffers(win, 1) != 0) {
		perror("buffers");
		return 1;
	}
	screen_buffer_t buf;
	screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf);
	screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&fb);
	screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
	int dirty[4] = { 0, 0, W, H };
	draw();
	screen_post_window(win, buf, 1, dirty, 0);

	screen_event_t ev;
	screen_create_event(&ev);
	double t0 = now_s();
	int pressed = -1;
	while (!secs || now_s() - t0 < secs) {
		if (screen_get_event(ctx, ev, 500000000ULL) != 0)
			break;
		int type = SCREEN_EVENT_NONE, changed = 0;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		if (type == SCREEN_EVENT_POINTER) {
			int p[2] = { 0, 0 }, buttons = 0;
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, p);
			screen_get_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
			int down = buttons & SCREEN_LEFT_MOUSE_BUTTON;
			if (down && pressed < 0) {
				for (int i = 0; i < 3; i++)
					if (p[0] >= BUTTONS[i].x && p[0] < BUTTONS[i].x + BUTTONS[i].w && p[1] >= BUTTONS[i].y &&
					    p[1] < BUTTONS[i].y + BUTTONS[i].h)
						pressed = i;
			} else if (!down && pressed >= 0) { /* acts on release, as a button does */
				if (pressed == 0)
					counter++;
				else if (pressed == 1)
					counter = 0;
				else
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
				continue;
			if (sym == KEYCODE_BACKSPACE && n)
				line[n - 1] = 0;
			else if (sym == KEYCODE_RETURN)
				line[0] = 0;
			else if (((sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z') || (sym >= '0' && sym <= '9') ||
				  sym == ' ') && n < sizeof(line) - 1) {
				line[n] = (char)sym;
				line[n + 1] = 0;
			} else
				continue;
			changed = 1;
		}
		if (changed) {
			draw();
			screen_post_window(win, buf, 1, dirty, 0);
		}
	}
	screen_destroy_window(win);
	screen_destroy_context(ctx);
	return 0;
}
