#include <unistd.h>
enum flag {
     FREE,
     IN_USE
};
void fr33(void* ptr);
struct chunk_mdata* merge_chunks(struct chunk_mdata* lh, struct chunk_mdata* rh);

void append_to_free_list(struct chunk_mdata* chunk_to_append); 

struct chunk_mdata* get_buddy(struct chunk_mdata* chunk); 
void* mall0c(size_t size);

struct chunk_mdata* resize_heap(size_t req_size);

void* morecore(size_t size);

struct chunk_mdata* cascade_split(struct chunk_mdata* free_chunk, short int target_order); 

struct chunk_mdata* split_chunk(struct chunk_mdata* chunk_to_split); 
struct chunk_mdata* new_chunk(void* addr, enum flag chunk_flag, short int order, struct chunk_mdata* next, struct chunk_mdata* prev); 
size_t size_from_order(short int order); 

short int order_from_size(size_t size); 
