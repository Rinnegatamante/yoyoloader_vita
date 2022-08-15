#ifndef __SCE_LIBC_BRIDGE_H__
#define __SCE_LIBC_BRIDGE_H__

#include <stdio.h>

extern void *sceLibcBridge__ZdaPv;
extern void *sceLibcBridge__ZdlPv;
extern void *sceLibcBridge__Znaj;
extern void *sceLibcBridge__Znwj;

extern void *sceLibcBridge___cxa_guard_acquire;
extern void *sceLibcBridge___cxa_guard_release;

int sceLibcBridge_rand(void);
void sceLibcBridge_srand(unsigned int seed);

void sceLibcBridge_qsort(void *base, size_t num, size_t size, int (* compar)(const void *,const void *));

int sceLibcBridge_sscanf(const char *s, const char *format, ...);
int sceLibcBridge_fscanf(FILE *stream, char *fmt, ...);

void *sceLibcBridge_calloc(size_t num, size_t size);
void sceLibcBridge_free(void *ptr);
void *sceLibcBridge_malloc(size_t size);
void *sceLibcBridge_realloc(void *ptr, size_t size);
void *sceLibcBridge_memalign(size_t alignment, size_t size);

FILE *sceLibcBridge_fopen(const char *filename, const char *mode);
int sceLibcBridge_fclose(FILE *stream);
size_t sceLibcBridge_fread(void *ptr, size_t size, size_t count, FILE *stream);
int sceLibcBridge_fseek(FILE *stream, long int offset, int origin);
long int sceLibcBridge_ftell(FILE *stream);
size_t sceLibcBridge_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int sceLibcBridge_ferror(FILE *stream);
int sceLibcBridge_feof(FILE *stream);

int sceLibcBridge_fprintf( FILE * stream, const char * format, ... );
int sceLibcBridge_fputc(int ch, FILE *stream);

int sceLibcBridge_setjmp(jmp_buf env);
void sceLibcBridge_longjmp(jmp_buf env, int val);

int sceLibcBridge_getc(FILE *stream);
int sceLibcBridge_ungetc(int c, FILE *stream);
int sceLibcBridge_vfprintf(FILE *stream, const char *format, va_list arg);
char *sceLibcBridge_fgets(char *s, int len, FILE *stream);
int sceLibcBridge_fflush(FILE *stream);
int sceLibcBridge_fputs(char *s, FILE *stream);
int sceLibcBridge_fgetc(FILE *stream);

int sceLibcBridge_puts(const char *s);
int sceLibcBridge_putc(int c, FILE *stream);
wint_t sceLibcBridge_putwc(wchar_t wc, FILE* stream);
wint_t sceLibcBridge_getwc (FILE* stream);
int sceLibcBridge_ungetc(int c, FILE *stream);
wint_t sceLibcBridge_ungetwc (wint_t wc, FILE* stream);

#endif
