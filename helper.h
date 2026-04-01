#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>
#include <stdio.h>

void print_str(const char *s);
void print_int(uint32_t n);
void print_hex(uintptr_t p);

void write_str(const char *s, FILE *f);
void write_int(uint32_t n, FILE *f);
void write_hex(uintptr_t p, FILE *f);

#endif // HELPER_H
