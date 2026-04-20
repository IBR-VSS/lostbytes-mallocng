#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "helper.h"
#include "meta.h"

const uint16_t scs[] = {
    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   12,   15,
    18,   20,   25,   31,   36,   42,   50,   63,   72,   84,   102,  127,
    146,  170,  204,  255,  292,  340,  409,  511,  584,  682,  818,  1023,
    1169, 1364, 1637, 2047, 2340, 2730, 3276, 4095, 4680, 5460, 6552, 8191};

static uint64_t counter_ms = 0;
#define TIMER_INTERVAL_US 1000000

static int fd_slots = -1;
static int fd_pages = -1;

void mallocstat(void);

void *profiler_thread(void *arg) {
  while (1) {
    usleep(TIMER_INTERVAL_US);

    if (MT && a_cas(&malloc_lock, 0, 1) != 0) {
      continue;
    }

    mallocstat();

    if (MT)
      __sync_lock_release(&malloc_lock, 0);
  }
}

__attribute__((constructor)) void start_malloc_profiler(void) {
  fd_slots = open("slots.csv", O_CREAT | O_WRONLY | O_TRUNC, 0664);
  assert(fd_slots != -1);
  fd_pages = open("pages.csv", O_CREAT | O_WRONLY | O_TRUNC, 0664);
  assert(fd_pages != -1);

  pthread_t tid;
  if (pthread_create(&tid, NULL, profiler_thread, NULL) == 0) {
    pthread_detach(tid);
  } else {
    print_str("Failed to start profiler thread..\n");
  }
}

static uint32_t sample_id = 0;

// 3. The Profiler
void mallocstat(void) {
  counter_ms += TIMER_INTERVAL_US / 1000;
  struct meta_area *ma = ctx.meta_area_head;

  if (sample_id == 0) {
    // CSV Header
    write_str("counter_ms,groupaddr,slotidx,slotsize,pageaddr,status\n",
              fd_slots);
    write_str("counter_ms,n_phys\n", fd_pages);
  }

  size_t n_phys = 0;
  while (ma != NULL) {
    for (int i = 0; i < ma->nslots; i++) {
      struct meta *m = &ma->slots[i];

      // Ignore subgroups
      if (m->maplen == 0)
        continue;

      uintptr_t groupaddr = (uintptr_t)m->mem;
      assert(groupaddr % 4096 == 0);

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
        if (!is_empty) {
          continue;
        }

        // 2. Find the slot's address and align it to 4KB for mincore
        uintptr_t slot_addr = groupaddr + (j * slot_size);
        uintptr_t page_addr = slot_addr & ~4095UL;

        // 3. Ask the kernel if this page is backed by physical RAM
        unsigned char vec;
        int is_resident = 0;
        if (mincore((void *)page_addr, 4096, &vec) == 0) {
          is_resident = vec & 1;
        }

        // Write CSV
        write_int(counter_ms, fd_slots);
        write_str(",", fd_slots);
        write_hex(groupaddr, fd_slots);
        write_str(",", fd_slots);
        write_int(j, fd_slots);
        write_str(",", fd_slots);
        write_int(slot_size, fd_slots);
        write_str(",", fd_slots);
        write_hex(page_addr, fd_slots);
        write_str(",", fd_slots);

        if (is_empty && is_resident) {
          write_str("WASTED\n", fd_slots);
        } else if (!is_empty && is_resident) {
          write_str("ACTIVE\n", fd_slots);
        } else if (is_empty && !is_resident) {
          write_str("IDLE\n", fd_slots);
        } else if (!is_empty && !is_resident) {
          write_str("SWAPPED\n", fd_slots);
        }
      }

      // TODO: Also measure total phys page mapped

      for (size_t j = 0; j < m->maplen; j++) {
        void *curr_groupaddr = (char *)m->mem + (j * 4096);
        unsigned char vec;
        int is_resident = 0;
        assert(mincore(curr_groupaddr, 4096, &vec) == 0);
        is_resident = vec & 1;

        if (is_resident) {
          n_phys++;
        }
      }
    }
    ma = ma->next;
  }
  write_int(counter_ms, fd_pages);
  write_str(",", fd_pages);
  write_int(n_phys, fd_pages);
  write_str("\n", fd_pages);
  sample_id++;
}
