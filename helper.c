#include "helper.h"
#include <stdint.h>
#include <unistd.h>

#define STDOUT 1
#define STDERR 2

void print_str(const char *s) {
    int len = 0;
    while (s[len])
        len++;
    write(STDOUT, s, len);
}

void print_int(uint32_t n) {
    char buf[12];
    int i = 11;
    if (n == 0)
        buf[i--] = '0';
    while (n > 0 && i >= 0) {
        buf[i--] = (n % 10) + '0';
        n /= 10;
    }
    write(STDOUT, &buf[i + 1], 11 - i);
}

void print_hex(uintptr_t p) {
    char buf[20];
    int i = 19;
    if (p == 0)
        buf[i--] = '0';
    while (p > 0 && i >= 0) {
        int nibble = p % 16;
        buf[i--] = (nibble < 10) ? (nibble + '0') : (nibble - 10 + 'a');
        p /= 16;
    }
    buf[i--] = 'x';
    buf[i--] = '0';
    write(STDOUT, &buf[i + 1], 19 - i);
}

void write_str(const char *s, int fd) {
    int len = 0;
    while (s[len])
        len++;
    write(fd, s, len);
}

void write_int(uint32_t n, int fd) {
    char buf[12];
    int i = 11;
    if (n == 0)
        buf[i--] = '0';
    while (n > 0 && i >= 0) {
        buf[i--] = (n % 10) + '0';
        n /= 10;
    }
    write(fd, &buf[i + 1], 11 - i);
}

void write_hex(uintptr_t p, int fd) {
    char buf[20];
    int i = 19;
    if (p == 0)
        buf[i--] = '0';
    while (p > 0 && i >= 0) {
        int nibble = p % 16;
        buf[i--] = (nibble < 10) ? (nibble + '0') : (nibble - 10 + 'a');
        p /= 16;
    }
    buf[i--] = 'x';
    buf[i--] = '0';
    write(fd, &buf[i + 1], 19 - i);
}
