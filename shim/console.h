#pragma once
#include <stdint.h>

#define FB_BASE		0x9c000000UL	/* reserved-memory cont_splash_region */
#define FB_W		1600
#define FB_H		2560
#define FB_STRIDE	(FB_W * 4)	/* a8r8g8b8, matches mainline simplefb */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

/* ios-nabu boot splash */
#define BG_DARK	0xff0b0d12U
#define ACCENT	0xff5ad1ffU
#define MUTED	0xff8a8f98U
#define ERROR_RED 0xffff3b30U

extern volatile uint32_t *con_fb;

void con_clear(uint32_t bg);
void con_banner(const char *stage);
void con_progress(int step, int total);
void con_text_at(int x, int y, const char *s, uint32_t fg, int scale);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
