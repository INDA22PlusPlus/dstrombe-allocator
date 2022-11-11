#include <stdlib.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include "buddy.h"
#include <stdio.h>

#define MIN_BLOCK_SIZE 32
#define DEFAULT_HEAP_SIZE 4096 // so we allocate just 1 page by default

// this can't be a const, bc consts are mutable in c. lol 
// max size of a block is 2^LEVELS
#define LEVELS 16

#define USE_MMAP

struct chunk_mdata* head = NULL; 

struct chunk_mdata* free_list[LEVELS] = { NULL };

struct chunk_mdata {
    enum flag chunk_flag;
    short int order;
    struct chunk_mdata* next;
    struct chunk_mdata* prev;
};

// the implementation of free
void fr33(void* ptr) {
    struct chunk_mdata* chunk_to_free = (struct chunk_mdata*)ptr;
    struct chunk_mdata* buddy = get_buddy(chunk_to_free);

    // if the chunk already has a free buddy, they should be merged
    while (buddy && buddy->chunk_flag == FREE) {
        chunk_to_free = merge_chunks(chunk_to_free, buddy);
        buddy = get_buddy(chunk_to_free);
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

// append an item to the free list
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


// get the buddy of a given chunk
struct chunk_mdata* get_buddy(struct chunk_mdata* chunk) {
    long addr = (long)chunk;
    // buddy will be 2^order bytes to the left or right of the chunk.
    long addr_of_buddy = addr ^ (1 << chunk->order); 
    return (struct chunk_mdata*)addr_of_buddy;
}

// the actual malloc function
void* mall0c(size_t size) {
    // this pointer will be handed out to the caller
    struct chunk_mdata* chunk_to_distribute = NULL;

    if(size == 0) {
        return NULL;
    }
    short int order = order_from_size(size);
    if (!free_list[order]) {
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
    fflush(stdout);
    return (void*)((long)chunk_to_distribute + sizeof(struct chunk_mdata));;
}

// Add a new chunk whose order is at least one order grater than the largest currently allocated order
struct chunk_mdata* resize_heap(size_t req_size) {
    size_t total_size = 0;
    struct chunk_mdata* local_tail = NULL;
    while(total_size < req_size) {
        struct chunk_mdata* chunk_to_add = NULL;
        if(!head) {
            // initialize the heap
            size_t heap_size = DEFAULT_HEAP_SIZE - sizeof(struct chunk_mdata); //size_from_order(order_from_size(req_size));
            void* mc = morecore(heap_size);
            head = new_chunk(mc, FREE, order_from_size(heap_size), NULL, NULL);
            chunk_to_add = head;
        }
        else {
            // double the current size of the heap
            size_t length_of_heap = (long)get_end_of_heap() - (long)head;

            // the order right above the highest allocated order
            size_t size_to_allocate = (length_of_heap << 1) - sizeof(struct chunk_mdata);
            
            // size_t size_to_allocate = h_size > req_size ? double_heap_size : req_size; 
            void* new_pages = morecore(size_to_allocate); 
            
            chunk_to_add = new_chunk(new_pages, FREE, order_from_size(size_to_allocate), NULL, NULL);
        }   
        total_size += size_from_order(chunk_to_add->order);
        // we append from the tail so that we return a pointer to the largest chunk
        if(local_tail) {
            struct chunk_mdata* curr_chunk = local_tail;
            while(curr_chunk->prev) {
                curr_chunk = curr_chunk->prev;
            }
            curr_chunk->prev = chunk_to_add;
            chunk_to_add->next = curr_chunk;

        }
        else {
            local_tail = chunk_to_add;
        }
        append_to_free_list(chunk_to_add);
         
    }
    
    return local_tail;
}

// get the end of the heap
// either head must exist or USE_SBRK must be defined
void* get_end_of_heap() {
    #ifdef USE_SBRK
        return sbrk(0);
    #else
        struct chunk_mdata* curr_chunk = head;
        while(curr_chunk->next) {
            curr_chunk = curr_chunk->next;
        }
        // the end of the heap is the end of the final chunk
        return (void*)((long)curr_chunk + (long)(sizeof(struct chunk_mdata) + size_from_order(curr_chunk->order)));
    #endif
}

// Request more memory from the OS
void* morecore(size_t size) {

    #ifdef USE_SBRK
        // sbrk is slightly faster than mmap
        sbrk(size); // grow the heap by size bytes
        return (void*)((long)sbrk(0) - size); // sbrk(0) returns a pointer to the end of the heap
    #else
        //mmap is not as popular as sbrk, but it is more portable as brk/sbrk are not POSIX
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

        // remove the chunk from the free list
        short int is_in_free_list = 0;
        struct chunk_mdata* curr_chunk = free_list[j];
        while(curr_chunk->next) {
            if(curr_chunk == chunk_to_split) {
                is_in_free_list = 1;
            }
            curr_chunk = curr_chunk->next;
        }
        if(is_in_free_list) {
            // remove the chunk from the free list
            if(chunk_to_split->prev) {
                chunk_to_split->prev->next = chunk_to_split->next;
            }
            else {
                free_list[j] = chunk_to_split->next;
            }
            if(chunk_to_split->next) {
                chunk_to_split->next->prev = chunk_to_split->prev;
            }
        }

        struct chunk_mdata* right_hand_block = split_chunk(chunk_to_split);
        right_hand_block->chunk_flag = FREE;
        append_to_free_list(right_hand_block); // add the right hand chunk back to the free list
        
        // ! the left hand block is kept, to be split further !
    }
    return chunk_to_split;
}

// returns the secondary chunk of the pair after splitting them
// the primary chunk remains as chunk_to_split;
struct chunk_mdata* split_chunk(struct chunk_mdata* chunk_to_split) {
    chunk_to_split->order--;
    size_t size_of_chunk = size_from_order(chunk_to_split->order);
    void* new_chunk_addr = (void*)((long)chunk_to_split + size_of_chunk);
    
    // create the new chunk, its *next will point to the old chunks next
    // its *prev will point the old chunk
    struct chunk_mdata* n_chunk = new_chunk(new_chunk_addr, chunk_to_split->chunk_flag, chunk_to_split->order, chunk_to_split->next, chunk_to_split);
    
    // the old chunk's next needs to point to the new chunk
    chunk_to_split->next = n_chunk;
    return n_chunk;
}

// construct a new chunk
struct chunk_mdata* new_chunk(void* addr, enum flag chunk_flag, short int order, struct chunk_mdata* next, struct chunk_mdata* prev) {
    struct chunk_mdata* n_chunk = (struct chunk_mdata*)(addr);
    n_chunk->chunk_flag = chunk_flag;
    n_chunk->order = order;
    n_chunk->next = next;
    n_chunk->prev = prev;

    return n_chunk;
}

// given an order, return the corresponding chunk size in bytes (including the chunk metadata)
size_t size_from_order(short int order) {
    return (MIN_BLOCK_SIZE << order) + sizeof(struct chunk_mdata);
}

// returns the order that can hold the requested size (includin metadata)
// would need to use log2 if we were not using this while loop
short int order_from_size(size_t size) {
    short int i = 0;
    size_t total_size = sizeof(struct chunk_mdata) + size;
    while((total_size >> i) > MIN_BLOCK_SIZE) {
        i++;
        
    }
    return i;
}
