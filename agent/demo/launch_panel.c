/*
 * launch_panel: the OpticNerve launcher panel. A full-screen QNX Screen application with a tile for each app of the
 * quick-start image's launcher configuration; a click on a tile starts that app, as often as needed, so that a test
 * tool or a person in a viewer can open the image's apps over VNC. Drawn by this program, with the font of the demo
 * panel (panel_draw.h) and the image's own icons.
 *
 *   launch_panel [-daemon] [-conf DIR] [-state FILE] [-noextra]   the panel (as root, for the apps to run as they
 *                                                                  are meant to); -daemon: in the background, in a
 *                                                                  session of its own (so a sudo that started it
 *                                                                  returns)
 *   launch_panel -list [-conf DIR] [-noextra]           print the tiles it would show, and exit (no Screen)
 *
 * DIR (default /system/etc/desktop_files) holds desktop.conf, whose [Launcher] groups name a .desktop file each
 * (DesktopFileName) and its place (Row, Order). Of a .desktop file, the [Desktop Entry] group's Name, Exec and Icon are
 * used. Tiles are laid out by row and order, at most 4 to a line. Two extra tiles of ours are added when their
 * programs are on the target (-noextra: not): the OpticNerve demo panel (agent/demo/demo_panel.c, from
 * /data/home/qnxuser, as the user qnxuser through sudo, without its pointer marker and with no time limit) at the end
 * of row 1, and GTK4 Widget Factory at the end of row 2. The OpticNerve mark (the logo's eye and circuit traces,
 * agent/demo/opticnerve_mark.png cut from docs/brand/opticnerve-logo-transparent.png, installed with the panel in
 * /data/opticnerve/share/) is the demo panel's icon and stands at the left of the title bar.
 *
 * Clicks act as the close X of the demo panel: a press must start on a tile, the tile is highlighted while the
 * pointer is on it with the button held, and only a release on it starts the app. An app is started with
 * /system/bin/sh -c "<Exec>", from /, with a fixed environment (that of the image's launcher), in a session of its
 * own, its output to /dev/null. While anything of that session's process group runs, the tile shows Running; this
 * is a hint and never stops another start (clicks on a tile in the 3 s after it started an app are ignored). The
 * running groups are written to FILE (default /dev/shmem/opticnerve-launch.state, gone at a reboot) for
 * launch_panel.sh stop -apps; a new panel keeps the entries of groups that still run. An X in the top right corner of
 * the title bar ends the panel; apps it started keep running. Alt+Tab switches between apps and Alt+Tab+Del, with Tab
 * held, closes the one selected (the window manager's keys; shown on the panel). A viewer on a laptop keeps Alt+Tab
 * for the laptop: from a viewer, the agent's key chord stands in for them (hold Q and tap N; hold Q and N, then press
 * X; agent/core/on_core.h). The display must be at least
 * 1760x1000 (two lines of tiles and the hint).
 *
 * Build (baseline SDP-BL-006): see agent/Makefile (-lscreen -limg).
 */
#define _QNX_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <img/img.h>
#include <screen/screen.h>

#include "panel_draw.h"

#define MAX_TILES 12
#define EXEC_MAX 255
#define LINE_LEN 512
#define GUARD 3.0                     /* s: clicks on a tile just after it started an app are ignored */
#define TITLE_H 110
#define CLOSE_X (W - TITLE_H)
#define TILE_W 400
#define TILE_H 300
#define TILE_GAP 40
#define ICON 150                      /* an icon is scaled down to fit ICON_W x ICON */
#define ICON_W 300                    /* wider than high: room for a wide mark; square icons stay ICON x ICON */
#define MARK "/data/opticnerve/share/opticnerve_mark.png"
#define PER_LINE 4

struct tile {
	char name[64], exec[EXEC_MAX + 1], icon[256];
	int row, order, seq;
	uint8_t *px;                  /* the icon, B, G, R, A, iw x ih, or NULL */
	int iw, ih;
	int running;                  /* groups of its apps that still run */
	double started;
	int x, y;                     /* on the screen */
};
static struct tile tiles[MAX_TILES];
static int ntiles;
static const char *conf_dir = "/system/etc/desktop_files", *state_file = "/dev/shmem/opticnerve-launch.state";

