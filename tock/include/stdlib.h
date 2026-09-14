#pragma once
#include "stddef_shim.h"
void  *malloc(size_t n);
void  *calloc(size_t n, size_t sz);
void  *realloc(void *p, size_t n);
void   free(void *p);
int    atoi(const char *s);
double atof(const char *s);
long   strtol(const char *s, char **end, int base);
void   exit(int status);
char  *getenv(const char *name);
int    system(const char *cmd);
int    abs(int v);
