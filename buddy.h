#ifndef OS_MM_H
#define OS_MM_H
#define MAX_ERRNO 4095

#define OK          0
#define EINVAL      22  /* Invalid argument */    
#define ENOSPC      28  /* No page left */  


#define IS_ERR_VALUE(x) ((x) >= (unsigned long)-MAX_ERRNO)
// Macros accept both pointers and ints without prototype conversion warnings
#define ERR_PTR(error) ((void *)(long)(error))
#define PTR_ERR(x) ((long)(x))
#define IS_ERR(x) IS_ERR_VALUE((unsigned long)(x))


int init_page(void *p, int pgcount);
void *alloc_pages(int rank);
int return_pages(void *p);
int query_ranks(void *p);
int query_page_counts(int rank);

#endif