/* Every app started, each a process group of its own (a new session), until nothing of the group runs. */
#define MAX_GROUPS 64
struct group {
	pid_t pgid;
	int tile;                     /* index into tiles, -1 for a group kept from an earlier panel */
	int leader;                   /* its first process not yet reaped */
	char name[64];
};
static struct group groups[MAX_GROUPS];
static int ngroups;

/* The environment the image's launcher runs with, for the apps started from here. */
static char *const APP_ENV[] = { "PATH=/proc/boot:/system/bin", "ALSA_CONFIG_PATH=/usr/etc/alsa/alsa.conf",
	"PCI_BKWD_COMPAT_MODULE=/proc/boot/pci_bkwd_compat.so", "LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll",
	"FONTCONFIG_PATH=/etc/fonts", "TZDIR=/usr/share/zoneinfo", "PCI_SLOG_MODULE=pci_slog2.so", "HOME=/data/home/root",
	"TERMINFO=/usr/share/terminfo", "TERM=xterm-256color", "PCI_CAP_MODULE_DIR=/proc/boot",
	"MM_INIT=/usr/lib/dll/mmedia", "OMXIL_COMPONENT_PATH=/usr/lib/dll/omxil", "PCI_HW_CONFIG_FILE=/proc/boot/pci_hw.cfg",
	"VIMRUNTIME=/usr/share/vim/vim91", NULL };

/* Our extra tiles: each shown when its programs are there. */
static const struct { const char *name, *exec, *icon, *program, *program2; int row; } EXTRA[] = {
	{ "OpticNerve Demo Panel", "/usr/bin/sudo -u qnxuser /data/home/qnxuser/demo_panel -nomarker",
	  MARK, "/data/home/qnxuser/demo_panel", "/usr/bin/sudo", 1 },
	{ "Widget Factory", "GDK_BACKEND=qnx GSK_RENDERER=cairo /usr/bin/gtk4-widget-factory",
	  "/usr/share/icons/qnx/cube_large_coral.png", "/usr/bin/gtk4-widget-factory", NULL, 2 },
};

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- the configuration ---- */

/* Reads one line without its line end into buf; 0 at the end of the file, -1 for a line too long (its rest is
 * read and dropped, so it is not taken as a line of its own). */
static int read_line(FILE *f, char *buf, size_t size)
{
	if (!fgets(buf, (int)size, f))
		return 0;
	size_t n = strlen(buf);
	if (n && buf[n - 1] != '\n' && !feof(f)) {
		int c;
		while ((c = fgetc(f)) != EOF && c != '\n')
			;
		return -1;
	}
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	return 1;
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;
	return s;
}

/* Splits "key = value"; 0 if the line has no '='. */
static int key_value(char *line, char **k, char **v)
{
	char *eq = strchr(line, '=');
	if (!eq)
		return 0;
	*eq = 0;
	*k = trim(line);
	*v = trim(eq + 1);
	return 1;
}

/* Removes the .desktop field codes (%f %F %u %U %i %c %k) from Exec; "%%" becomes "%". */
static void drop_field_codes(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++) {
		if (p[0] == '%' && p[1]) {
			if (p[1] == '%')
				*o++ = '%';
			p++;
			continue;
		}
		*o++ = *p;
	}
	while (o > s && (o[-1] == ' ' || o[-1] == '\t')) /* "app %U" -> "app" */
		o--;
	*o = 0;
}

