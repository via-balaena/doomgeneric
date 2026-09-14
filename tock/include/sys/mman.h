#pragma once
#include "../stddef_shim.h"
#define PROT_READ  1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off);
int munmap(void *addr, size_t len);
