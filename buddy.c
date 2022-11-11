#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include "buddy.h"
#include <stdio.h>
static size_t MIN_BLOCK_SIZE = 32;
static int CURR_ORDER = 0;
static int BASE_PTR = 0; // must be initialized
static size_t DEFAULT_HEAP_SIZE = 4096; // so we allocate just 1 page by default

// this can't be a const, bc consts are mutable in c. lol 
#define LEVELS 8

#define USE_SBRK

struct chunk_mdata* head = NULL; 

struct chunk_mdata* free_list[LEVELS] = { NULL };

struct chunk_mdata {
    enum flag chunk_flag;
    short int order;
    struct chunk_mdata* next;
    struct chunk_mdata* prev;
};

// buddy allocator

void fr33(void* ptr) {
    struct chunk_mdata* chunk_to_free = (struct chunk_mdata*)ptr;
    struct chunk_mdata* buddy = get_buddy(chunk_to_free);

    // if the chunk already has a free buddy, they should be merged
    if (buddy && buddy->chunk_flag == FREE) {
        chunk_to_free = merge_chunks(chunk_to_free, buddy);
    }
    
    chunk_to_free->chunk_flag = FREE;
    append_to_free_list(chunk_to_free);   
}

// merge two chunks into the primary chunk and return the primary chunk
struct chunk_mdata* merge_chunks(struct chunk_mdata* lh, struct chunk_mdata* rh) {

    // the leftmost chunk (lower addr chunk)
    struct chunk_mdata* primary_chunk = lh < rh ? lh : rh;
    struct chunk_mdata* secondary_chunk = lh > rh ? lh : rh;
    
    // re-link the list; this will remove the secondary chunk from any linked lists it is part of
    // leaving the primary chunk in its place. 
    primary_chunk->next = secondary_chunk->next; 
    if(primary_chunk->next) {
        primary_chunk->next->prev = primary_chunk;
    }
    primary_chunk->order++;
    
    return primary_chunk;
}

void append_to_free_list(struct chunk_mdata* chunk_to_append) {
    if(free_list[chunk_to_append->order]) {
        // traverse the linked list to the end
        struct chunk_mdata* curr_chunk = free_list[chunk_to_append->order];
        while(curr_chunk->next) {
            curr_chunk = curr_chunk->next;
        }
        curr_chunk->next = chunk_to_append;
        chunk_to_append->prev = curr_chunk;
        chunk_to_append->next = NULL;
    }
    else {
        free_list[chunk_to_append->order] = chunk_to_append;
    }
}


// TODO double check
struct chunk_mdata* get_buddy(struct chunk_mdata* chunk) {
    long addr = (long)chunk;
    long addr_of_buddy = addr ^ (1 << chunk->order);
    return (struct chunk_mdata*)addr_of_buddy;
}

void* mall0c(size_t size) {
    // this pointer will be handed out to the caller
    struct chunk_mdata* chunk_to_distribute = NULL;

    if(size == 0) {
        return NULL;
    }
    short int order = order_from_size(size);
    if (free_list[order] == NULL) {
        // no free chunk of this order
        // search superior orders 
        short int i = order;
        struct chunk_mdata* free_chunk = NULL;
        do {
            free_chunk = free_list[i];
            i++; 
        }
        while(free_list[i] == NULL && i <= LEVELS);
        
        if(i > LEVELS) {
            // there are no free chunks, we need to page in more memory
            free_chunk = resize_heap(size);
            free_list[free_chunk->order] = free_chunk;
        }

        struct chunk_mdata* suitable_chunk = cascade_split(free_chunk, order);
        
        // remove this chunk from the free list
        // offsetting the chunk by sizeof(chunk_mdata) will yield an address pointing to the data
        chunk_to_distribute = suitable_chunk; 
    }
    else {
        chunk_to_distribute = free_list[order];
    }

    // flag the chunk as in use
    chunk_to_distribute->chunk_flag = IN_USE;

    // remove the returned pointer from our free list
    // the start of the free list will now point to the 2nd first element in the free list
    // if chunk_to_distribute->next is null then so will this free list be. aka empty
    free_list[order] = chunk_to_distribute->next;
    return (void*)((long)chunk_to_distribute + sizeof(struct chunk_mdata));;
}

