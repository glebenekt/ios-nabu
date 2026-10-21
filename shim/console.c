/*
 * Minimal text console on the ABL splash framebuffer. With the MMU off
 * every access is Device memory, so stay aligned (-mstrict-align).
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

volatile uint32_t *con_fb = (volatile uint32_t *)FB_BASE;
static uint32_t con_bg;
static int row;

void con_clear(uint32_t bg)
{
	for (int y = 0; y < FB_H; y++) {
		volatile uint32_t *line = con_fb + y * (FB_STRIDE / 4);
		for (int x = 0; x < FB_W; x++)
			line[x] = bg;
	}
	con_bg = bg;
	row = 0;
}

static void put_glyph(int x0, int y0, char ch, uint32_t fg, int scale)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	if (scale < 1)
		scale = 1;
	const unsigned char *g = font8x16[ch - 0x20];
	int w = 8 * scale, h = 16 * scale;
	if (x0 < 0 || y0 < 0 || x0 + w > FB_W || y0 + h > FB_H)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			uint32_t c = (g[gy] & (0x80 >> gx)) ? fg : con_bg;
			for (int sy = 0; sy < scale; sy++) {
				volatile uint32_t *p = con_fb + (y0 + gy * scale + sy) * (FB_STRIDE / 4)
						       + x0 + gx * scale;
				for (int sx = 0; sx < scale; sx++)
					p[sx] = c;
			}
		}
}

void con_text_at(int x, int y, const char *s, uint32_t fg, int scale)
{
	while (*s) {
		put_glyph(x, y, *s++, fg, scale);
		x += 8 * scale;
	}
}

static void put_char(int col, char ch, uint32_t fg)
{
	put_glyph(MARGIN + col * CH_W, MARGIN + row * CH_H, ch, fg, SCALE);
}

void con_banner(const char *stage)
{
	con_clear(BG_DARK);

	/* Top accent line. */
	for (int y = 0; y < 10; y++) {
		volatile uint32_t *line = con_fb + y * (FB_STRIDE / 4);
		for (int x = 0; x < FB_W; x++)
			line[x] = ACCENT;
	}

	con_text_at(80, 80, "iPadOS-nabu", WHITE, 7);
	con_text_at(80, 240, "Xiaomi Pad 5 (nabu)  -  SM8150-AC", MUTED, 3);
	con_text_at(80, 320, "iPadOS 16.6.1 (20G81)  /  XNU", MUTED, 3);
	con_text_at(80, 400, "By mytiantian001", ACCENT, 3);

	for (int x = 80; x < 1200; x++)
		con_fb[480 * (FB_STRIDE / 4) + x] = ACCENT;

	con_text_at(80, 540, "stage", ACCENT, 3);
	con_text_at(80, 620, stage, WHITE, 3);

	/* Leave the old diagnostic lines below the splash. */
	row = 18;
}

void
con_progress(int step, int total)
{
	const int x0 = 80, x1 = 1200, y = 700, h = 14;
	int fill;

	if (total <= 0) {
		total = 1;
	}
	if (step < 0) {
		step = 0;
	}
	if (step > total) {
		step = total;
	}
	fill = (x1 - x0) * step / total;

	for (int yy = 0; yy < h; yy++) {
		volatile uint32_t *line = con_fb + (y + yy) * (FB_STRIDE / 4);
		for (int x = x0; x < x1; x++) {
			line[x] = 0xff151a24;
		}
		for (int x = x0; x < x0 + fill; x++) {
			line[x] = ACCENT;
		}
		int px = x0 + fill;
		for (int d = -2; d <= 2; d++) {
			if (px + d >= x0 && px + d < x1) {
				line[px + d] = WHITE;
			}
		}
	}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
