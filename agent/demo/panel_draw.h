/* panel_draw.h: the font and the drawing helpers shared by the demo panel and the launcher panel. Included once, by
 * one .c file each (the definitions are static). The including file defines _QNX_SOURCE and includes
 * <stddef.h>/<string.h> first.
 *
 * Text is drawn with anti-aliased Roboto glyphs, rendered from the font file on the build host by
 * host/tools/make_panel_font.py into panel_font_roboto.h, so the panels need no font library on the target and draw
 * the same pixels every time. Roboto is a font of the QNX quick-start image's own, so the panels match the image's
 * look, and it read best by OCR of the fonts tried (Inter, and the 5x7 font used before). */
#ifndef PANEL_DRAW_H
#define PANEL_DRAW_H

struct pf_glyph {
	short adv, x, y, w, h; /* advance; ink box from the pen (x) and from the top of the capitals (y) */
	unsigned off;          /* the ink's first byte in PF_ALPHA, w x h coverage values row by row */
};
struct pf_size {
	int scale;
	const struct pf_glyph *g; /* one per PF_CHARS */
};
#include "panel_font_roboto.h"

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

/* The glyphs for `scale`: the size whose capitals are 7 x scale pixels high; for a scale not rendered, the largest
 * rendered one below it (the smallest for a scale below that). The largest size has ink for digits and minus only. */
static const struct pf_glyph *glyphs(int scale)
{
	const struct pf_glyph *g = PF_SIZES[0].g;
	for (size_t i = 0; i < sizeof(PF_SIZES) / sizeof(PF_SIZES[0]); i++)
		if (PF_SIZES[i].scale <= scale)
			g = PF_SIZES[i].g;
	return g;
}

static const struct pf_glyph *glyph(const struct pf_glyph *g, char c)
{
	const char *at = c ? strchr(PF_CHARS, c) : NULL;
	return &g[at ? at - PF_CHARS : 0]; /* unknown characters as space */
}

/* Width in pixels of s at `scale`, from the first pen position to the last letter's advance. */
static int text_width(int scale, const char *s)
{
	const struct pf_glyph *g = glyphs(scale);
	int w = 0;
	for (; *s; s++)
		w += glyph(g, *s)->adv;
	return w;
}

/* Draws text with the top of its capitals at y and its first pen position at x, the capitals 7 x scale pixels high,
 * blending the glyphs' coverage over what is already there. */
static void text(int x, int y, int scale, const char *s, unsigned rgb)
{
	const struct pf_glyph *g = glyphs(scale);
	const unsigned c[3] = { rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff }; /* B, G, R as in memory */
	for (; *s; s++) {
		const struct pf_glyph *q = glyph(g, *s);
		const unsigned char *a = PF_ALPHA + q->off;
		for (int j = 0; j < q->h; j++) {
			int py = y + q->y + j;
			if (py < 0 || py >= H)
				continue;
			for (int i = 0; i < q->w; i++) {
				int px = x + q->x + i;
				unsigned k = a[j * q->w + i];
				if (!k || px < 0 || px >= W)
					continue;
				unsigned char *p = fb + (size_t)py * stride + (size_t)px * 4;
				for (int n = 0; n < 3; n++)
					p[n] = (unsigned char)((c[n] * k + p[n] * (255 - k) + 127) / 255);
				p[3] = 0xff;
			}
		}
		x += q->adv;
	}
}

#endif
