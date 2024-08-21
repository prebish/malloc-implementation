#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>

#include "memlib.h"
#include "mm.h"

/* Macros for unscaled pointer arithmetic to keep other code cleaner.
   Casting to a char* has the effect that pointer arithmetic happens at
   the byte granularity (i.e. POINTER_ADD(0x1, 1) would be 0x2).  (By
   default, incrementing a pointer in C has the effect of incrementing
   it by the size of the type to which it points (e.g. Block).)
   We cast the result to void* to force you to cast back to the
   appropriate type and ensure you don't accidentally use the resulting
   pointer as a char* implicitly.
*/
#define UNSCALED_POINTER_ADD(p, x) ((void*)((char*)(p) + (x))) 
#define UNSCALED_POINTER_SUB(p, x) ((void*)((char*)(p) - (x))) 


/******** FREE LIST IMPLEMENTATION ***********************************/
typedef struct _BlockInfo {
  // Size of the block and whether or not the block is in use or free.
  // When the size is negative, the block is currently free.
  long int size;
  struct _Block* prev; // Pointer to the previous block in the list.

} BlockInfo;

/* A FreeBlockInfo structure contains metadata just for free blocks.
 * When you are ready, you can improve your naive implementation by
 * using these to maintain a separate list of free blocks.
 *
 * These are "kept" in the region of memory that is normally used by
 * the program when the block is allocated. That is, since that space
 * is free anyway, we can make good use of it to improve our malloc.
 */
typedef struct _FreeBlockInfo {
  struct _Block* nextFree; // Pointer to the next free block in the list.
  struct _Block* prevFree; // Pointer to the previous free block in the list.

} FreeBlockInfo;

/* This is a structure that can serve as all kinds of nodes.*/
typedef struct _Block {
  BlockInfo info;
  FreeBlockInfo freeNode;
} Block;

/* Pointer to the first FreeBlockInfo in the free list, the list's head. */
static Block* free_list_head = NULL;
static Block* malloc_list_tail = NULL;

static size_t heap_size = 0;

/* Size of a word on this architecture. */
#define WORD_SIZE sizeof(void*)
/* Alignment of blocks returned by mm_malloc.
 * (We need each allocation to at least be big enough for the free space
 * metadata... so let's just align by that.)  */
#define ALIGNMENT (sizeof(FreeBlockInfo))

/* This function will have the OS allocate more space for our heap.
 *
 * It returns a pointer to that new space. That pointer will always be
 * larger than the last request and be continuous in memory.
 */
void* requestMoreSpace(size_t reqSize);
/* This function will get the first block or returns NULL if there is not
 * one.
 *
 * You can use this to start your through search for a block.
 */
Block* first_block();
/* This function will get the adjacent block or returns NULL if there is not
 * one.
 *
 * You can use this to move along your malloc list one block at a time.
 */
Block* next_block(Block* block);
/* Use this function to print a thorough listing of your heap data structures.
 */
void examine_heap();
/* Checks the heap for any issues and prints out errors as it finds them.
 *
 * Use this when you are debugging to check for consistency issues. */
int check_heap();

//========================================================================================//
//========================================================================================//
//========================================================================================//

/* Find a free block of at least the requested size in the free list.  Returns
   NULL if no free block is large enough. */
Block* searchFreeList(size_t reqSize) {
  Block* ptrFreeBlock = free_list_head;
  long int checkSize = -reqSize;

  if(malloc_list_tail && malloc_list_tail->info.size <= checkSize) return malloc_list_tail;

  while (ptrFreeBlock) {  // while ptrFreeBlock  < size needed
    if (ptrFreeBlock->info.size <= checkSize) return ptrFreeBlock; // if end of list, request more space
    ptrFreeBlock = ptrFreeBlock->freeNode.nextFree;
  }
  return NULL;
}

// TOP-LEVEL ALLOCATOR INTERFACE ------------------------------------

void insertFreeBlock(Block* freeBlock) {
  if (!freeBlock) return;

  freeBlock->freeNode.prevFree = NULL;

  if (free_list_head) {
    freeBlock->freeNode.nextFree = free_list_head;
    free_list_head->freeNode.prevFree = freeBlock;
  } 
  else freeBlock->freeNode.nextFree = NULL;

  free_list_head = freeBlock;
}

