#include "buddy.h"
#include <stddef.h>
#include <stdlib.h>

#define NULL ((void *)0)

#define MAXR 16
#define PAGE_SIZE 4096

static char *base = NULL;
static int total_pages = 0;
static int max_rank = 0;  // <= MAXR

// free bitmap per rank (1..max_rank). Each entry indicates if that block is free.
// Indexing: for rank r, block size in pages is (1 << (r-1)), and block number b
// starts at page index b * size.
static unsigned char *free_bits[MAXR + 1];
static int blocks_per_rank[MAXR + 1];
static int free_count[MAXR + 1];

// Record allocated block rank by head page index; 0 means not allocated.
static unsigned char *alloc_rank_at = NULL;  // length = total_pages

static inline int pages_per_block(int r) { return 1 << (r - 1); }
static inline int in_range_page_index(long idx) {
    return (idx >= 0 && idx < total_pages);
}

static int compute_max_rank(int pgcount) {
    // max rank cannot exceed MAXR, and must satisfy 2^(r-1) == pgcount
    // If pgcount is not power-of-two, pick the largest r such that 2^(r-1) <= pgcount
    int r;
    for (r = 1; r <= MAXR; ++r) {
        if (pages_per_block(r) == pgcount) return r;
    }
    // fallback: largest r with size <= pgcount
    r = 1;
    while (r < MAXR && pages_per_block(r + 1) <= pgcount) r++;
    return r;
}

static inline long page_index_from_ptr(void *p) {
    if (p == NULL || base == NULL) return -1;
    long off = (long)((char *)p - base);
    if (off < 0) return -1;
    if (off % PAGE_SIZE != 0) return -1;
    long idx = off / PAGE_SIZE;
    if (!in_range_page_index(idx)) return -1;
    return idx;
}

int init_page(void *p, int pgcount) {
    if (p == NULL || pgcount <= 0) return -EINVAL;
    base = (char *)p;
    total_pages = pgcount;
    max_rank = compute_max_rank(pgcount);

    // initialize per-rank arrays
    for (int r = 1; r <= MAXR; ++r) {
        free_bits[r] = NULL;
        blocks_per_rank[r] = 0;
        free_count[r] = 0;
    }

    for (int r = 1; r <= max_rank; ++r) {
        int size = pages_per_block(r);
        int blocks = total_pages / size;
        blocks_per_rank[r] = blocks;
        if (blocks > 0) {
            free_bits[r] = (unsigned char *)calloc((size_t)blocks, sizeof(unsigned char));
            if (free_bits[r] == NULL) return -EINVAL; // allocation failure treated as invalid
        }
    }

    // allocation map
    alloc_rank_at = (unsigned char *)calloc((size_t)total_pages, sizeof(unsigned char));
    if (alloc_rank_at == NULL) return -EINVAL;

    // initially, one big block at max_rank is free
    if (blocks_per_rank[max_rank] > 0 && free_bits[max_rank]) {
        free_bits[max_rank][0] = 1;
        free_count[max_rank] = 1;
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAXR) return ERR_PTR(-EINVAL);
    if (base == NULL) return ERR_PTR(-ENOSPC);
    if (rank > max_rank) return ERR_PTR(-ENOSPC);

    // find a free block from rank..max_rank
    int src_rank = -1;
    int src_b = -1;
    for (int r = rank; r <= max_rank; ++r) {
        if (free_count[r] == 0) continue;
        // find first free block in this rank
        int blocks = blocks_per_rank[r];
        unsigned char *fb = free_bits[r];
        for (int b = 0; b < blocks; ++b) {
            if (fb[b]) { src_rank = r; src_b = b; break; }
        }
        if (src_rank != -1) break;
    }
    if (src_rank == -1) return ERR_PTR(-ENOSPC);

    // remove selected free block
    free_bits[src_rank][src_b] = 0;
    free_count[src_rank]--;

    // starting page index of the block
    int start_page = src_b * pages_per_block(src_rank);

    // split down to requested rank, placing the right buddy at each step into free list
    for (int r = src_rank; r > rank; --r) {
        int child_size = pages_per_block(r - 1);
        int right_start = start_page + child_size;
        int right_b = right_start / child_size; // block number at (r-1)
        free_bits[r - 1][right_b] = 1;
        free_count[r - 1]++;
        // continue with left child (start_page unchanged)
    }

    // mark allocated at head
    alloc_rank_at[start_page] = (unsigned char)rank;

    return (void *)(base + (long)start_page * PAGE_SIZE);
}

int return_pages(void *p) {
    if (p == NULL) return -EINVAL;
    long idx = page_index_from_ptr(p);
    if (idx < 0) return -EINVAL;
    if (alloc_rank_at == NULL) return -EINVAL;
    int r = (int)alloc_rank_at[idx];
    if (r < 1 || r > MAXR) return -EINVAL;

    // clear allocation mark for the head
    alloc_rank_at[idx] = 0;

    int b = (int)(idx / pages_per_block(r));

    // try to coalesce upwards while buddy at same rank is free
    while (r < max_rank) {
        int buddy_b = b ^ 1; // adjacent block number
        if (free_bits[r] && buddy_b < blocks_per_rank[r] && free_bits[r][buddy_b]) {
            // consume buddy
            free_bits[r][buddy_b] = 0;
            free_count[r]--;
            // move to parent block
            b = b >> 1;
            r++;
        } else {
            break;
        }
    }

    // insert the (possibly coalesced) block into free list
    free_bits[r][b] = 1;
    free_count[r]++;

    return OK;
}

int query_ranks(void *p) {
    long idx = page_index_from_ptr(p);
    if (idx < 0) return -EINVAL;

    // allocated head page
    int r = (int)alloc_rank_at[idx];
    if (r >= 1 && r <= MAXR) return r;

    // search largest free block containing this page
    for (int rr = max_rank; rr >= 1; --rr) {
        int s = pages_per_block(rr);
        int b = (int)(idx / s);
        if (free_bits[rr] && b < blocks_per_rank[rr] && free_bits[rr][b]) {
            return rr;
        }
    }
    return -EINVAL;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAXR) return -EINVAL;
    if (rank > max_rank) return 0;
    return free_count[rank];
}
