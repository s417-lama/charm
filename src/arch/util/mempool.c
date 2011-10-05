
/** 

Memory pool implementation , It is only good for Charm++ usage. The first 64 bytes provides additional information. sizeof(int)- size of this block(free or allocated), next gni_mem_handle_t, then void** point to the next available block. 

Written by Yanhua Sun 08-27-2011
Generalized by Gengbin Zheng  10/5/2011

*/

#define MEMPOOL_DEBUG   0

#define POOLS_NUM       2
#define MAX_INT        2147483647

#include "mempool.h"

static      size_t     expand_mem = 1024ll*1024*16;



mempool_type *init_mempool(void *pool, size_t pool_size, gni_mem_handle_t mem_hndl)
{
    mempool_type *mptr;
    mempool_header *header;
    gni_return_t status;

    mptr = (mempool_type*)pool;
    mptr->mempools_head.mempool_ptr = pool;
    mptr->mempools_head.mem_hndl = mem_hndl;
    mptr->mempools_head.next = NULL;
    header = (mempool_header *) ((char*)pool+sizeof(mempool_type));
    mptr->freelist_head = sizeof(mempool_type);
//printf("[%d] pool: %p  free: %p\n", myrank, pool, header);
    header->size = pool_size-sizeof(mempool_type)-sizeof(mempool_header);
    header->mem_hndl = mem_hndl;
    header->next_free = 0;
    return mptr;
}

void kill_allmempool(mempool_type *mptr)
{
    gni_return_t status;
    mempool_block *current, *mempools_head;

    current = mempools_head = &(mptr->mempools_head);

    while(mempools_head!= NULL)
    {
#if CMK_CONVERSE_GEMINI_UGNI
        status = GNI_MemDeregister(nic_hndl, &(mempools_head->mem_hndl));
        GNI_RC_CHECK("Mempool de-register", status);
#endif
        //printf("[%d] free mempool:%p\n", CmiMyPe(), mempools_head->mempool_ptr);
        current=mempools_head;
        mempools_head = mempools_head->next;
        free(current->mempool_ptr);
    }
}

// append size before the real memory buffer
void*  mempool_malloc(mempool_type *mptr, int size, int expand)
{
    int     bestfit_size = MAX_INT; //most close size 
    gni_return_t    status;
    size_t    *freelist_head = &mptr->freelist_head;
    mempool_header    *freelist_head_ptr = mptr->freelist_head?(mempool_header*)((char*)mptr+mptr->freelist_head):NULL;
    mempool_header    *current = freelist_head_ptr;
    mempool_header    *previous = NULL;
    mempool_header    *bestfit = NULL;
    mempool_header    *bestfit_previous = NULL;
    mempool_block     *mempools_head = &(mptr->mempools_head);
    int     expand_size;

#if  MEMPOOL_DEBUG
    CmiPrintf("[%d] request malloc from pool: %p  free_head: %p %d for size %d, \n", CmiMyPe(), mptr, freelist_head_ptr, mptr->freelist_head, size);
#endif

#if 1
    while(current!= NULL)     /* best fit */
    {
#if  MEMPOOL_DEBUG
        CmiPrintf("[%d] current=%p size:%d \n", CmiMyPe(), current, current->size);
#endif
        if(current->size >= size && current->size < bestfit_size)
        {
            bestfit_size = current->size;
            bestfit = current;
            bestfit_previous = previous;
        }
        previous = current;
        current = current->next_free?(mempool_header*)((char*)mptr + current->next_free):NULL;
    }
#else
    while(current!= NULL)             /*  first fit */
    {
#if  MEMPOOL_DEBUG
        CmiPrintf("[%d] current=%p size:%d ->%p \n", CmiMyPe(), current, current->size, (char*)current+current->size);
#endif
        CmiAssert(current->size != 0);
        if(current->size >= size)
        {
            bestfit_size = current->size;
            bestfit = current;
            bestfit_previous = previous;
            break;
        }
        previous = current;
        current = current->next_free?(mempool_header*)((char*)mptr + current->next_free):NULL;
    }
#endif

    if(bestfit == NULL)
    {
        mempool_block   *expand_pool;
        void *pool;

        if (!expand) return NULL;

        expand_size = expand_mem>size ? expand_mem:2*size; 
        pool = memalign(ALIGNBUF, expand_size);
        expand_pool = (mempool_block*)pool;
        expand_pool->mempool_ptr = pool;
        printf("[%d] No memory has such free empty chunck of %d. expanding %p (%d)\n", CmiMyPe(), size, expand_pool->mempool_ptr, expand_size);
#if CMK_CONVERSE_GEMINI_UGNI
        status = MEMORY_REGISTER(onesided_hnd, nic_hndl, expand_pool->mempool_ptr, expand_size,  &(expand_pool->mem_hndl), &omdh);
        GNI_RC_CHECK("Mempool register", status);
#endif
        expand_pool->next = NULL;
        while (mempools_head->next != NULL) mempools_head = mempools_head->next;
        mempools_head->next = expand_pool;

        bestfit = (mempool_header*)((char*)expand_pool->mempool_ptr + sizeof(mempool_block));
        bestfit->size = expand_size-sizeof(mempool_block);
        bestfit->mem_hndl = expand_pool->mem_hndl;
        bestfit->next_free = 0;
        bestfit_size = expand_size;
#if 0
        current = freelist_head;
        while(current!= NULL && current < bestfit )
        {
          previous = current;
          current = current->next;
        }
#else
        CmiAssert(bestfit > previous);
#endif
        bestfit_previous = previous;
        if (previous == NULL)
           *freelist_head = (char*)bestfit - (char*)mptr;
        else
           previous->next_free = (char*)bestfit-(char*)mptr;
    }

    bestfit->size = size;
    if(bestfit_size > size) //deduct this entry 
    {
        mempool_header *ptr = (mempool_header *)((char*)bestfit + size);
        ptr->size = bestfit_size - size;
        ptr->mem_hndl = bestfit->mem_hndl;
        ptr->next_free = bestfit->next_free;
        if(bestfit == freelist_head_ptr)
           *freelist_head = (char*)ptr - (char*)mptr;
        if(bestfit_previous != NULL)
           bestfit_previous->next_free = (char*)ptr - (char*)mptr;
    }
    else {  
          //delete this free entry
        if(bestfit == freelist_head_ptr)
            *freelist_head = freelist_head_ptr->next_free;
        else
            bestfit_previous->next_free = bestfit->next_free;
    }
#if  MEMPOOL_DEBUG
    printf("[%d] ++MALLOC served: %d, ptr:%p\n", CmiMyPe(), size, bestfit);
printf("[%d] freelist_head in malloc  offset:%d free_head: %ld %ld %d %d\n", myrank, (char*)bestfit-(char*)mptr, *freelist_head, ((mempool_header*)((char*)mptr+*freelist_head))->next_free, bestfit_size, size);
#endif
    CmiAssert(*freelist_head >= 0);
    return (char*)bestfit;
}

