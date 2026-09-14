#pragma once
#define O_RDONLY 0
int open(const char *path, int flags, ...);
int close(int fd);
