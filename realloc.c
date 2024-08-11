#include <config.h>
#undef realloc

#include <sys/types.h>

void *realloc(void *ptr, size_t size);

/* Re-allocate an N-byte block of memory from the heap.
If N is zero, re-allocate to a 1-byte block.  */

void *rpl_realloc(void *ptr, size_t n)
{
	if (n == 0)
		 n = 1;
	return realloc(ptr, n);
}
