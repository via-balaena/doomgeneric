#pragma once
#include "stddef_shim.h"
typedef struct _TOCK_FILE FILE;
extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    snprintf(char *b, size_t n, const char *fmt, ...);
int    vsnprintf(char *b, size_t n, const char *fmt, va_list ap);
int    sscanf(const char *s, const char *fmt, ...);
int    puts(const char *s);
int    putchar(int c);
int    fputc(int c, FILE *f);
int    fflush(FILE *f);
int    fileno(FILE *f);
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    remove(const char *path);
int    rename(const char *a, const char *b);