struct chunk_mdata* resize_heap(size_t req_size) {    
    if(!head) {
        // initialize the heap as a single 4k page 
        void* mc = morecore(DEFAULT_HEAP_SIZE);
        return new_chunk(mc, FREE, order_from_size(DEFAULT_HEAP_SIZE), NULL, NULL);
    }
    
    // find the highest non-free level
    // we need to mmap max(req_sie, sizeof(highest_non_free_level)) 
    // this is slow but morecore will be called so infrequently that it is fine -- no "occupied" list is needed
    struct chunk_mdata* curr_chunk = head;
    short int max_order_in_use = head->order;
    while(curr_chunk->next) {
        max_order_in_use = curr_chunk->order > max_order_in_use ? curr_chunk->order : max_order_in_use; 
        curr_chunk = curr_chunk->next;
    }

    // the order right above the highest allocated order
    size_t h_size = MIN_BLOCK_SIZE << (max_order_in_use + 1);
    size_t size_to_allocate = h_size > req_size ? h_size : req_size; 
    void* new_pages = morecore(size_to_allocate); 
    
    return new_chunk(new_pages, FREE, order_from_size(size_to_allocate), NULL, NULL);
}

// should use sbrk
void* morecore(size_t size) {
    
    #ifdef USE_SBRK
        sbrk(size);
        return (void*)((long)sbrk(0) - size);
    #else
        return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    #endif
}

// given a free chunk and a target order, split the free chunk until a chunk of the target order
// has been reached
struct chunk_mdata* cascade_split(struct chunk_mdata* free_chunk, short int target_order) {
    short int top_order = free_chunk->order;
    
    // split the chunks in a cascading fashion
    struct chunk_mdata* chunk_to_split = free_chunk; 
    for(short int j = top_order; j > target_order; j--) {
        struct chunk_mdata* right_hand_block = split_chunk(chunk_to_split);
        right_hand_block->chunk_flag = FREE;
        free_list[j] = right_hand_block; // add the right hand chunk back to the free list
        
        // ! the left hand block is kept, to be split further !
    }
    return chunk_to_split;
}

// returns the secondary chunk of the pair after splitting them
// the primary chunk remains as chunk_to_split;
struct chunk_mdata* split_chunk(struct chunk_mdata* chunk_to_split) {
    chunk_to_split->order--;
    size_t size_of_chunk_w_header = sizeof(struct chunk_mdata) + size_from_order(chunk_to_split->order);
    void* new_chunk_addr = (void*)((long)chunk_to_split + size_of_chunk_w_header);
    
    // create the new chunk, its *next will point to the old chunks next
    // its *prev will point the old chunk
    struct chunk_mdata* n_chunk = new_chunk(new_chunk_addr, chunk_to_split->chunk_flag, chunk_to_split->order, chunk_to_split->next, chunk_to_split);
    
    // the old chunk's next needs to point to the new chunk
    chunk_to_split->next = n_chunk;
    return n_chunk;
}

struct chunk_mdata* new_chunk(void* addr, enum flag chunk_flag, short int order, struct chunk_mdata* next, struct chunk_mdata* prev) {
    struct chunk_mdata* n_chunk = (struct chunk_mdata*)(addr);
    n_chunk->chunk_flag = chunk_flag;
    n_chunk->order = order;
    n_chunk->next = next;
    n_chunk->prev = prev;

    return n_chunk;
}

size_t size_from_order(short int order) {
    return MIN_BLOCK_SIZE << order; 
}

// returns the order that can hold the requested size
// would need to use log2 if we were not using this while loop
short int order_from_size(size_t size) {
    short int i = 0;
    while((size >> i) > MIN_BLOCK_SIZE) {
        i++;
        
    }
    return i;
}