void removeFreeBlock(Block* freeBlock) {
  if(!freeBlock) return;
  if(!free_list_head) return;

  Block* prev = freeBlock->freeNode.prevFree;
  Block* next = freeBlock->freeNode.nextFree;

  // Remove from Middle
  if(prev && next) {
    prev->freeNode.nextFree = next;
    next->freeNode.prevFree = prev;
  }

  // Remove at front
  else if(!prev && next) {  
    free_list_head = next;
    free_list_head->freeNode.prevFree = NULL;
  } 

  // Remove from tail
  else if(prev && !next)
    prev->freeNode.nextFree = NULL;  

  // Remove single free block list
  else if(!prev && !next) 
    free_list_head = NULL;
    
  return;
}


/* Allocate a block of size size and return a pointer to it. If size is zero,
 * returns null.
 */
void* mm_malloc(size_t size) {
  if (size == 0) return NULL;

  long int reqSize;
  reqSize = size; // Determine the amount of memory we want to allocate
  reqSize = ALIGNMENT * ((reqSize + ALIGNMENT - 1) / ALIGNMENT); // Round up for correct alignment
  Block* ptrFreeBlock = searchFreeList(reqSize);

  // == No Free Blocks to Use == //
  if(!ptrFreeBlock) {
    ptrFreeBlock = (Block*)requestMoreSpace(sizeof(BlockInfo) + reqSize); // request more space to create block
    if(!ptrFreeBlock) return NULL;

    ptrFreeBlock->info.size = reqSize; // set block size
    ptrFreeBlock->info.prev = malloc_list_tail; // block prev is now old tail
    malloc_list_tail = ptrFreeBlock; // new tail is block

    return UNSCALED_POINTER_ADD(ptrFreeBlock, sizeof(BlockInfo)); // return space for use to allocate
  }

  // == Split Existing Block == //
  if(-(ptrFreeBlock->info.size) > (reqSize + sizeof(BlockInfo))) {
    Block* nextBlock = next_block(ptrFreeBlock);
    Block* splitBlock = UNSCALED_POINTER_ADD(ptrFreeBlock, reqSize + sizeof(BlockInfo));

    removeFreeBlock(ptrFreeBlock); // remove before changing

    splitBlock->info.size = ptrFreeBlock->info.size + reqSize +  sizeof(BlockInfo);
    ptrFreeBlock->info.size = reqSize; // block size is requested size
    splitBlock->info.prev = ptrFreeBlock; // splitBlock prev is the original block

    if(nextBlock) nextBlock->info.prev = splitBlock; // if there is a next, its prev is splitBlock
    else malloc_list_tail = splitBlock; // update tail 
    
    insertFreeBlock(splitBlock);

    return UNSCALED_POINTER_ADD(ptrFreeBlock, sizeof(BlockInfo));
  }

  // == Exact Size == //
  ptrFreeBlock->info.size = -ptrFreeBlock->info.size; // make positive size
  removeFreeBlock(ptrFreeBlock);
  return UNSCALED_POINTER_ADD(ptrFreeBlock, sizeof(BlockInfo));
}

void coalesce(Block* blockInfo) {
  if (!blockInfo || blockInfo->info.size >= 0) return;

  Block* nextBlock = next_block(blockInfo);
  Block* previousBlock = blockInfo->info.prev;

  if (!nextBlock && !previousBlock) {
    insertFreeBlock(blockInfo);
    return;
  }

  // If PREV is free
  if(previousBlock && previousBlock->info.size < 0) {
    removeFreeBlock(previousBlock);
    previousBlock->info.size = (previousBlock->info.size + blockInfo->info.size) - sizeof(BlockInfo); // set prevBlock size to absorb blockInfo
    blockInfo = previousBlock; 

    if (nextBlock) nextBlock->info.prev = blockInfo;
    else malloc_list_tail = blockInfo;
  }

  // If NEXT is free
  if (nextBlock && nextBlock->info.size < 0) {
    removeFreeBlock(nextBlock); // REMOVING FREE NEXT HERE
    blockInfo->info.size = (blockInfo->info.size + nextBlock->info.size) - sizeof(BlockInfo);
    
    Block* nextNext = next_block(nextBlock);

    if(nextNext) nextNext->info.prev = blockInfo;
    else malloc_list_tail = blockInfo;
  }

  insertFreeBlock(blockInfo);

}

/* Free the block referenced by ptr. */
void mm_free(void* ptr) {
  if (!ptr) return;
  Block* blockInfo = (Block*)UNSCALED_POINTER_SUB(ptr, sizeof(BlockInfo));
  blockInfo->info.size = -blockInfo->info.size; // show block as free with neg num
  coalesce(blockInfo);
}