/* Fills t from a .desktop file's [Desktop Entry] group; 0 if it has a Name and an Exec. */
static int read_desktop(const char *path, struct tile *t)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "launch_panel: %s: %s: skipped\n", path, strerror(errno));
		return -1;
	}
	char line[LINE_LEN], *k, *v;
	int r, in_entry = 0, bad = 0;
	t->name[0] = t->exec[0] = t->icon[0] = 0;
	while ((r = read_line(f, line, sizeof(line))) != 0) {
		if (r < 0) {
			fprintf(stderr, "launch_panel: %s: a line of %d bytes or more: ignored\n", path, LINE_LEN - 1);
			continue;
		}
		char *s = trim(line);
		if (*s == '[') {
			in_entry = strcmp(s, "[Desktop Entry]") == 0;
			continue;
		}
		if (!in_entry || *s == '#' || !key_value(s, &k, &v))
			continue;
		if (strcmp(k, "Name") == 0)
			snprintf(t->name, sizeof(t->name), "%s", v);
		else if (strcmp(k, "Exec") == 0) {
			if (strlen(v) > EXEC_MAX)
				bad = 1; /* never truncated: it runs as root */
			else
				snprintf(t->exec, sizeof(t->exec), "%s", v);
		} else if (strcmp(k, "Icon") == 0)
			snprintf(t->icon, sizeof(t->icon), "%s", v);
	}
	fclose(f);
	if (bad) {
		fprintf(stderr, "launch_panel: %s: Exec over %d bytes: skipped\n", path, EXEC_MAX);
		return -1;
	}
	drop_field_codes(t->exec);
	if (!t->name[0] || !t->exec[0]) {
		fprintf(stderr, "launch_panel: %s: no Name or no Exec: skipped\n", path);
		return -1;
	}
	return 0;
}

static void add_tile(const struct tile *t)
{
	if (ntiles == MAX_TILES) {
		fprintf(stderr, "launch_panel: more than %d tiles: %s not shown\n", MAX_TILES, t->name);
		return;
	}
	tiles[ntiles] = *t;
	tiles[ntiles].seq = ntiles;
	ntiles++;
}

/* The [Launcher] group just read (file, row, order): its tile. */
static void end_group(const char *file, int row, int order)
{
	if (!file[0])
		return;
	if (strchr(file, '/')) {
		fprintf(stderr, "launch_panel: DesktopFileName %s is not a file name: skipped\n", file);
		return;
	}
	char path[512];
	struct tile t;
	memset(&t, 0, sizeof(t));
	snprintf(path, sizeof(path), "%s/%s", conf_dir, file);
	if (read_desktop(path, &t) == 0) {
		t.row = row;
		t.order = order;
		add_tile(&t);
	}
}

static int by_place(const void *a, const void *b)
{
	const struct tile *x = a, *y = b;
	if (x->row != y->row)
		return x->row - y->row;
	if (x->order != y->order)
		return x->order - y->order;
	return x->seq - y->seq;
}

static void read_conf(int extra)
{
	char path[512], line[LINE_LEN], file[256] = "", *k, *v;
	int r, in_launcher = 0, row = 1, order = 0;
	snprintf(path, sizeof(path), "%s/desktop.conf", conf_dir);
	FILE *f = fopen(path, "r");
	if (!f)
		fprintf(stderr, "launch_panel: %s: %s\n", path, strerror(errno));
	while (f && (r = read_line(f, line, sizeof(line))) != 0) {
		if (r < 0) {
			fprintf(stderr, "launch_panel: %s: a line of %d bytes or more: ignored\n", path, LINE_LEN - 1);
			continue;
		}
		char *s = trim(line);
		if (*s == '[') {
			if (in_launcher)
				end_group(file, row, order);
			in_launcher = strcmp(s, "[Launcher]") == 0;
			file[0] = 0;
			row = 1;
			order = 0;
			continue;
		}
		if (!in_launcher || *s == '#' || !key_value(s, &k, &v))
			continue;
		if (strcmp(k, "DesktopFileName") == 0)
			snprintf(file, sizeof(file), "%s", v);
		else if (strcmp(k, "Row") == 0)
			row = atoi(v);
		else if (strcmp(k, "Order") == 0)
			order = atoi(v);
	}
	if (f) {
		if (in_launcher)
			end_group(file, row, order);
		fclose(f);
	}
	for (size_t i = 0; extra && i < sizeof(EXTRA) / sizeof(EXTRA[0]); i++) {
		if (access(EXTRA[i].program, X_OK) != 0 || (EXTRA[i].program2 && access(EXTRA[i].program2, X_OK) != 0))
			continue;
		struct tile t;
		memset(&t, 0, sizeof(t));
		snprintf(t.name, sizeof(t.name), "%s", EXTRA[i].name);
		snprintf(t.exec, sizeof(t.exec), "%s", EXTRA[i].exec);
		snprintf(t.icon, sizeof(t.icon), "%s", EXTRA[i].icon);
		t.row = EXTRA[i].row;
		t.order = 1000;
		add_tile(&t);
	}
	qsort(tiles, (size_t)ntiles, sizeof(tiles[0]), by_place);
}

