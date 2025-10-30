/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

static inline int ILog2(uint32 x){
	int r = -1;
	while(x){r++;x>>=1;}
	return r;
}

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here

	//initialize the page info arr & block list
	dynAllocStart = daStart;
	dynAllocEnd = daEnd;

	int NumOfPages = (daEnd - daStart) / PAGE_SIZE;

	LIST_INIT(&freePagesList);

	for (int i=0; i<= LOG2_MAX_SIZE - LOG2_MIN_SIZE; i++){
		LIST_INIT(&freeBlockLists[i]);
	}

	for (int i=0; i<NumOfPages; i++){
		pageBlockInfoArr[i].block_size =0;
		pageBlockInfoArr[i].num_of_free_blocks =0;
		LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
	}

	//Comment the following line
	//panic("initialize_dynamic_allocator() Not implemented yet");
}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
	uint32 Address = (uint32)va;

	uint32 Page_Index = (Address - dynAllocStart)/PAGE_SIZE;

	struct PageInfoElement *PageInfo = &pageBlockInfoArr[Page_Index];

	return PageInfo->block_size;

	//Comment the following line
	//panic("get_block_size() Not implemented yet");
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here

	
	if(size < DYN_ALLOC_MIN_BLOCK_SIZE){size = DYN_ALLOC_MIN_BLOCK_SIZE;}

	// Round up to the nearest power of two
	uint32 block_size = DYN_ALLOC_MIN_BLOCK_SIZE;
	while(block_size < size){block_size <<= 1;}


	int index = ILog2(block_size) - LOG2_MIN_SIZE;

	if(index < 0 || index > (LOG2_MAX_SIZE - LOG2_MIN_SIZE)){
		panic("alloc_block: Only god knows how this value became like that !!");
		return NULL;
	}

	//init block
	struct BlockElement *block = NULL;

	if (!LIST_EMPTY(&freeBlockLists[index])){

		block = LIST_FIRST(&freeBlockLists[index]);
        LIST_REMOVE(&freeBlockLists[index], block);
	
	} else if (!LIST_EMPTY(&freePagesList)) {
		struct PageInfoElement *PInfo = LIST_FIRST(&freePagesList);
		LIST_REMOVE(&freePagesList, PInfo);

		uint32 page_idx = PInfo - pageBlockInfoArr;
		void *Page_va = (void*)(dynAllocStart+(page_idx <<PGSHIFT));

		if(get_page(Page_va) < 0){
			LIST_INSERT_HEAD(&freePagesList,PInfo);
			return NULL;
		}

		PInfo->block_size = block_size;
		PInfo->num_of_free_blocks = PAGE_SIZE/block_size;

		//divide page into blocks + add to the list
		for(uint32 OFST = 0; OFST < PAGE_SIZE; OFST += block_size){
			struct BlockElement *blk = (struct BlockElement *)((uint8*)Page_va + OFST);
			LIST_INSERT_HEAD(&freeBlockLists[index],blk);
		}
	
		block = LIST_FIRST(&freeBlockLists[index]);
        LIST_REMOVE(&freeBlockLists[index], block);

	} else {

		int tmp = index + 1;
		while(tmp <= (LOG2_MAX_SIZE-LOG2_MIN_SIZE) && LIST_EMPTY(&freeBlockLists[tmp])){tmp++;}

		if(tmp <= (LOG2_MAX_SIZE-LOG2_MIN_SIZE)){

			block = LIST_FIRST(&freeBlockLists[tmp]);
			LIST_REMOVE(&freeBlockLists[tmp],block);

		}else{
			panic("alloc_block: no block available at any size");
		}

	}

	//fint the index of the page + make decrement
	uint32 Page_Index = ((uint32)block - dynAllocStart)/PAGE_SIZE;
	pageBlockInfoArr[Page_Index].num_of_free_blocks--;

	return (void*)block;


	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here

	uint32 addr = (uint32)va;
	uint32 Page_Index = (addr - dynAllocStart)/PAGE_SIZE;
	struct PageInfoElement *PageInfo = &pageBlockInfoArr[Page_Index];

	uint32 block_size = PageInfo->block_size;
	if (block_size == 0) {
        panic("free_block: page has block_size == 0");
        return;
    }

	int index = ILog2(block_size) - LOG2_MIN_SIZE;

	struct BlockElement *block = (struct BlockElement*)va;

	LIST_INSERT_HEAD(&freeBlockLists[index],block);
	
	PageInfo->num_of_free_blocks++;

	//handling after the all the page became free return it back to page
	if(PageInfo->num_of_free_blocks == PAGE_SIZE/block_size){
		//rm all blks
		struct BlockElement *blk = LIST_FIRST(&freeBlockLists[index]);
		while(blk){
			struct BlockElement *next = LIST_NEXT(blk);
			uint32 Blk_Page_Index = ((uint32)blk - dynAllocStart)/PAGE_SIZE;
			if(Blk_Page_Index == Page_Index){
				LIST_REMOVE(&freeBlockLists[index], blk);
			}
			blk = next;
		}

		void *Page_va = (void*)(dynAllocStart + (Page_Index << PGSHIFT));
		return_page(Page_va);

		PageInfo->block_size = 0;
		PageInfo->num_of_free_blocks = 0;
		LIST_INSERT_HEAD(&freePagesList, PageInfo);
	}
	//Comment the following line
	//panic("free_block() Not implemented yet");
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}
