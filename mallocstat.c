#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
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

uintptr_t page_floor(uintptr_t p) { return p & ~4095UL; }

uintptr_t page_ceil(uintptr_t p) { return (p + 4095) & ~4095UL; }

bool is_page_aligned(uintptr_t p) { return p % 4096 == 0; }

size_t head_size(uintptr_t slot_start) {
  uintptr_t ceil = page_ceil(slot_start);
  return ceil - slot_start;
}

size_t tail_size(uintptr_t slot_end) {
  uintptr_t floor = page_floor(slot_end);
  return slot_end - floor;
}

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

void get_hole_status(int is_empty, int is_resident) {
  if (is_resident == -1) {
    write_str("NONE", fd_slots);
    return;
  }
  if (is_empty && is_resident) {
    write_str("WASTED", fd_slots);
  } else if (!is_empty && is_resident) {
    write_str("ACTIVE", fd_slots);
  } else if (is_empty && !is_resident) {
    write_str("IDLE", fd_slots);
  } else if (!is_empty && !is_resident) {
    write_str("SWAPPED", fd_slots);
  }
}

static uint32_t sample_id = 0;

static unsigned char page_vec[4096];

static void mallocstat_hole_callback(uintptr_t hole_start, size_t hole_len) {
  uintptr_t hole_end = hole_start + hole_len - 1;

  uintptr_t head_end = page_ceil(hole_start);
  uintptr_t tail_start = page_floor(hole_end);

  size_t head_size_b;
  size_t tail_size_b;

  uintptr_t body_start = hole_start;
  uintptr_t body_end = hole_end;
  if (head_end >= tail_start ||
      (is_page_aligned(head_end) && is_page_aligned(hole_len))) {
    // Only body
    head_size_b = 0;
    tail_size_b = 0;
  } else {
    head_size_b = head_size(hole_start);
    tail_size_b = tail_size(hole_end);
    body_start = head_end;
    body_end = tail_start - 1;
  }

  // 3. Ask the kernel if this page is backed by physical RAM

  uintptr_t head_start = hole_start;
  uintptr_t tail_end = hole_end;

  size_t n_body_page = (page_floor(body_end) - page_floor(body_start)) / 4096;
  n_body_page += 1;

  int is_resident_h = -1;
  if (head_size_b != 0) {
    if (mincore((void *)page_floor(head_start), 4096, page_vec) == 0) {
      is_resident_h = page_vec[0] & 1;
    }
  }
  int n_phys_body = 0;
  size_t body_pglen = n_body_page * 4096;
  if (mincore((void *)page_floor(body_start), body_pglen, page_vec) == 0) {
    for (size_t pg_i = 0; pg_i < n_body_page; pg_i++) {
      n_phys_body += page_vec[pg_i] & 1;
    }
  }
  int is_resident_t = -1;
  if (tail_size_b != 0) {
    if (mincore((void *)page_floor(tail_end), 4096, page_vec) == 0) {
      is_resident_t = page_vec[0] & 1;
    }
  }

  // Write CSV
  write_int(counter_ms, fd_slots);
  write_str(",", fd_slots);
  // FIXME: Do we need this?!
  // write_hex(groupaddr, fd_slots);
  // write_str(",", fd_slots);

  // FIXME: Do we need this?
  // write_int(j, fd_slots);
  // write_str(",", fd_slots);
  write_int(hole_len, fd_slots);
  write_str(",", fd_slots);

  write_hex(body_start, fd_slots);
  write_str(",", fd_slots);
  write_int(n_phys_body, fd_slots);
  write_str(",", fd_slots);
  write_int(n_body_page, fd_slots);
  write_str(",", fd_slots);

  if (head_size_b == 0) {
    write_hex(0, fd_slots);
  } else {
    write_hex(page_floor(head_start), fd_slots);
  }
  write_str(",", fd_slots);

  // FIXME: What is this?
  // get_hole_status(is_empty, is_resident_h);
  // write_str(",", fd_slots);

  if (tail_size_b == 0) {
    write_hex(0, fd_slots);
  } else {
    write_hex(page_floor(tail_end), fd_slots);
  }
  write_str(",", fd_slots);
  // FIXME: What is this?
  // get_hole_status(is_empty, is_resident_t);
  write_str("\n", fd_slots);
}

// 3. The Profiler
void mallocstat(void) {
  counter_ms += TIMER_INTERVAL_US / 1000;
  struct meta_area *ma = ctx.meta_area_head;

  if (sample_id == 0) {
    // CSV Header
    write_str(
        "counter_ms,groupaddr,slotidx,slotsize,pageaddr_body,n_phys_body,n_"
        "virt_body,pageaddr_head,status_head,pageaddr_tail,status_tail\n",
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
        /* if (!is_empty) { */
        /*   continue; */
        /* } */

        // TODO: better differentiation, to which pages a slot belongs to
        // - Also mincore body_addr instead of page_addr, page_addr is not
        //   really correct

        // 2. Find the slot's address and align it to 4KB for mincore
        uintptr_t slot_addr = groupaddr + (j * slot_size);

        mallocstat_hole_report(slot_addr, slot_size);
      }

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

  mallocstat_hole_iterate(mallocstat_hole_callback);
  mallocstat_hole_reset();

  write_int(counter_ms, fd_pages);
  write_str(",", fd_pages);
  write_int(n_phys, fd_pages);
  write_str("\n", fd_pages);
  sample_id++;
}