/* ---- icons ---- */

static uint8_t *mark;                /* the title bar's mark, B, G, R, A, mark_w x mark_h, or NULL */
static int mark_w, mark_h;

/* Loads a PNG as B, G, R, A and scales it once (box average, colour weighted by alpha) to fit max_w x max_h; NULL
 * if it cannot be loaded. */
static uint8_t *load_image(img_lib_t ilib, const char *path, int max_w, int max_h, int *w, int *h)
{
	img_t img;
	memset(&img, 0, sizeof(img));
	img.format = IMG_FMT_PKLE_ARGB8888; /* B, G, R, A in memory, as the window */
	img.flags = IMG_FORMAT;
	int rc = img_load_file(ilib, path, NULL, &img);
	if (rc != IMG_ERR_OK || !(img.flags & IMG_DIRECT) || !img.w || !img.h) {
		printf("image %s: not loaded (%d)\n", path, rc);
		return NULL;
	}
	double scale = (double)max_w / img.w < (double)max_h / img.h ? (double)max_w / img.w : (double)max_h / img.h;
	if (scale > 1)
		scale = 1;
	*w = (int)(img.w * scale + 0.5);
	*h = (int)(img.h * scale + 0.5);
	if (*w < 1)
		*w = 1;
	if (*h < 1)
		*h = 1;
	uint8_t *px = malloc((size_t)*w * *h * 4);
	if (!px) {
		printf("image %s: not loaded (no memory)\n", path);
		free(img.access.direct.data);
		return NULL;
	}
	for (int y = 0; y < *h; y++)
		for (int x = 0; x < *w; x++) {
			unsigned x0 = (unsigned)(x / scale), x1 = (unsigned)((x + 1) / scale);
			unsigned y0 = (unsigned)(y / scale), y1 = (unsigned)((y + 1) / scale);
			if (x1 > img.w)
				x1 = img.w;
			if (y1 > img.h)
				y1 = img.h;
			if (x1 <= x0)
				x1 = x0 + 1;
			if (y1 <= y0)
				y1 = y0 + 1;
			/* colour weighted by alpha: transparent pixels (often black) must not darken the edges */
			unsigned sum[4] = { 0, 0, 0, 0 }, n = 0;
			for (unsigned sy = y0; sy < y1; sy++)
				for (unsigned sx = x0; sx < x1; sx++, n++) {
					const uint8_t *s = img.access.direct.data + sy * img.access.direct.stride + sx * 4;
					for (int c = 0; c < 3; c++)
						sum[c] += s[c] * s[3];
					sum[3] += s[3];
				}
			uint8_t *d = px + ((size_t)y * *w + x) * 4;
			for (int c = 0; c < 3; c++)
				d[c] = (uint8_t)(sum[3] ? sum[c] / sum[3] : 0);
			d[3] = (uint8_t)(sum[3] / n);
		}
	free(img.access.direct.data);
	printf("image %s: %ux%u, shown at %dx%d\n", path, img.w, img.h, *w, *h);
	return px;
}

/* Loads each tile's icon and the title bar's mark. */
static void load_icons(void)
{
	img_lib_t ilib = NULL;
	if (img_lib_attach(&ilib) != IMG_ERR_OK) {
		fprintf(stderr, "launch_panel: no image library: tiles without icons\n");
		return;
	}
	for (int i = 0; i < ntiles; i++)
		if (tiles[i].icon[0])
			tiles[i].px = load_image(ilib, tiles[i].icon, ICON_W, ICON, &tiles[i].iw, &tiles[i].ih);
	mark = load_image(ilib, MARK, W / 4, TITLE_H - 20, &mark_w, &mark_h);
	img_lib_detach(ilib);
}

