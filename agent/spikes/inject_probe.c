/*
 * inject_probe: spike S7 (input injection reaches an application).
 *
 * Throwaway test program. It creates a Screen context of the type asked for, builds input events and hands
 * them to screen_inject_event() on the first display, printing the return value of every call. What arrives is
 * read from the log of input_log, which runs beside it. Not part of the agent.
 *
 *   inject_probe CTX [dev] s7 X Y        the S7 sequence: pointer move to X,Y; left click there; keys a, B
 *                                        (with Shift), Return
 *   inject_probe CTX [dev] move X Y      one pointer event, no button
 *   inject_probe CTX [dev] click X Y     left button down, then up, at X,Y
 *   inject_probe CTX [dev] key SYM [MOD] one key down and up; SYM and MOD as numbers (0x.. accepted)
 *   inject_probe CTX [dev] touch X Y     a touch and its release at X,Y (SCREEN_EVENT_MTOUCH_TOUCH, _RELEASE)
 *   inject_probe CTX [dev] alttab        Alt down, Tab down and up with KEYMOD_ALT set, Alt up
 *
 *   CTX  ip    input provider context
 *        ipdm  input provider and display manager in one context (what the agent will ask for)
 *        wm    window manager context
 *        app   application context
 *   dev  create a keyboard device and a pointer device in the context and name them in the events
 *   pf=N set SCREEN_PROPERTY_FLAGS of pointer events to N (for example 0x2000, SCREEN_FLAG_POSITION_VALID)
 *   rel  pointer events carry SCREEN_PROPERTY_DISPLACEMENT (X, Y as a relative move) instead of a position
 *   dsp  set SCREEN_PROPERTY_DISPLAY of every event to the display
 *   The three options above may follow CTX in any order, before or after dev.
 *
 * X and Y are display coordinates. Needs root for every CTX except app.
 *
 * Build (baseline SDP-BL-006):
 *   qcc -Vgcc_ntoaarch64le -std=c11 -Wall -Wextra -Werror -O2 -o inject_probe inject_probe.c -lscreen
 */
#define _QNX_SOURCE /* with -std=c11, for the POSIX and QNX declarations */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

static screen_context_t ctx;
static screen_display_t disp;
static screen_device_t kbd, ptr;
static int failures;
static int pflags = -1, relative, set_display;

static void fail(const char *what)
{
	fprintf(stderr, "FAIL %s: %s (errno %d)\n", what, strerror(errno), errno);
	exit(1);
}

static void report(const char *what, int rc)
{
	if (rc == 0) {
		printf("%-34s ok\n", what);
	} else {
		printf("%-34s FAILED: %s (errno %d)\n", what, strerror(errno), errno);
		failures++;
	}
}

static void inject(const char *what, screen_event_t ev)
{
	report(what, screen_inject_event(disp, ev));
	usleep(50000); /* 50 ms between events, so the receiving log shows their order */
}

static void pointer(const char *what, int x, int y, int buttons)
{
	screen_event_t ev;
	int type = SCREEN_EVENT_POINTER, pos[2] = { x, y };
	if (screen_create_event(&ev) != 0)
		fail("screen_create_event");
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
	if (pflags >= 0)
		report("  set FLAGS", screen_set_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &pflags));
	if (relative) {
		report("  set DISPLACEMENT", screen_set_event_property_iv(ev, SCREEN_PROPERTY_DISPLACEMENT, pos));
	} else {
		screen_set_event_property_iv(ev, SCREEN_PROPERTY_POSITION, pos);
		screen_set_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, pos);
	}
	if (set_display)
		report("  set DISPLAY", screen_set_event_property_pv(ev, SCREEN_PROPERTY_DISPLAY, (void **)&disp));
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
	if (ptr)
		screen_set_event_property_pv(ev, SCREEN_PROPERTY_DEVICE, (void **)&ptr);
	inject(what, ev);
	screen_destroy_event(ev);
}

static void key(const char *what, int sym, int cap, int mods, int down)
{
	screen_event_t ev;
	int type = SCREEN_EVENT_KEYBOARD;
	int flags = KEY_SYM_VALID | KEY_CAP_VALID | (down ? KEY_DOWN : 0);
	if (screen_create_event(&ev) != 0)
		fail("screen_create_event");
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &flags);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_SYM, &sym);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_KEY_CAP, &cap);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_MODIFIERS, &mods);
	if (kbd)
		screen_set_event_property_pv(ev, SCREEN_PROPERTY_DEVICE, (void **)&kbd);
	inject(what, ev);
	screen_destroy_event(ev);
}

static void press(const char *name, int sym, int cap, int mods)
{
	char what[64];
	snprintf(what, sizeof(what), "key %s down", name);
	key(what, sym, cap, mods, 1);
	snprintf(what, sizeof(what), "key %s up", name);
	key(what, sym, cap, mods, 0);
}

/* A touch screen's event: SCREEN_EVENT_MTOUCH_TOUCH, _MOVE or _RELEASE at X,Y, finger 0. */
static void touch(const char *what, int type, int x, int y)
{
	screen_event_t ev;
	int pos[2] = { x, y }, id = 0, seq = 1;
	if (screen_create_event(&ev) != 0)
		fail("screen_create_event");
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_POSITION, pos);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, pos);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_TOUCH_ID, &id);
	screen_set_event_property_iv(ev, SCREEN_PROPERTY_SEQUENCE_ID, &seq);
	if (set_display)
		report("  set DISPLAY", screen_set_event_property_pv(ev, SCREEN_PROPERTY_DISPLAY, (void **)&disp));
	if (pflags >= 0)
		report("  set FLAGS", screen_set_event_property_iv(ev, SCREEN_PROPERTY_FLAGS, &pflags));
	inject(what, ev);
	screen_destroy_event(ev);
}

