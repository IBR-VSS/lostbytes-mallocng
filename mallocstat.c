#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>


#include "helper.h"
#include "meta.h"

#define PRINT(f, v) do { write_str(f ": ",1); write_hex((uintptr_t)(v),1); write_str("\n",1); } while(0);

// #undef assert
// #define assert_loc(f, l) #f ":" #l ":"
// #define assert(cond) do { if (cond) {} else { write_str(assert_loc(__FILE__, __LINE__),2); write_str(#cond,2); write_str("\n", 2); _exit(-1);}} while (0)

const uint16_t scs[] = {
    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   12,   15,
    18,   20,   25,   31,   36,   42,   50,   63,   72,   84,   102,  127,
    146,  170,  204,  255,  292,  340,  409,  511,  584,  682,  818,  1023,
    1169, 1364, 1637, 2047, 2340, 2730, 3276, 4095, 4680, 5460, 6552, 8191};

static uint64_t counter_us = 0;
#define TIMER_INTERVAL_US 1000000

static int fd_slots = -1;
static int fd_subholes = -1;
static int fd_pages = -1;

static struct timespec last_mallocstat;


void mallocstat(void);

static uintptr_t page_floor(uintptr_t p) { return p & ~4095UL; }

static uintptr_t page_ceil(uintptr_t p) { return (p + 4095) & ~4095UL; }

static bool is_page_aligned(uintptr_t p) { return p % 4096 == 0; }

static size_t head_size(uintptr_t slot_start) {
    uintptr_t ceil = page_ceil(slot_start);
    return ceil - slot_start;
}

static size_t tail_size(uintptr_t slot_end) {
    uintptr_t floor = page_floor(slot_end);
    return slot_end - floor;
}

static double time_delta(struct timespec *ts) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double seconds = (double)(now.tv_sec - ts->tv_sec) + 
        (double)(now.tv_nsec - ts->tv_nsec) / 1e9;

    *ts = now;

    return seconds;
}



static void *profiler_thread(void *arg) {
    int timer_interval = TIMER_INTERVAL_US;
    char *TIMER = getenv("MALLOCSTAT_TIMER_INTERVAL");
    if (TIMER)
        timer_interval = atoi(TIMER) * 1000; // in ms

    time_delta(&last_mallocstat);

    while (1) {
        if (MT && a_cas(&malloc_lock, 0, 1) != 0) {
            continue;
        }

        mallocstat();

        if (MT)
            __sync_lock_release(&malloc_lock, 0);

        usleep(timer_interval);
    }
    return NULL;
}

__attribute__((constructor)) 
static void start_malloc_profiler(void) {
    fd_slots = -1;
    if (char *MALLOCSTAT_SLOTS = getenv("MALLOCSTAT_SLOTS")) {
        fd_slots = open(MALLOCSTAT_SLOTS, O_CREAT | O_WRONLY | O_TRUNC, 0664);
    }

    char *fn_pages = getenv("MALLOCSTAT_PAGES");
    if (!fn_pages) fn_pages = "pages.csv";

    fd_pages = open(fn_pages, O_CREAT | O_WRONLY | O_TRUNC, 0664);
    assert(fd_pages != -1);

    pthread_t tid;
    if (pthread_create(&tid, NULL, profiler_thread, NULL) == 0) {
        pthread_detach(tid);
    } else {
        print_str("Failed to start profiler thread..\n");
    }
}

// __attribute__((destructor)) 
// static void library_exit_handler(void) {
//     if (MT && a_cas(&malloc_lock, 0, 1) != 0) {
//         return;
//     }
//     mallocstat();
//     if (MT)
//         __sync_lock_release(&malloc_lock, 0);
// }

static uint32_t sample_id = 0;

static uintptr_t page_vec_addr;
static size_t page_vec_len;
static unsigned char page_vec[1024*1024];

static int page_vec_mapped(uintptr_t addr) {
    uintptr_t off = addr - (uintptr_t) page_vec_addr;
    assert(addr >= (uintptr_t)page_vec_addr);
    assert(off <= page_vec_len);
    uintptr_t idx = off / 4096;
    return (page_vec[idx] & 1);
}