//sorted free_list and merge it if it become continous 
void mempool_free(mempool_type *mptr, void *ptr_free)
{
    int i;
    int merged = 0;
    int free_size;
    void *free_lastbytes_pos;
    mempool_block     *mempools_head;
    size_t    *freelist_head;
    mempool_header    *freelist_head_ptr;
    mempool_header    *current;
    mempool_header *previous = NULL;
    mempool_header *to_free = (mempool_header *)ptr_free;

    mempools_head = &(mptr->mempools_head);
    freelist_head = &mptr->freelist_head;
    freelist_head_ptr = mptr->freelist_head?(mempool_header*)((char*)mptr+mptr->freelist_head):NULL;
    current = freelist_head_ptr;

    free_size = to_free->size;
    free_lastbytes_pos = (char*)ptr_free +free_size;

#if  MEMPOOL_DEBUG
    printf("[%d] INSIDE FREE ptr=%p, size=%d freehead=%p mutex: %p\n", CmiMyPe(), ptr_free, free_size, freelist_head, mptr->mutex);
#endif
    
    while(current!= NULL && current < to_free )
    {
#if  MEMPOOL_DEBUG
        CmiPrintf("[%d] previous=%p, current=%p size:%d %p\n", CmiMyPe(), previous, current, current->size, (char*)current+current->size);
#endif
        previous = current;
        current = current->next_free?(mempool_header*)((char*)mptr + current->next_free):NULL;
    }
#if  MEMPOOL_DEBUG
    if (current) CmiPrintf("[%d] previous=%p, current=%p size:%d %p\n", CmiMyPe(), previous, current, current->size, free_lastbytes_pos);
#endif
    //continuos with previous free space 
    if(previous!= NULL && (char*)previous+previous->size == ptr_free &&  memcmp(&previous->mem_hndl, &to_free->mem_hndl, sizeof(gni_mem_handle_t))==0 )
    {
        previous->size +=  free_size;
        merged = 1;
    }
    else if(current!= NULL && free_lastbytes_pos == current && memcmp(&current->mem_hndl, &to_free->mem_hndl, sizeof(gni_mem_handle_t))==0)
    {
        to_free->size += current->size;
        to_free->next_free = current->next_free;
        current = to_free;
        merged = 1;
        if(previous == NULL)
            *freelist_head = (char*)current - (char*)mptr;
        else
            previous->next_free = (char*)to_free - (char*)mptr;
    }
    //continous, merge
    if(merged) {
       if (previous!= NULL && current!= NULL && (char*)previous + previous->size  == (char *)current && memcmp(&previous->mem_hndl, &current->mem_hndl, sizeof(gni_mem_handle_t))==0)
      {
         previous->size += current->size;
         previous->next_free = current->next_free;
      }
    }
    else {
          // no merge to previous, current, create new entry
        to_free->next_free = current?(char*)current - (char*)mptr: 0;
        if(previous == NULL)
            *freelist_head = (char*)to_free - (char*)mptr;
        else
            previous->next_free = (char*)to_free - (char*)mptr;
    }
#if  MEMPOOL_DEBUG
    printf("[%d] Memory free done %p, freelist_head=%p\n", CmiMyPe(), ptr_free,  freelist_head);
#endif

    CmiAssert(*freelist_head >= 0);
}