//=======================================================================================//
//=======================================================================================//
//=======================================================================================//




















// PROVIDED FUNCTIONS -----------------------------------------------
//
// You do not need to modify these, but they might be helpful to read
// over.

/* Get more heap space of exact size reqSize. */
void* requestMoreSpace(size_t reqSize) {
  void* ret = UNSCALED_POINTER_ADD(mem_heap_lo(), heap_size);
  heap_size += reqSize;

  void* mem_sbrk_result = mem_sbrk(reqSize);
  if ((size_t)mem_sbrk_result == -1) {
    printf("ERROR: mem_sbrk failed in requestMoreSpace\n");
    exit(0);
  }

  return ret;
}

/* Initialize the allocator. */
int mm_init() {
  free_list_head = NULL;
  malloc_list_tail = NULL;
  heap_size = 0;

  return 0;
}

/* Gets the first block in the heap or returns NULL if there is not one. */
Block* first_block() {
  Block* first = (Block*)mem_heap_lo();
  if (heap_size == 0) {
    return NULL;
  }

  return first;
}

/* Gets the adjacent block or returns NULL if there is not one. */
Block* next_block(Block* block) {
  size_t distance = (block->info.size > 0) ? block->info.size : -block->info.size;

  Block* end = (Block*)UNSCALED_POINTER_ADD(mem_heap_lo(), heap_size);
  Block* next = (Block*)UNSCALED_POINTER_ADD(block, sizeof(BlockInfo) + distance);
  if (next >= end) {
    return NULL;
  }

  return next;
}

/* Print the heap by iterating through it as an implicit free list. */
void examine_heap() {
  /* print to stderr so output isn't buffered and not output if we crash */
  Block* curr = (Block*)mem_heap_lo();
  Block* end = (Block*)UNSCALED_POINTER_ADD(mem_heap_lo(), heap_size);
  fprintf(stderr, "heap size:\t0x%lx\n", heap_size);
  fprintf(stderr, "heap start:\t%p\n", curr);
  fprintf(stderr, "heap end:\t%p\n", end);

  fprintf(stderr, "free_list_head: %p\n", (void*)free_list_head);

  fprintf(stderr, "malloc_list_tail: %p\n", (void*)malloc_list_tail);

  while(curr && curr < end) {
    /* print out common block attributes */
    fprintf(stderr, "%p: %ld\t", (void*)curr, curr->info.size);

    /* and allocated/free specific data */
    if (curr->info.size > 0) {
      fprintf(stderr, "ALLOCATED\tprev: %p\n", (void*)curr->info.prev);
    } else {
      fprintf(stderr, "FREE\tnextFree: %p, prevFree: %p, prev: %p\n", (void*)curr->freeNode.nextFree, (void*)curr->freeNode.prevFree, (void*)curr->info.prev);
    }

    curr = next_block(curr);
  }
  fprintf(stderr, "END OF HEAP\n\n");

  curr = free_list_head;
  fprintf(stderr, "Head ");
  while(curr) {
    fprintf(stderr, "-> %p ", curr);
    curr = curr->freeNode.nextFree;
  }
  fprintf(stderr, "\n");
}

/* Checks the heap data structure for consistency. */
int check_heap() {
  Block* curr = (Block*)mem_heap_lo();
  Block* end = (Block*)UNSCALED_POINTER_ADD(mem_heap_lo(), heap_size);
  Block* last = NULL;
  long int free_count = 0;

  while(curr && curr < end) {
    if (curr->info.prev != last) {
      fprintf(stderr, "check_heap: Error: previous link not correct.\n");
      examine_heap();
    }

    if (curr->info.size <= 0) {
      // Free
      free_count++;
    }

    last = curr;
    curr = next_block(curr);
  }

  curr = free_list_head;
  last = NULL;
  while(curr) {
    if (curr == last) {
      fprintf(stderr, "check_heap: Error: free list is circular.\n");
      examine_heap();
    }
    last = curr;
    curr = curr->freeNode.nextFree;
    if (free_count == 0) {
      fprintf(stderr, "check_heap: Error: free list has more items than expected.\n");
      examine_heap();
    }
    free_count--;
  }

  return 0;
}

// = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = //
                              //  N O T E S   //

// FREE LIST IS JUST LINKING ALL THE FREE PARTS ARE THE ORIGINAL LINK LIST OF BLOCKS
// Phase 1 fails in coalescing-bal.rep, random-bal.rep, random2-bal.rep, 
// realloc-bal.rep & realloc2-bal.rep (Bogus type character (r) in trace file) 
