#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == ((bp->header ^ MAGIC) & 0xfffffff0))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert the total number of quick list blocks of a specified size.
 * If size == 0, then assert the total number of all quick list blocks.
 */
void assert_quick_list_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_QUICK_LISTS; i++) {
	sf_block *bp = sf_quick_lists[i].first;
	while(bp != NULL) {
	    if(size == 0 || size == ((bp->header ^ MAGIC) & 0xfffffff0)) {
		cnt++;
		if(size != 0) {
		    // Check that the block is in the correct list for its size.
		    int index = (size - 32) >> 4;
		    cr_assert_eq(index, i, "Block %p (size %ld) is in wrong quick list for its size "
				 "(expected %d, was %d)",
				 &bp->header, (bp->header ^ MAGIC) & 0xfffffff0, index, i);
		}
	    }
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");
	sf_block *bp = (sf_block *)((char *)x - 16);
	cr_assert((bp->header >> 32) & 0xffffffff,
		  "Malloc'ed block payload size (%ld) not what was expected (%ld)!",
		  (bp->header >> 32) & 0xffffffff, sz);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(944, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	void *x = sf_malloc(4032);
	cr_assert_not_null(x, "x is NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(98304);

	cr_assert_null(x, "x is not NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(24528, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_quick, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 32, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(48, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(864, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(208, 1);
	assert_free_block_count(704, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(384, 1);
	assert_free_block_count(528, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 150, sz_w = 50, sz_x = 150, sz_y = 200, sz_z = 250;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_quick_list_block_count(0, 1);
	assert_free_block_count(0, 3);
	assert_free_block_count(208, 2);
	assert_free_block_count(928, 1);

	// First block in list should be the most recently freed block not in quick list.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(&bp->header, (char *)y - 8,
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, &bp->header, (char *)y - 8);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 16);
	cr_assert((bp->header ^ MAGIC) & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert(((bp->header ^ MAGIC) & 0xfffffff0) == 96,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ MAGIC) & 0xfffffff0, 96);
	cr_assert((((bp->header ^ MAGIC) >> 32) & 0xffffffff) == sz_x1,
		  "Realloc'ed block payload size (%ld) not what was expected (%ld)!",
		  (((bp->header ^ MAGIC) >> 32) & 0xffffffff), sz_x1);

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(32, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(816, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char *)x - 16);
	cr_assert((bp->header ^ MAGIC) & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert(((bp->header ^ MAGIC) & 0xfffffff0) == 96,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ MAGIC) & 0xfffffff0, 96);
	cr_assert((((bp->header ^ MAGIC) >> 32) & 0xffffffff) == sz_y,
		  "Realloc'ed block payload size (%ld) not what was expected (%ld)!",
		  (((bp->header ^ MAGIC) >> 32) & 0xffffffff), sz_y);

	// There should be only one free block.
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(880, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)x - 16);
	cr_assert((bp->header ^ MAGIC) & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert(((bp->header ^ MAGIC) & 0xfffffff0) == 32,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ MAGIC) & 0xfffffff0, 32);
	cr_assert((((bp->header ^ MAGIC) >> 32) & 0xffffffff) == sz_y,
		  "Realloc'ed block payload size (%ld) not what was expected (%ld)!",
		  (((bp->header ^ MAGIC) >> 32) & 0xffffffff), sz_y);

	// After realloc'ing x, we can return a block of size 48
	// to the freelist.  This block will go into the main freelist and be coalesced.
	// Note that we don't put split blocks into the quick lists because their sizes are not sizes
	// that were requested by the client, so they are not very likely to satisfy a new request.
	assert_quick_list_block_count(0, 0);	
	assert_free_block_count(0, 1);
	assert_free_block_count(944, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

//Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
//}


Test(sfmm_student_suite, flush_qkist, .timeout = TEST_TIMEOUT) {
	size_t sz_a = 40;
	size_t sz_b = 40;
	size_t sz_c = 40;
	size_t sz_d = 40;
	size_t sz_e = 40;
	size_t sz_f = 40;

	void *a = sf_malloc(sz_a);
	void *b = sf_malloc(sz_b);
	void *c = sf_malloc(sz_c);
	void *d = sf_malloc(sz_d);
	void *e = sf_malloc(sz_e);
	void *f = sf_malloc(sz_f);

	sf_free(a);
	sf_free(b);
	sf_free(c);
	sf_free(d);
	sf_free(e);
	sf_free(f);

	assert_quick_list_block_count(48, 1);
	assert_free_block_count(48*5, 1);
	assert_free_block_count(688, 1);
}

Test(sfmm_student_suite, resize_heap, .timeout = TEST_TIMEOUT) {
	size_t sz_x = 1024;
	size_t sz_y = 64;
	size_t sz_z = 4000;
	sf_malloc(sz_x);
	sf_malloc(sz_y);
	sf_malloc(sz_z);

	assert_free_block_count(6096-5136, 1);
}

Test(sfmm_student_suite, reuse_qklist_blk, .timeout = TEST_TIMEOUT) {
	size_t sz_x = 64;
	size_t sz_y = 60;

	void *x = sf_malloc(sz_x);
	sf_free(x);

	void *y = sf_malloc(sz_y);

	cr_assert(x == y, "Block from quicklist was not used");
	assert_quick_list_block_count(80, 0);
}

Test(sfmm_student_suite, coalesce_non_qklist_blks, .timeout = TEST_TIMEOUT) {
	size_t sz_a = 1000; // 1008
	size_t sz_b = 205;  // 224
	size_t sz_c = 61;   // 80
	size_t sz_d = 509;  // 528
	size_t sz_e = 53;   // 64
	size_t sz_f = 622;  // 640

	void *a = sf_malloc(sz_a);
	void *b = sf_malloc(sz_b);
	void *c = sf_malloc(sz_c);
	void *d = sf_malloc(sz_d);
	void *e = sf_malloc(sz_e);
	void *f = sf_malloc(sz_f);

	sf_free(a);
	sf_free(e);
	sf_free(d);
	sf_free(f);
	sf_free(b);
	sf_free(c);

	assert_free_block_count(1232, 1);
	assert_free_block_count(528, 1);
	assert_free_block_count(1120, 1);
	assert_quick_list_block_count(80, 1);
	assert_quick_list_block_count(64, 1);
}

Test(sfmm_student_suite, fragmentation_and_utilization, .timeout = TEST_TIMEOUT) {
	size_t sz_a = 1000;
	size_t sz_b = 205;
	size_t sz_c = 61;
	size_t sz_d = 509;
	size_t sz_e = 53;
	size_t sz_f = 622;

	void *a = sf_malloc(sz_a);
	sf_malloc(sz_b);
	void *c = sf_malloc(sz_c);
	void *d = sf_malloc(sz_d);
	sf_malloc(sz_e);
	void *f = sf_malloc(sz_f);

	sf_free(a);

	sf_free(c);
	sf_free(d);

	sf_free(f);

	cr_assert(sf_internal_fragmentation() == (double)(53+205)/(double)(64+224),
		"Internal fragmentation is incorrect");
	cr_assert(sf_peak_utilization() == (double)(53+205)/3072,
		"Peak utilization is incorrect");
}

