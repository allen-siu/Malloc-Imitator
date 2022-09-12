#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"

#include <errno.h>

#define OBF_PREV_FOOTER 2
#define OBF_HEADER 1

#define QKLIST_MAX_BLK_SIZE (32 + 16*(NUM_QUICK_LISTS - 1))

void coalesce_free_blks();
void add_blk_to_free_list(sf_block *blk);
void remove_from_free_list(sf_block *blk);
int get_free_list_index(size_t size);
void init_free_list_sentinels();

void split_blk(sf_block *blk, long needed_blk_size, int payload);

int is_valid_ptr(void *ptr);
void add_to_qklist(sf_block *blk);

int is_alloced(sf_block *blk);
int prev_is_alloced(sf_block *blk);
int is_in_qklist(sf_block *blk);

long get_payload_size(sf_block *blk);
long get_blk_size(sf_block *blk);
long get_aligned_blk_size(size_t size);
sf_header get_header(sf_block *blk);
void set_payload_size(sf_block *blk, sf_size_t size);
void set_blk_size(sf_block *blk, long size);
void set_alloced(sf_block *blk, int val);
void set_prev_blk_alloced(sf_block *blk, int val);
void set_in_qklist(sf_block *blk, int val);
void set_header_eq_footer(sf_block *blk);
void obfuscate(sf_block *blk, int type);

void set_below_prev_alloc(sf_block *blk, int val);