/* ---- drawing ---- */

static int lit = -1;          /* the tile (or -2 the close box) pressed and under the pointer */

/* Lines of tiles, a new one for each row and after PER_LINE tiles (fewer on a narrow display), each centred, under
 * its row's header; a tile that would not fit above the hint line is not shown (y -1). */
static void layout(void)
{
	int per = (W + TILE_GAP) / (TILE_W + TILE_GAP), y = 150 + 60, start = 0;
	if (per > PER_LINE)
		per = PER_LINE;
	while (start < ntiles) {
		int n = 1;
		while (start + n < ntiles && n < per && tiles[start + n].row == tiles[start].row)
			n++;
		int x = (W - (n * TILE_W + (n - 1) * TILE_GAP)) / 2;
		for (int j = start; j < start + n; j++, x += TILE_W + TILE_GAP) {
			tiles[j].x = x;
			tiles[j].y = y + TILE_H <= H - 90 ? y : -1;
			if (tiles[j].y < 0)
				fprintf(stderr, "launch_panel: no room for %s: not shown\n", tiles[j].name);
			else /* for a test tool: where to click */
				printf("tile %s at %d,%d size %dx%d\n", tiles[j].name, tiles[j].x, tiles[j].y, TILE_W, TILE_H);
		}
		start += n;
		y += TILE_H + 30 + (start < ntiles && tiles[start].row != tiles[start - 1].row ? 60 : 0);
	}
}

/* Draws a B, G, R, A image of w x h at x0,y0, blended by its alpha over what is there. */
static void blit(const uint8_t *px, int w, int h, int x0, int y0)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int sx = x0 + x, sy = y0 + y;
			if (sx < 0 || sy < 0 || sx >= W || sy >= H)
				continue;
			const uint8_t *s = px + ((size_t)y * w + x) * 4;
			uint8_t *d = fb + (size_t)sy * stride + (size_t)sx * 4;
			for (int c = 0; c < 3; c++)
				d[c] = (uint8_t)((s[c] * s[3] + d[c] * (255 - s[3])) / 255);
		}
}

/* The tile's name as configured: on one line at scale 6 if it fits, else on two lines broken at the first space (or
 * one line if it has none) at scale 5, the lines 10 x scale apart so the first line's descenders clear the second. */
static void draw_name(const struct tile *t)
{
	char a[sizeof(t->name)], b[sizeof(t->name)] = "";
	memcpy(a, t->name, sizeof(a)); /* name is a terminated string of that size */
	int scale = 6, step = 0;
	if (text_width(scale, a) > TILE_W - 20) {
		char *sp = strchr(a, ' ');
		if (sp) {
			*sp = 0;
			memcpy(b, sp + 1, strlen(sp + 1) + 1);
		}
		scale = 5;
		step = 10 * scale;
	}
	int ty = t->y + 20 + ICON + 20;
	text(t->x + (TILE_W - text_width(scale, a)) / 2, ty, scale, a, 0xffffff);
	if (*b)
		text(t->x + (TILE_W - text_width(scale, b)) / 2, ty + step, scale, b, 0xffffff);
}

