#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"

#include <errno.h>
#include "helper.h"


void coalesce_free_blks() {
	for(int i = 0; i < NUM_FREE_LISTS; i++) {
		sf_block *sentinel = &sf_free_list_heads[i];
		sf_block *curr = sentinel->body.links.prev;
		sf_block *prev = sentinel;

		// Loop backwards because new coalesced blocks can be in the same
		// free list size. The new coalesced blocks are added to the front
		while(curr != sentinel) {
			sf_block *below_blk = (void *)curr + get_blk_size(curr);
			int below_free = !is_alloced(below_blk);//(below_blk->header & THIS_BLOCK_ALLOCATED);
			sf_block *above_blk = (void *)curr - ((curr->prev_footer ^ MAGIC) & 0xFFFFFFF0);
			int above_free = !prev_is_alloced(curr);//(curr->header & PREV_BLOCK_ALLOCATED);

			if(below_free && above_free) {
				long total_blk_size = get_blk_size(above_blk)
					+ get_blk_size(curr) + get_blk_size(below_blk);
				sf_block *new = above_blk;
				set_blk_size(new, total_blk_size);

				remove_from_free_list(above_blk);
				remove_from_free_list(curr);
				remove_from_free_list(below_blk);

				set_header_eq_footer(new);
				add_blk_to_free_list(new);

				curr = prev->body.links.prev;
				prev = prev;
			}
			else if(below_free) {
				long total_blk_size = get_blk_size(curr) + get_blk_size(below_blk);
				sf_block *new = curr;
				set_blk_size(new, total_blk_size);

				remove_from_free_list(curr);
				remove_from_free_list(below_blk);

				set_header_eq_footer(new);
				add_blk_to_free_list(new);

				curr = prev->body.links.prev;
				prev = prev;
			}
			else if(above_free) {
				long total_blk_size = get_blk_size(above_blk) + get_blk_size(curr);
				sf_block *new = above_blk;
				set_blk_size(new, total_blk_size);

				remove_from_free_list(above_blk);
				remove_from_free_list(curr);

				set_header_eq_footer(new);
				add_blk_to_free_list(new);

				curr = prev->body.links.prev;
				prev = prev;
			}
			else {
				prev = curr;
				curr = curr->body.links.prev;
			}
		}
	}
}


void split_blk(sf_block *blk, long needed_blk_size, int payload) {
	long size_diff = get_blk_size(blk) - needed_blk_size;
    //sf_block *original_next_blk = (void *)blk + get_blk_size(blk);
    set_payload_size(blk, payload);
    set_alloced(blk, THIS_BLOCK_ALLOCATED);

    if(size_diff >= 32) {
    	set_below_prev_alloc(blk, !PREV_BLOCK_ALLOCATED);
        //set_prev_blk_alloced(original_next_blk, !PREV_BLOCK_ALLOCATED);
        set_blk_size(blk, needed_blk_size);

        sf_block *split_blk = (void *)blk + get_blk_size(blk);
        split_blk->header = 0;
        obfuscate(split_blk, OBF_HEADER);

        set_payload_size(split_blk, 0);
        set_blk_size(split_blk, size_diff);
        set_alloced(split_blk, !THIS_BLOCK_ALLOCATED);
        set_prev_blk_alloced(split_blk, PREV_BLOCK_ALLOCATED);
        set_header_eq_footer(split_blk);

        add_blk_to_free_list(split_blk);

        // Coalesce
        coalesce_free_blks();
    }
    else {
        //set_prev_blk_alloced(original_next_blk, PREV_BLOCK_ALLOCATED);
        set_below_prev_alloc(blk, PREV_BLOCK_ALLOCATED);
    }
}


void add_blk_to_free_list(sf_block *blk) {
	int free_list_num = get_free_list_index(get_blk_size(blk));
    sf_block *sentinel = &sf_free_list_heads[free_list_num];

    if(!sentinel->body.links.next) {
    	sentinel->body.links.prev = sentinel;
    	sentinel->body.links.next = sentinel;
    }

    sf_block *temp = sentinel->body.links.next;

    sentinel->body.links.next = blk;

    blk->body.links.prev = sentinel;
    blk->body.links.next = temp;

    temp->body.links.prev = blk;
}
void remove_from_free_list(sf_block *blk) {
	sf_block *prev = blk->body.links.prev;
	sf_block *next = blk->body.links.next;

	prev->body.links.next = next;
	next->body.links.prev = prev;
}
int get_free_list_index(size_t size) {
	int m = 32;
	long size_aligned = size;

	if(size_aligned <= m)
		return 0;
	else if(size_aligned <= 2*m)
		return 1;
	else if(size_aligned <= 4*m)
		return 2;
	else if(size_aligned <= 8*m)
		return 3;
	else if(size_aligned <= 16*m)
		return 4;
	else if(size_aligned <= 32*m)
		return 5;
	else if(size_aligned <= 64*m)
		return 6;
	else if(size_aligned <= 128*m)
		return 7;
	else if(size_aligned <= 256*m)
		return 8;
	else
		return 9;
}
void init_free_list_sentinels() {
	for(int i = 0; i < NUM_FREE_LISTS; i++) {
		sf_block *sent = &sf_free_list_heads[i];
		sent->body.links.prev = sent;
		sent->body.links.next = sent;
	}
}



