#include <vitasdk.h>
#include <vitashark.h>
#include <vitaGL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "font_utils.h"

int dbg_y = 8;
uint32_t *frame_buf;

void vgl_debugger_draw_character(int character, int x, int y) {
	for (int yy = 0; yy < 10; yy++) {
		int xDisplacement = x;
		int yDisplacement = (y + (yy<<1)) * SCREEN_W;
		uint32_t* screenPos = frame_buf + xDisplacement + yDisplacement;

		uint8_t charPos = font[character * 10 + yy];
		for (int xx = 7; xx >= 2; xx--) {
			uint32_t clr = ((charPos >> xx) & 1) ? 0xFFFFFFFF : 0x00000000;
			*(screenPos) = clr;
			*(screenPos + 1) = clr;
			*(screenPos + SCREEN_W) = clr;
			*(screenPos + SCREEN_W + 1) = clr;			
			screenPos += 2;
		}
	}
}

void vgl_debugger_draw_string(int x, int y, const char *str) {
	for (size_t i = 0; i < strlen(str); i++)
		vgl_debugger_draw_character(str[i], x + i * 12, y);
}

void vgl_debugger_draw_string_format(int x, int y, const char *format, ...) {
	char str[512] = { 0 };
	va_list va;

	va_start(va, format);
	vsnprintf(str, 512, format, va);
	va_end(va);

	for (char* text = strtok(str, "\n"); text != NULL; text = strtok(NULL, "\n"), y += 20)
		vgl_debugger_draw_string(x, y, text);
}

void vgl_debugger_draw_mem_usage(const char *str, vglMemType type) {
	uint32_t tot = vgl_mem_get_total_space(type) / (1024 * 1024);
	uint32_t used = tot - (vgl_mem_get_free_space(type) / (1024 * 1024));
	float ratio = ((float)used / (float)tot) * 100.0f;
	vgl_debugger_draw_string_format(5, dbg_y, "%s: %luMBs / %luMBs (%.2f%%)", str, used, tot, ratio);
	dbg_y += 20;
}

void mem_profiler(void *framebuf) {
	frame_buf = (uint32_t *)framebuf;
	dbg_y = 8;
	vgl_debugger_draw_mem_usage("RAM Usage", VGL_MEM_RAM);
	vgl_debugger_draw_mem_usage("VRAM Usage", VGL_MEM_VRAM);
	vgl_debugger_draw_mem_usage("Phycont RAM Usage", VGL_MEM_SLOW);
	vgl_debugger_draw_mem_usage("CDLG RAM Usage", VGL_MEM_BUDGET);
}
