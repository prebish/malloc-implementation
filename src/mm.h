#include <stdio.h>

extern int mm_init(void);
extern void *mm_malloc(size_t size);
extern void mm_free(void *ptr);
extern void examine_heap();


extern void* mm_realloc(void* ptr, size_t size);