int is_valid_ptr(void *ptr) {
	if(!ptr) {
		return 0;
	}

	uintptr_t temp = (uintptr_t)ptr;
	if(temp % 16 != 0){
		return 0;
	}

	// -8 because the given ptr is actually the ptr ot the payload
	// actual block starts 8 bytes before header
	sf_block *blk_ptr = (sf_block *)(ptr - 16);
	long blk_size = get_blk_size(blk_ptr);
	if(blk_size < 32) {
		return 0;
	}
	if(blk_size % 16 != 0) {
		return 0;
	}

	void *header_ptr = (void *)(&blk_ptr->header);
	void *footer_ptr = (void *)blk_ptr + blk_size;
	if(header_ptr < sf_mem_start()) {
		return 0;
	}
	if(footer_ptr > sf_mem_end() - 16) {
		return 0;
	}

	if(!is_alloced(blk_ptr)) {
		return 0;
	}

	// prev alloc condition
	// assumed this condition is met if rest of code is corret

	return 1;
}
void add_to_qklist(sf_block *blk) {
	int index = (get_blk_size(blk) - 32) / 16;
	set_in_qklist(blk, IN_QUICK_LIST);
	set_header_eq_footer(blk);

	// If qklist is empty set first element
	if(!sf_quick_lists[index].first) {
		sf_quick_lists[index].first = blk;
		blk->body.links.next = NULL;
	}
	else {
		blk->body.links.next = sf_quick_lists[index].first;
		sf_quick_lists[index].first = blk;
	}

	sf_quick_lists[index].length = (sf_quick_lists[index].length + 1);
}


long get_payload_size(sf_block *blk) {
	return ((blk->header ^ MAGIC) & 0xFFFFFFFF00000000) >> 32;
}
int is_alloced(sf_block *blk) {
	//
	return (blk->header ^ MAGIC) & THIS_BLOCK_ALLOCATED;
}
int prev_is_alloced(sf_block *blk) {
	//
	return (blk->header ^ MAGIC) & PREV_BLOCK_ALLOCATED;
}
int is_in_qklist(sf_block *blk) {
	//
	return (blk->header ^ MAGIC) & IN_QUICK_LIST;
}
long get_aligned_blk_size(size_t size) {
	size_t w_header = size + 8;
	if(w_header < 32)
		return 32;

	return w_header + ((16 - (w_header % 16)) % 16);
}
long get_blk_size(sf_block *blk) {
	//
	return (blk->header ^ MAGIC) & 0xFFFFFFF0;
}
sf_header get_header(sf_block *blk) {
	return blk->header ^ MAGIC;
	//
}
void set_payload_size(sf_block *blk, sf_size_t size) {
	obfuscate(blk, OBF_HEADER);
	blk->header = blk->header & 0xFFFFFFFF;
	blk->header = blk->header | ((long)size << 32);
	obfuscate(blk, OBF_HEADER);
}
void set_blk_size(sf_block *blk, long size) {
	obfuscate(blk, OBF_HEADER);
	blk->header = blk->header & 0xFFFFFFFF0000000F;
	blk->header = blk->header | size;
	obfuscate(blk, OBF_HEADER);
}
void set_alloced(sf_block *blk, int val) {
	obfuscate(blk, OBF_HEADER);
	blk->header = blk->header & ~THIS_BLOCK_ALLOCATED;
	blk->header = blk->header | val;
	obfuscate(blk, OBF_HEADER);
}
void set_prev_blk_alloced(sf_block *blk, int val) {
	obfuscate(blk, OBF_HEADER);
	blk->header = blk->header & ~PREV_BLOCK_ALLOCATED;
	blk->header = blk->header | val;
	obfuscate(blk, OBF_HEADER);
}
void set_in_qklist(sf_block *blk, int val) {
	obfuscate(blk, OBF_HEADER);
	blk->header = blk->header & ~IN_QUICK_LIST;
	blk->header = blk->header | val;
	obfuscate(blk, OBF_HEADER);
}
void set_header_eq_footer(sf_block *blk) {
	sf_block *next_blk = (void *)blk + get_blk_size(blk);
	next_blk->prev_footer = blk->header;
}
void obfuscate(sf_block *blk, int type) {
	if(type == 1)
		blk->header = blk->header ^ MAGIC;
	else if(type == 2)
		blk->prev_footer = blk->prev_footer ^ MAGIC;
}
void set_below_prev_alloc(sf_block *blk, int val) {
	sf_block *below = (void *)blk + get_blk_size(blk);

	set_prev_blk_alloced(below, val);
	if(is_alloced(below)) {
		set_header_eq_footer(below);
	}
}