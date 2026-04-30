#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ptr_arr {
  void *ptrs[512];
  size_t used_idx;
};

struct ptr_arr pa;

int ptr_add(struct ptr_arr *pa, void *p) {
  if (pa->used_idx >= 512) {
    return -1;
  }
  pa->ptrs[pa->used_idx] = p;
  pa->used_idx++;
  return 0;
}

#define SIZE 4096 * 6
void allocmems() {
  for (size_t i = 0; i < 15; i++) {
    char *p = malloc(SIZE);
    for (size_t j = 0; j < SIZE; j++) {
      p[j] = 'a';
    }
    ptr_add(&pa, p);
  }
}

void freemems() {
  for (size_t i = 0; i < pa.used_idx; i++) {
    free(pa.ptrs[i]);
  }
}

int main(int argc, char *argv[]) {
  fprintf(stderr, "Starting test...\n");
  allocmems();
  usleep(100 * 1000);
  sleep(2);
  freemems();
  sleep(5);
  fprintf(stderr, "Test finished.\n");

  return 0;
}