static void draw(void)
{
	fill(0, 0, W, H, 0x1c2430);
	fill(0, 0, W, TITLE_H, 0x0a3d62);
	if (mark) /* the mark at the left, the title after it */
		blit(mark, mark_w, mark_h, 30, (TITLE_H - mark_h) / 2);
	text(mark ? 30 + mark_w + 30 : 60, 27, 8, "OpticNerve Launcher", 0xffffff);
	if (lit == -2)
		fill(CLOSE_X, 0, TITLE_H, TITLE_H, 0xe81123);
	for (int d = 0; d < 40; d++) { /* the X, as the demo panel's */
		int x0 = CLOSE_X + (TITLE_H - 40) / 2, y0 = (TITLE_H - 40) / 2;
		fill(x0 + d - 1, y0 + d - 1, 4, 4, 0xffffff);
		fill(x0 + 39 - d - 1, y0 + d - 1, 4, 4, 0xffffff);
	}
	int last_row = -1;
	for (int i = 0; i < ntiles; i++) {
		struct tile *t = &tiles[i];
		if (t->y < 0)
			continue;
		if (t->row != last_row) {
			const char *h = t->row == 1 ? "QNX Apps" : "QNX Sample Applications";
			text((W - text_width(5, h)) / 2, t->y - 50, 5, h, 0x9fb3c8);
			last_row = t->row;
		}
		fill(t->x, t->y, TILE_W, TILE_H, i == lit ? 0x3a86ff : 0x5a5a5a);
		fill(t->x + 3, t->y + 3, TILE_W - 6, TILE_H - 6, i == lit ? 0x3a86ff : 0x2f2f2f);
		if (t->px)
			blit(t->px, t->iw, t->ih, t->x + (TILE_W - t->iw) / 2, t->y + 20 + (ICON - t->ih) / 2);
		draw_name(t);
		if (t->running)
			text(t->x + 12, t->y + 12, 2, "Running", 0x2a9d8f);
	}
	if (!ntiles)
		text(160, 300, 8, "No Tiles In desktop.conf", 0xffd166);
	static const char *const hint[2] = { "Alt+Tab Switches Apps    Alt+Tab+Del With Tab Held Closes The Selected App",
		"From A Viewer: Hold Q And Tap N To Switch    Hold Q And N Then Press X To Close" };
	for (int i = 0; i < 2; i++) {
		int sc = text_width(3, hint[i]) <= W - 40 ? 3 : 2;
		text((W - text_width(sc, hint[i])) / 2, H - 70 + i * 32, sc, hint[i], 0x9fb3c8);
	}
}

/* ---- apps ---- */

static int write_groups(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	for (int i = 0; i < ngroups; i++)
		fprintf(f, "%d %s\n", (int)groups[i].pgid, groups[i].name);
	return fclose(f);
}

/* Written to FILE.tmp and renamed, so a reader never sees half a file; where rename is not supported (/dev/shmem),
 * written in place. */
static void write_state(void)
{
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", state_file);
	if (write_groups(tmp) == 0 && rename(tmp, state_file) == 0)
		return;
	int e = errno;
	unlink(tmp);
	if (e != ENOSYS && e != EXDEV && e != EINVAL && e != ENOTSUP)
		printf("state %s: %s\n", state_file, strerror(e));
	else if (write_groups(state_file) != 0)
		printf("state %s: %s\n", state_file, strerror(errno));
}

/* A group runs while kill(-pgid, 0) does not say ESRCH (EPERM: it runs, as someone else). */
static int group_runs(pid_t pgid)
{
	return kill(-pgid, 0) == 0 || errno != ESRCH;
}

/* The groups an earlier panel started and that still run: kept in the state file. */
static void keep_old_groups(void)
{
	FILE *f = fopen(state_file, "r");
	char line[128];
	while (f && fgets(line, sizeof(line), f) && ngroups < MAX_GROUPS) {
		int pgid = 0, at = 0;
		if (sscanf(line, "%d %n", &pgid, &at) < 1 || pgid <= 1 || !group_runs(pgid))
			continue;
		struct group *g = &groups[ngroups++];
		g->pgid = pgid;
		g->tile = -1;
		g->leader = 0;
		snprintf(g->name, sizeof(g->name), "%.*s", (int)strcspn(line + at, "\n"), line + at);
	}
	if (f)
		fclose(f);
}