static void page_vec_ensure(uintptr_t addr, size_t len) {
    if (page_vec_addr <= addr
        && (addr+len <= page_vec_addr+page_vec_len))
        return;
    
    assert(len < sizeof(page_vec)*4096);
    page_vec_addr = page_floor(addr);
    page_vec_len  = page_ceil(addr+len)-page_vec_addr;
    int rc = mincore((void*)page_vec_addr, page_vec_len , page_vec);
    
    assert(rc == 0);
}

// Statistics vector.
struct hole_stat {
    size_t hole_count;
    
    size_t head_size;
    size_t head_size_freed;

    size_t body_size;
    size_t body_size_freed;
    
    size_t tail_size;
    size_t tail_size_freed;

    size_t subhole_size;
    size_t subhole_size_freed;
};

static void mallocstat_hole_stat_print(struct hole_stat *stat, int fd) {
#define PRINT_FIELD(name) do {                  \
        write_str(",", fd);                      \
        if (stat == NULL) {                      \
            write_str(#name, fd);                \
        } else {                                 \
            write_hex(stat->name, fd);           \
        }                                        \
    }while(0)

    PRINT_FIELD(hole_count);
    PRINT_FIELD(head_size);
    PRINT_FIELD(head_size_freed);
    PRINT_FIELD(body_size);
    PRINT_FIELD(body_size_freed);
    PRINT_FIELD(tail_size);
    PRINT_FIELD(tail_size_freed);
    PRINT_FIELD(subhole_size);
    PRINT_FIELD(subhole_size_freed);
#undef PRINT_FIELD
}

static void mallocstat_hole_stat_acc(struct hole_stat *acc, struct hole_stat *rhs) {
    acc->hole_count         += rhs->hole_count;
    acc->head_size          += rhs->head_size;
    acc->head_size_freed    += rhs->head_size_freed;
    acc->body_size          += rhs->body_size;
    acc->body_size_freed    += rhs->body_size_freed;
    acc->tail_size          += rhs->tail_size;
    acc->tail_size_freed    += rhs->tail_size_freed;
    acc->subhole_size       += rhs->subhole_size;
    acc->subhole_size_freed += rhs->subhole_size_freed;
}


struct hole_stat acc_merged_true, acc_merged_false;    

