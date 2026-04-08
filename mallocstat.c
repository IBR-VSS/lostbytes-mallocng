#include <assert.h>
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "helper.h"
#include "meta.h"

const uint16_t scs[] = {
    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   12,   15,
    18,   20,   25,   31,   36,   42,   50,   63,   72,   84,   102,  127,
    146,  170,  204,  255,  292,  340,  409,  511,  584,  682,  818,  1023,
    1169, 1364, 1637, 2047, 2340, 2730, 3276, 4095, 4680, 5460, 6552, 8191};

static uint64_t counter_ms = 0;
#define TIMER_INTERVAL_US 100000

static int fd = -1;

void mallocstat(void);

void profiler_interrupt(int sig) {
  if (MT && a_cas(&malloc_lock, 0, 1) != 0) {
    print_str("No sample...\n");
    return;
  }

  mallocstat();

  if (MT)
    __sync_lock_release(&malloc_lock, 0);
}

__attribute__((constructor)) void start_malloc_profiler(void) {
  fd = open("buffbloat.csv", O_CREAT | O_WRONLY | O_TRUNC, 0664);
  assert(fd != -1);

  struct sigaction sa = {0};
  sa.sa_handler = profiler_interrupt;
  sigaction(SIGALRM, &sa, NULL);

  struct itimerval timer = {0};
  timer.it_value.tv_usec = TIMER_INTERVAL_US;
  timer.it_interval.tv_usec = TIMER_INTERVAL_US;
  setitimer(ITIMER_REAL, &timer, NULL);
}

static uint32_t sample_id = 0;

// 3. The Profiler
void mallocstat(void) {
  counter_ms += TIMER_INTERVAL_US / 1000;
  struct meta_area *ma = ctx.meta_area_head;

  if (sample_id == 0) {
    // CSV Header
    write_str("counter_ms,groupaddr,slotidx,slotsize,status\n", fd);
  }

  while (ma != NULL) {
    for (int i = 0; i < ma->nslots; i++) {
      struct meta *m = &ma->slots[i];

      if (m->mem == NULL)
        continue;

      // Combine masks to find all unused (ready + quarantined) slots
      uint32_t unused_mask = m->avail_mask | m->freed_mask;

      // Calculate the actual byte size of the slots
      size_t slot_size;
      if (m->sizeclass == 63) {
        slot_size = m->maplen * 4096; // Direct mmap
      } else {
        slot_size = scs[m->sizeclass] * 16; // Standard size class (UNIT = 16)
      }

      // Loop through every slot that exists in this group
      for (int j = 0; j <= m->last_idx; j++) {

        // 1. Is the slot empty according to mallocng?
        int is_empty = (unused_mask & (1U << j)) != 0;

        // 2. Find the slot's address and align it to 4KB for mincore
        uintptr_t slot_addr = (uintptr_t)m->mem + (j * slot_size);
        uintptr_t page_addr = slot_addr & ~4095UL;

        // 3. Ask the kernel if this page is backed by physical RAM
        unsigned char vec;
        int is_resident = 0;
        if (mincore((void *)page_addr, 4096, &vec) == 0) {
          is_resident = vec & 1;
        }

        // Write CSV
        write_int(counter_ms, fd);
        write_str(",", fd);
        write_hex((uintptr_t)m->mem, fd);
        write_str(",", fd);
        write_int(j, fd);
        write_str(",", fd);
        write_int(slot_size, fd);
        write_str(",", fd);

        if (is_empty && is_resident) {
          write_str("WASTED\n", fd);
        } else if (!is_empty && is_resident) {
          write_str("ACTIVE\n", fd);
        } else if (is_empty && !is_resident) {
          write_str("IDLE\n", fd);
        } else if (!is_empty && !is_resident) {
          write_str("SWAPPED\n", fd);
        }
      }
    }
    ma = ma->next;
  }
  sample_id++;
}