static void click(int x, int y)
{
	pointer("pointer left button down", x, y, SCREEN_LEFT_MOUSE_BUTTON);
	pointer("pointer left button up", x, y, 0);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 3)
		goto usage;
	int type;
	if (strcmp(argv[1], "ip") == 0)
		type = SCREEN_INPUT_PROVIDER_CONTEXT;
	else if (strcmp(argv[1], "ipdm") == 0)
		type = SCREEN_INPUT_PROVIDER_CONTEXT | SCREEN_DISPLAY_MANAGER_CONTEXT;
	else if (strcmp(argv[1], "wm") == 0)
		type = SCREEN_WINDOW_MANAGER_CONTEXT;
	else if (strcmp(argv[1], "app") == 0)
		type = SCREEN_APPLICATION_CONTEXT;
	else
		goto usage;
	int a = 2, devices = 0;
	for (; a < argc; a++) {
		if (strcmp(argv[a], "dev") == 0)
			devices = 1;
		else if (strncmp(argv[a], "pf=", 3) == 0)
			pflags = (int)strtol(argv[a] + 3, NULL, 0);
		else if (strcmp(argv[a], "rel") == 0)
			relative = 1;
		else if (strcmp(argv[a], "dsp") == 0)
			set_display = 1;
		else
			break;
	}
	if (a >= argc)
		goto usage;
	const char *cmd = argv[a++];
	int rest = argc - a;

	printf("inject_probe: context %s%s, pointer flags %d%s%s, uid %d\n", argv[1], devices ? ", own devices" : "",
	       pflags, relative ? ", relative" : "", set_display ? ", display set" : "", (int)geteuid());
	if (screen_create_context(&ctx, type) != 0)
		fail("screen_create_context");
	printf("%-34s ok\n", "screen_create_context");
	int count = 0;
	if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count) != 0 || count < 1)
		fail("get DISPLAY_COUNT");
	screen_display_t *displays = calloc(count, sizeof(*displays));
	if (!displays || screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays) != 0)
		fail("get DISPLAYS");
	disp = displays[0];
	free(displays);
	if (devices) {
		report("create keyboard device", screen_create_device_type(&kbd, ctx, SCREEN_EVENT_KEYBOARD));
		report("create pointer device", screen_create_device_type(&ptr, ctx, SCREEN_EVENT_POINTER));
		if (kbd)
			screen_set_device_property_cv(kbd, SCREEN_PROPERTY_ID_STRING,
						      (int)strlen("opticnerve-probe-kbd"), "opticnerve-probe-kbd");
		if (ptr)
			screen_set_device_property_cv(ptr, SCREEN_PROPERTY_ID_STRING,
						      (int)strlen("opticnerve-probe-ptr"), "opticnerve-probe-ptr");
		screen_flush_context(ctx, 0);
	}

	if (strcmp(cmd, "s7") == 0 && rest == 2) {
		int x = atoi(argv[a]), y = atoi(argv[a + 1]);
		pointer("pointer move", x, y, 0);
		click(x, y);
		press("a", 'a', 'a', 0);
		key("key Shift down", KEYCODE_LEFT_SHIFT, KEYCODE_LEFT_SHIFT, KEYMOD_SHIFT, 1);
		press("B", 'B', 'b', KEYMOD_SHIFT);
		key("key Shift up", KEYCODE_LEFT_SHIFT, KEYCODE_LEFT_SHIFT, 0, 0);
		press("Return", KEYCODE_RETURN, KEYCODE_RETURN, 0);
	} else if (strcmp(cmd, "move") == 0 && rest == 2) {
		pointer("pointer move", atoi(argv[a]), atoi(argv[a + 1]), 0);
	} else if (strcmp(cmd, "click") == 0 && rest == 2) {
		click(atoi(argv[a]), atoi(argv[a + 1]));
	} else if (strcmp(cmd, "touch") == 0 && rest == 2) {
		int x = atoi(argv[a]), y = atoi(argv[a + 1]);
		touch("touch", SCREEN_EVENT_MTOUCH_TOUCH, x, y);
		touch("touch release", SCREEN_EVENT_MTOUCH_RELEASE, x, y);
	} else if (strcmp(cmd, "alttab") == 0 && rest == 0) {
		/* Alt held, with KEYMOD_ALT set on the Tab events themselves (the agent sets no modifiers) */
		key("key Alt down", KEYCODE_LEFT_ALT, KEYCODE_LEFT_ALT, KEYMOD_ALT, 1);
		press("Tab", KEYCODE_TAB, KEYCODE_TAB, KEYMOD_ALT);
		key("key Alt up", KEYCODE_LEFT_ALT, KEYCODE_LEFT_ALT, 0, 0);
	} else if (strcmp(cmd, "key") == 0 && (rest == 1 || rest == 2)) {
		int sym = (int)strtol(argv[a], NULL, 0);
		press(argv[a], sym, sym, rest == 2 ? (int)strtol(argv[a + 1], NULL, 0) : 0);
	} else {
		goto usage;
	}

	if (kbd)
		screen_destroy_device(kbd);
	if (ptr)
		screen_destroy_device(ptr);
	screen_destroy_context(ctx);
	printf("calls that failed: %d\n", failures);
	return failures ? 1 : 0;
usage:
	fprintf(stderr, "usage: inject_probe ip|ipdm|wm|app [dev] [pf=N] [rel] [dsp] s7 X Y | move X Y | click X Y | key SYM [MOD]\n");
	return 2;
}