static void mallocstat_hole_callback(uintptr_t hole_start, size_t hole_len,
                                     int is_merged) {
    uintptr_t hole_end = hole_start + hole_len - 1;

    struct hole_stat stat = {0};
    stat.hole_count = 1;

    uintptr_t body_start = hole_start;
    uintptr_t body_end = hole_end;

    if (page_floor(hole_start) == page_floor(hole_end)
        && page_floor(hole_start) != hole_start) {
        stat.subhole_size = hole_len;
        stat.subhole_size_freed = page_vec_mapped(hole_start) ? 0 : hole_len;
    } else {
        // There is a head
        if (!is_page_aligned(hole_start)) {
            stat.head_size = head_size(hole_start);
            stat.head_size_freed = page_vec_mapped(hole_start) ? 0 : stat.head_size;
            body_start += stat.head_size;
        }

        // There is a tail
        if (!is_page_aligned(hole_end)) {
            stat.tail_size = tail_size(hole_end);
            stat.tail_size_freed = page_vec_mapped(hole_end) ? 0 : stat.tail_size;
            body_end -= stat.tail_size;
        }

        stat.body_size = body_end - body_start;
        assert(body_end >= body_start);
        for (uintptr_t p = body_start; p != body_end; p += 4096) {
            if (!page_vec_mapped(p))
                stat.body_size_freed += 4096;
        }
    }

    // Write CSV
    if (fd_slots != -1) {
        write_int(counter_us, fd_slots);
    
        write_str(",", fd_slots);
        write_hex(hole_start, fd_slots);
        
        write_str(",", fd_slots);
        write_int(hole_len, fd_slots);
    
        write_str(",", fd_slots);
        write_int(is_merged, fd_slots);
        
        mallocstat_hole_stat_print(&stat, fd_slots);
    
        write_str("\n", fd_slots);
    }

    // Accumulate for fd_pages
    if (is_merged) {
        mallocstat_hole_stat_acc(&acc_merged_true, &stat);
    } else {
        mallocstat_hole_stat_acc(&acc_merged_false, &stat);
    }
}    

    
// 3. The Profiler
void mallocstat(void) {
    struct timespec start;
    double delta = time_delta(&last_mallocstat);
    start = last_mallocstat;
    counter_us += (int)(delta * 1000000);
    
    struct meta_area *ma = ctx.meta_area_head;

    if (sample_id == 0) {
        // CSV Header
        if (fd_slots != -1) {
            write_str("counter_us,hole_start,hole_len,is_merged",
                      fd_slots);
            mallocstat_hole_stat_print(NULL, fd_slots);
            write_str("\n", fd_slots);
        }

        if (fd_subholes != -1) {
            write_str("counter_us,pageaddr,start,len,len_freed,merged\n",
                      fd_subholes);
        }

        // fd-pages is always written
        write_str("counter_us,n_phys,is_merged,sample_ns", fd_pages);
        mallocstat_hole_stat_print(NULL, fd_pages);
        write_str("\n", fd_pages);
    }

    size_t n_phys = 0;
    while (ma != NULL) {
        for (int i = 0; i < ma->nslots; i++) {
            struct meta *m = &ma->slots[i];
            if (!m->mem) continue;

            // Combine masks to find all unused (ready + quarantined) slots
            uint32_t unused_mask = m->avail_mask | m->freed_mask;

            // Calculate the actual byte size of the slots
            size_t slot_size;
            if (m->sizeclass == 63) {
                slot_size = m->maplen * 4096; // Direct mmap
            } else {
                slot_size =
                    scs[m->sizeclass] * 16; // Standard size class (UNIT = 16)
            }
            
            if (m->maplen > 0) {
                uintptr_t groupaddr = page_floor((uintptr_t)m->mem);
                assert(groupaddr % 4096 == 0);
                
                page_vec_ensure(groupaddr, m->maplen*4096);
            } else {
                // This is a subgroup. Find the parent group for
                // getting the paging information.
                struct meta *pm = m;
                while (!pm->maplen) {
                    // size_t stride = get_stride(pm);
                    pm = get_meta((void*)pm->mem);
                }

                assert(pm->maplen > 0);
                page_vec_ensure(page_floor((uintptr_t)pm->mem),
                                pm->maplen*4096);
            }

            // Loop through every slot that exists in this group
            for (int j = 0; j <= m->last_idx; j++) {

                // 1. Is the slot empty according to mallocng?
                int is_empty = (unused_mask & (1U << j)) != 0;
                if (!is_empty) {
                    continue;
                }

                uintptr_t slot_addr = (uintptr_t)m->mem->storage + (j * slot_size);
                //#ifdef TEST
#if 0
                print_str("[STAT] slot addr: ");
                print_hex(slot_addr);
                print_str("\n");
#endif
                mallocstat_hole_callback(slot_addr, slot_size, 0);
                mallocstat_hole_report(slot_addr, slot_size);
            }

            for (size_t j = 0; j < m->maplen; j++) {
                uintptr_t curr_groupaddr = (uintptr_t)m->mem + (j * 4096);
                int is_resident = page_vec_mapped(curr_groupaddr);
                if (is_resident) {
                    n_phys++;
                }
            }
            mallocstat_hole_iterate(mallocstat_hole_callback);
            mallocstat_hole_clear();
        }
        ma = ma->next;
    }

    mallocstat_hole_reset();
        
    double ns = time_delta(&start) * 1e9;

    for (int is_merged = 0; is_merged < 2; is_merged++ ){
        write_int(counter_us, fd_pages);
        write_str(",", fd_pages);
        write_int(n_phys, fd_pages);
        write_str(",", fd_pages);
        write_int(is_merged, fd_pages);
        write_str(",", fd_pages);
        write_int((int)ns, fd_pages);

        mallocstat_hole_stat_print(is_merged
                                   ? &acc_merged_true
                                   : &acc_merged_false,
                                   fd_pages);
        write_str("\n", fd_pages);
    }

    struct hole_stat zero = {0};
    acc_merged_true = acc_merged_false = zero;
    

    sample_id++;
}
