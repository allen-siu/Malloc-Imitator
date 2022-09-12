/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"

#include <errno.h>
#include "helper.h"

void *sf_malloc(sf_size_t size) {
    // TO BE IMPLEMENTED
    if(size == 0)
        return NULL;

    // If this is first malloc ever done, set up heap
    if(sf_mem_start() == sf_mem_end()) {
        init_free_list_sentinels();

        void *heap_ptr = sf_mem_grow();
        long bytes_used = 0;

        // If memgrow fails, set error
        if(!heap_ptr) {
            sf_errno = ENOMEM;
            return NULL;
        }

        // Setup prologue
        bytes_used += (8 * 2);
        sf_block *prologue = (sf_block*)heap_ptr; // no +8 bc prev footer field
        prologue->header = 0x20 | THIS_BLOCK_ALLOCATED; // set blk size to 32
        obfuscate(prologue, OBF_HEADER);

        // Setup epilogue
        bytes_used += 8;
        sf_block *epilogue = (sf_block*)(sf_mem_end() - 16); // -16 bytes
        epilogue->header = THIS_BLOCK_ALLOCATED;
        obfuscate(epilogue, OBF_HEADER);

        // Set up rest of memory as free block
        bytes_used += (8 * 3);
        sf_block *first_blk = (sf_block *)((void *)prologue + 8 + 8*3); // +8 for prologue header, +8*3 for 3 empty rows
        first_blk->header = PAGE_SZ - bytes_used;   // Set block size
        first_blk->header = first_blk->header | PREV_BLOCK_ALLOCATED;
        obfuscate(first_blk, OBF_HEADER);

        // Set footer for the free block
        epilogue->prev_footer = get_header(first_blk);
        obfuscate(epilogue, OBF_PREV_FOOTER);

        // Add free block to free list
        add_blk_to_free_list(first_blk);
    }

    // Search quick lists
    long quick_blk_size = get_aligned_blk_size(size);
    long list_num = (quick_blk_size - 32) / 16;

    // If size exists in quicklists, then search
    if (list_num < NUM_QUICK_LISTS) {
        sf_block *curr = sf_quick_lists[list_num].first;

        // If there exists block in quicklist
        if(curr) {
            sf_quick_lists[list_num].first = curr->body.links.next;
            set_payload_size(curr, size);
            return &curr->body;
        }
    }

    // Check free blocks if no quick lists fit
    sf_block *open_blk = NULL;
    while(!open_blk) {
        long needed_blk_size = get_aligned_blk_size(size);
        long free_list_num = get_free_list_index(needed_blk_size);

        // Search for if a free block can satisfy request
        for(int i = free_list_num; i < NUM_FREE_LISTS; i++) {
            sf_block *sentinel = &sf_free_list_heads[i];
            sf_block *curr = sentinel->body.links.next;

            while(curr != sentinel) {
                if(get_blk_size(curr) >= needed_blk_size) {
                    open_blk = curr;
                    break;
                }
                curr = curr->body.links.next;
            }
            if(open_blk)
                break;
        }

        // If free block found, remove from free list, split and coalesce if needed
        // else memgrow and coalesce
        if(open_blk) {
            remove_from_free_list(open_blk);
            split_blk(open_blk, needed_blk_size, size);

            return &open_blk->body;
        }
        else {
            // No free blocks fit request, memgrow
            // Set epilogue to be a new free block and reate new epilogue
            sf_block *old_ep = sf_mem_end() - 16;
            void *mem_grow_err_check = sf_mem_grow();
            if(!mem_grow_err_check) {
                sf_errno = ENOMEM;
                return NULL;
            }

            // new epilogue is already obfuscated
            sf_block *new_ep = sf_mem_end() - 16;
            obfuscate(new_ep, OBF_HEADER);
            set_blk_size(new_ep, 0);
            set_prev_blk_alloced(new_ep, !PREV_BLOCK_ALLOCATED);
            set_payload_size(new_ep, 0);
            set_alloced(new_ep, THIS_BLOCK_ALLOCATED);

            set_alloced(old_ep, !THIS_BLOCK_ALLOCATED);

            set_blk_size(old_ep, PAGE_SZ);
            set_header_eq_footer(old_ep);

            add_blk_to_free_list(old_ep);

            // Coalesce
            coalesce_free_blks();
        }
    }

    // If reaches this point some unknown error happened
    return NULL;
}

