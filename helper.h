#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void print_str(const char *s);
void print_int(uint32_t n);
void print_hex(uintptr_t p);

void write_str(const char *s, int fd);
void write_int(uint32_t n, int fd);
void write_hex(uintptr_t p, int fd);

void mallocstat_hole_report(uintptr_t start, size_t length);
void mallocstat_hole_iterate(void (*callback)(uintptr_t, size_t));
void mallocstat_hole_reset();

#ifdef __cplusplus
}
#endif

#endif // HELPER_H