static void start_app(struct tile *t)
{
	long max = sysconf(_SC_OPEN_MAX);
	for (int fd = 3; fd < (max > 0 && max < 1024 ? max : 1024); fd++) /* the Screen connection stays with us */
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	posix_spawnattr_t a;
	posix_spawn_file_actions_t fa;
	sigset_t all, none;
	sigfillset(&all);
	sigemptyset(&none);
	pid_t pid = -1;
	if (ngroups == MAX_GROUPS) {
		printf("%s: not started: %d apps of ours run already\n", t->name, MAX_GROUPS);
		return;
	}
	int r = posix_spawnattr_init(&a), have_a = !r, have_fa = 0;
	if (!r)
		r = posix_spawn_file_actions_init(&fa), have_fa = !r;
	if (!r)
		r = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	if (!r)
		r = posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	if (!r)
		r = posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
	if (!r)
		r = posix_spawnattr_setxflags(&a, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
	if (!r)
		r = posix_spawnattr_setsigdefault(&a, &all);
	if (!r)
		r = posix_spawnattr_setsigmask(&a, &none);
	char *argv[] = { "/system/bin/sh", "-c", t->exec, NULL };
	if (!r)
		r = posix_spawn(&pid, argv[0], &fa, &a, argv, APP_ENV);
	if (have_a)
		posix_spawnattr_destroy(&a);
	if (have_fa)
		posix_spawn_file_actions_destroy(&fa);
	if (r) {
		printf("%s: not started: %s\n", t->name, strerror(r));
		return;
	}
	struct group *g = &groups[ngroups++];
	g->pgid = pid; /* a new session: pid is its group */
	g->tile = (int)(t - tiles);
	g->leader = 1;
	snprintf(g->name, sizeof(g->name), "%s", t->name);
	t->running++;
	t->started = now_s();
	printf("started %s, pid %d\n", t->name, (int)pid);
	write_state();
}

/* Reaps ended apps and drops groups of which nothing runs; 1 if a group ended (a tile's RUNNING may change, and the
 * panel may be in front again). */
static int poll_apps(void)
{
	int changed = 0, status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		for (int i = 0; i < ngroups; i++)
			if (groups[i].pgid == pid && groups[i].leader) {
				groups[i].leader = 0;
				printf("%s: pid %d ended (status %d)\n", groups[i].name, (int)pid,
				       WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
			}
	if (pid < 0 && errno == ECHILD) /* no child left to wait for: every leader is reaped */
		for (int i = 0; i < ngroups; i++)
			groups[i].leader = 0;
	for (int i = 0; i < ngroups;) {
		if (groups[i].leader || group_runs(groups[i].pgid)) {
			i++;
			continue;
		}
		printf("%s: nothing of group %d runs\n", groups[i].name, (int)groups[i].pgid);
		if (groups[i].tile >= 0)
			tiles[groups[i].tile].running--;
		groups[i] = groups[--ngroups];
		changed = 1;
	}
	if (changed)
		write_state();
	return changed;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
	int list = 0, extra = 1, daemon = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-list") == 0)
			list = 1;
		else if (strcmp(argv[i], "-noextra") == 0)
			extra = 0;
		else if (strcmp(argv[i], "-daemon") == 0)
			daemon = 1;
		else if (strcmp(argv[i], "-conf") == 0 && i + 1 < argc)
			conf_dir = argv[++i];
		else if (strcmp(argv[i], "-state") == 0 && i + 1 < argc)
			state_file = argv[++i];
		else {
			fprintf(stderr, "usage: launch_panel [-list | -daemon] [-conf DIR] [-state FILE] [-noextra]\n");
			return 2;
		}
	}
	read_conf(extra);
	if (list) {
		for (int i = 0; i < ntiles; i++)
			printf("%d %d %s|%s|%s\n", tiles[i].row, tiles[i].order, tiles[i].name, tiles[i].exec, tiles[i].icon);
		return 0;
	}
	if (daemon) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("launch_panel: fork");
			return 1;
		}
		if (pid > 0)
			return 0; /* the starter returns; the panel goes on in the child */
		if (setsid() < 0)
			perror("launch_panel: setsid");
	}
	if (chdir("/") != 0)
		perror("launch_panel: chdir /");
	signal(SIGCHLD, SIG_DFL); /* children are waited for (an ignored SIGCHLD would reap them unseen) */
	keep_old_groups();

	screen_context_t ctx;
	screen_window_t win;
	if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0 || screen_create_window(&win, ctx) != 0) {
		perror("screen");
		return 1;
	}
	screen_display_t *displays;
	int count = 0, size[2] = { 1920, 1080 };
	screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count);
	displays = calloc((size_t)(count > 0 ? count : 1), sizeof(*displays));
	screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays);
	screen_get_display_property_iv(displays[0], SCREEN_PROPERTY_SIZE, size);
	free(displays);
	W = size[0];
	H = size[1];
	if (W < 1760 || H < 1000) {
		fprintf(stderr, "launch_panel: the display is %dx%d; at least 1760x1000 is needed\n", W, H);
		return 1;
	}
	load_icons();
	layout();
	int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE, format = SCREEN_FORMAT_RGBA8888, pos[2] = { 0, 0 };
	screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
	const char *id = "opticnerve-launcher";
	screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(id), id);
	if (screen_create_window_buffers(win, 2) != 0) {
		perror("buffers");
		return 1;
	}
	screen_buffer_t bufs[2];
	screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)bufs);
	int back = 0, dirty[4] = { 0, 0, W, H };
	screen_event_t ev;
	screen_create_event(&ev);
	write_state();
	printf("%d tiles; panel up\n", ntiles);

	int pressed = -1, last_buttons = 0, need_up = 0, changed = 1, closed = 0;
	while (!closed) {
		if (changed) {
			screen_get_buffer_property_pv(bufs[back], SCREEN_PROPERTY_POINTER, (void **)&fb);
			screen_get_buffer_property_iv(bufs[back], SCREEN_PROPERTY_STRIDE, &stride);
			draw();
			screen_post_window(win, bufs[back], 1, dirty, 0);
			back ^= 1;
			changed = 0;
		}
		if (poll_apps()) {
			changed = 1;
			need_up = 1; /* the panel may be in front again, maybe with a button held: wait for it to come up */
			pressed = lit = -1;
		}
		if (screen_get_event(ctx, ev, 500000000) != 0) { /* wakes every 0.5 s to look at the apps */
			if (errno == EINTR)
				continue;
			printf("screen_get_event: %s: ending\n", strerror(errno));
			break;
		}
		int type = SCREEN_EVENT_NONE;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_TYPE, &type);
		if (type != SCREEN_EVENT_POINTER)
			continue;
		int p[2] = { 0, 0 }, buttons = 0;
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_SOURCE_POSITION, p);
		screen_get_event_property_iv(ev, SCREEN_PROPERTY_BUTTONS, &buttons);
		int was_down = last_buttons & SCREEN_LEFT_MOUSE_BUTTON, down = buttons & SCREEN_LEFT_MOUSE_BUTTON;
		last_buttons = buttons;
		if (!down)
			need_up = 0;
		int under = -1; /* the tile under the pointer, -2 the close box */
		if (p[0] >= CLOSE_X && p[0] < W && p[1] >= 0 && p[1] < TITLE_H)
			under = -2;
		for (int i = 0; i < ntiles; i++)
			if (tiles[i].y >= 0 && p[0] >= tiles[i].x && p[0] < tiles[i].x + TILE_W && p[1] >= tiles[i].y &&
			    p[1] < tiles[i].y + TILE_H)
				under = i;
		if (down && !was_down && !need_up && pressed == -1 && under != -1) {
			pressed = under; /* only a press that starts on it */
			lit = under;
			changed = 1;
		} else if (down && pressed != -1) {
			int now_lit = under == pressed ? pressed : -1;
			if (now_lit != lit) {
				lit = now_lit;
				changed = 1;
			}
		} else if (!down && pressed != -1) {
			if (under == pressed) {
				if (pressed == -2) {
					printf("closed from the panel\n");
					closed = 1;
				} else if (now_s() - tiles[pressed].started < GUARD) {
					printf("%s: a click within %.0f s of its start: ignored\n", tiles[pressed].name, GUARD);
				} else {
					start_app(&tiles[pressed]);
					need_up = 1; /* the app may take the pointer before the button is seen up here */
				}
			}
			pressed = -1;
			lit = -1;
			changed = 1;
		}
	}
	screen_destroy_window(win);
	screen_destroy_context(ctx);
	return 0;
}