void sf_free(void *pp) {
    // TO BE IMPLEMENTED

    if(!is_valid_ptr(pp)) {
        abort();
    }

    sf_block *blk = pp - 16;
    long blk_size = get_blk_size(blk);

    if(blk_size > QKLIST_MAX_BLK_SIZE) {
        set_below_prev_alloc(blk, !PREV_BLOCK_ALLOCATED);
        set_alloced(blk, !THIS_BLOCK_ALLOCATED);
        set_header_eq_footer(blk);
        add_blk_to_free_list(blk);
        coalesce_free_blks();
    }
    else {
        // Check quicklists
        int qklist_index = (blk_size - 32) / 16;
        int list_len = sf_quick_lists[qklist_index].length;

        if(list_len == 5) {
            // Flush list
            sf_block *curr = sf_quick_lists[qklist_index].first;
            for(int i = 0; i < list_len; i++) {
                sf_block *temp = curr->body.links.next;

                set_below_prev_alloc(curr, !PREV_BLOCK_ALLOCATED);

                set_alloced(curr, !THIS_BLOCK_ALLOCATED);
                set_in_qklist(curr, !IN_QUICK_LIST);
                set_header_eq_footer(curr);
                add_blk_to_free_list(curr);

                curr = temp;
            }
            sf_quick_lists[qklist_index].first = NULL;
            sf_quick_lists[qklist_index].length = 0;

            coalesce_free_blks();
        }

        add_to_qklist(blk);
    }
}

void *sf_realloc(void *pp, sf_size_t rsize) {
    // TO BE IMPLEMENTED
    if(!is_valid_ptr(pp)) {
        abort();
    }
    if(rsize == 0) {
        sf_free(pp);
        return NULL;
    }

    // -16 because ptr returned from malloc is ptr to payload, not start of blk
    sf_block *old_blk = pp - 16;
    sf_block *new_blk = NULL;

    long needed_blk_size = get_aligned_blk_size(rsize);
    if(needed_blk_size <= get_blk_size(old_blk)) {
        new_blk = old_blk;

        split_blk(new_blk, needed_blk_size, rsize);

    }
    else {
        sf_free(pp);
        new_blk = sf_malloc(rsize) - 16;
        if(!new_blk) {
            return NULL;
        }
        memcpy(&new_blk->body, &old_blk->body, rsize);
    }

    return &new_blk->body;
}

double sf_internal_fragmentation() {
    // TO BE IMPLEMENTED
    sf_block *prologue = sf_mem_start();
    sf_block *epilogue = sf_mem_end() - 16;
    double block_sum = 0.0;
    double payload_sum = 0.0;

    sf_block *curr = (void *)prologue + get_blk_size(prologue);

    while(curr != epilogue) {
        if(is_alloced(curr) && !is_in_qklist(curr)) {
            block_sum += get_blk_size(curr);
            payload_sum += get_payload_size(curr);
        }
        curr = (void *)curr + get_blk_size(curr);
    }

    return payload_sum / block_sum;
}

double sf_peak_utilization() {
    // TO BE IMPLEMENTED
    sf_block *prologue = sf_mem_start();
    sf_block *epilogue = sf_mem_end() - 16;
    double payload_sum = 0.0;

    sf_block *curr = (void *)prologue + get_blk_size(prologue);

    while(curr != epilogue) {
        if(is_alloced(curr) && !is_in_qklist(curr)) {
            payload_sum += get_payload_size(curr);
        }
        curr = (void *)curr + get_blk_size(curr);
    }

    double heap_size = sf_mem_end() - sf_mem_start();
    if(heap_size == 0) {
        return 0;
    }
    return payload_sum / heap_size;
}
