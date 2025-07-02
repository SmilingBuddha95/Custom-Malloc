/*
 * validator.h - 6.172 Malloc Validator
 *
 * Validates a malloc/free/realloc implementation.
 *
 * Copyright (c) 2010, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#ifndef MM_VALIDATOR_H
#define MM_VALIDATOR_H

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./config.h"
#include "./mdriver.h"
#include "./memlib.h"

// Returns true if p is R_ALIGNMENT-byte aligned
#if (__WORDSIZE == 64 )
  #define IS_ALIGNED(p)  ((((uint64_t)(p)) % R_ALIGNMENT) == 0)
#else
  #define IS_ALIGNED(p)  ((((uint32_t)(p)) % R_ALIGNMENT) == 0)
#endif

// Range list data structure

// Records the extent of each block's payload
typedef struct range_t {
  char* lo;              // low payload address
  char* hi;              // high payload address
  struct range_t* next;  // next list element
} range_t;

// The following routines manipulate the range list, which keeps
// track of the extent of every allocated block payload. We use the
// range list to detect any overlapping allocated blocks.


// add_range - As directed by request opnum in trace tracenum,
// we've just called the student's malloc to allocate a block of
// size bytes at addr lo. After checking the block for correctness,
// we create a range struct for this block and add it to the range list.
static int add_range(const malloc_impl_t* impl, range_t** ranges, char* lo,
                     int size, int tracenum, int opnum) {
   char *hi = lo + size - 1;
   range_t *p = NULL;

  // You can use this as a buffer for writing messages with sprintf.
  // char msg[MAXLINE];

  assert(size > 0);

  // Payload addresses must be R_ALIGNMENT-byte aligned
  // TODO(project3): YOUR CODE HERE
  if (!IS_ALIGNED(lo)) {
    fprintf(stderr, "Error: Block at address %p is not aligned to %d bytes.\n", lo, R_ALIGNMENT);
    return 0; // Indicate an error
  }

  // The payload must lie within the extent of the heap
  // TODO(project3): YOUR CODE HERE
  if (lo < (char*)impl->heap_lo() || hi > (char*)impl->heap_hi()) {
    fprintf(stderr, "Error: Block with addresses [%p, %p] is outside heap bounds [%p, %p].\n",
            lo, hi, impl->heap_lo(), impl->heap_hi());
    return 0; // Indicate an error
  }
 

  // The payload must not overlap any other payloads
  // TODO(project3): YOUR CODE HERE
  for (range_t* curr = *ranges; curr != NULL; curr = curr->next) {
    if ((lo <= curr->hi && hi >= curr->lo) || (hi >= curr->lo && lo <= curr->hi)) {
      fprintf(stderr, "Error: Block with addresses [%p, %p] overlaps with existing block [%p, %p].\n",
              lo, hi, curr->lo, curr->hi);
      return 0; // Indicate an error
    }
  }
 

  // Everything looks OK, so remember the extent of this block by creating a
  // range struct and adding it the range list.
  // TODO(project3):  YOUR CODE HERE
  p = (range_t*)malloc(sizeof(range_t));
  if (p == NULL) {
    fprintf(stderr, "Error: Could not allocate memory for range struct.\n");
    return 0;
  }
  p->lo = lo;
  p->hi = hi;
  p->next = *ranges; // Insert at the head of the list
  *ranges = p;

  return 1;
}

// remove_range - Free the range record of block whose payload starts at lo
static void remove_range(range_t** ranges, char* lo) {
    range_t* curr = *ranges;
    range_t* prev = NULL;

    while (curr != NULL) {
        // Match based on range containment, not exact pointer equality
        if (lo >= curr->lo && lo <= curr->hi) {
            if (prev == NULL) {
                *ranges = curr->next;
            } else {
                prev->next = curr->next;
            }
            free(curr);
            return;
        }

        prev = curr;
        curr = curr->next;
    }

    // Optional: diagnostic for unmatched free
    fprintf(stderr, "[remove_range WARNING] Could not find block for ptr = %p\n", lo);
}




// clear_ranges - free all of the range records for a trace
static void clear_ranges(range_t** ranges) {
  range_t* p;
  range_t* pnext;

  for (p = *ranges; p != NULL; p = pnext) {
    pnext = p->next;
    free(p);
  }
  *ranges = NULL;
}

// eval_mm_valid - Check the malloc package for correctness
int eval_mm_valid(const malloc_impl_t* impl, trace_t* trace, int tracenum) {
  int i = 0;
  int index = 0;
  int size = 0;
  int oldsize = 0;
  char* newp = NULL;
  char* oldp = NULL;
  char* p = NULL;
  range_t* ranges = NULL;

  // Reset the heap.
  impl->reset_brk();

  // Call the mm package's init function
  if (impl->init() < 0) {
    malloc_error(tracenum, 0, "impl init failed.");
    return 0;
  }

  // Interpret each operation in the trace in order
  for (i = 0; i < trace->num_ops; i++) {
    index = trace->ops[i].index;
    size = trace->ops[i].size;

    switch (trace->ops[i].type) {
    case ALLOC:  // malloc
      // printf("[BLOCK_TRACK] trace->blocks[%d] = %p before alloc\n", index, trace->blocks[index]);
      // Call the student's malloc
      if ((p = (char*) impl->malloc(size)) == NULL) {
        malloc_error(tracenum, i, "impl malloc failed.");
        clear_ranges(&ranges);
        impl->reset_brk();
        return 0;
      }

      // Test the range of the new block for correctness and add it
      // to the range list if OK. The block must be  be aligned properly,
      // and must not overlap any currently allocated block.
      if (add_range(impl, &ranges, p, size, tracenum, i) == 0) {
        clear_ranges(&ranges);
        impl->reset_brk();
        return 0;
      }

      // Fill the allocated region with some unique data that you can check
      // for if the region is copied via realloc.
      // TODO(project3): YOUR CODE HERE
      memset(p, 0, size);  

      // Remember region
      trace->blocks[index] = p;
      // printf("[TRACE] saving ptr = %p at index = %d\n", p, index);
      trace->block_sizes[index] = size;
      break;

    case REALLOC:  // realloc

      // Call the student's realloc
      oldp = trace->blocks[index];
      if ((newp = (char*) impl->realloc(oldp, size)) == NULL) {
        malloc_error(tracenum, i, "impl realloc failed.");
        clear_ranges(&ranges);
        impl->reset_brk();
        return 0;
      }

      // Remove the old region from the range list
      assert(IS_ALIGNED(p));  // Or log p to double-check it's what you think it is
      remove_range(&ranges, oldp);

      // Check new block for correctness and add it to range list
      if (add_range(impl, &ranges, newp, size, tracenum, i) == 0) {
        clear_ranges(&ranges);
        impl->reset_brk();
        return 0;
      }

      // Make sure that the new block contains the data from the old block,
      // and then fill in the new block with new data that you can use to
      // verify the block was copied if it is resized again.
      oldsize = trace->block_sizes[index];
      if (size < oldsize) {
        oldsize = size;
      }
      // TODO(project3): YOUR CODE HERE
      for (int j = 0; j < oldsize; j++) {
        if (newp[j] != (char)0) { 
            malloc_error(tracenum, i, "Data in realloc'ed block does not match original data.");
            clear_ranges(&ranges);
            impl->reset_brk();
            return 0;
        }
      }
      memset(newp, 0, size);

      // Remember region
      trace->blocks[index] = newp;
      trace->block_sizes[index] = size;
      break;

    case FREE:  // free

      // Remove region from list and call student's free function
      p = trace->blocks[index];
      // printf("[TRACE] freeing ptr = %p from index = %d\n", p, index);
      assert(IS_ALIGNED(p));  // Or log p to double-check it's what you think it is
      remove_range(&ranges, p);
      impl->free(p);
      break;

    case WRITE:  // write

      break;

    default:
      app_error("Nonexistent request type in eval_mm_valid");
    }
  }

  // Free ranges allocated and reset the heap.
  impl->reset_brk();
  clear_ranges(&ranges);

  // As far as we know, this is a valid malloc package
  return 1;
}
#endif  // MM_VALIDATOR_H
