#include <inc/lib.h>

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
struct free_Upages_segments free_Upages_segments;
struct allocated_Upages_segments allocated_Upages_segments;

#define MAX_SEGMENTS 1048576
//*
void *custom_fit(uint32 required_pages);
uint32 split_segment(struct uheapPageSegment *segment, uint32 required_pages, uint32 *out_va);
//*
struct uheapPageSegment *find_page_segment(uint32 va);
void merge_free_segments(struct uheapPageSegment *segment);
int update_break_after_free(void);

//*
static struct uheapPageSegment usegment_pool[MAX_SEGMENTS];
static int usegment_pool_used[MAX_SEGMENTS];
//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
void uheap_init()
{
	if (__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;

		__firstTimeFlag = 0;
	}

	LIST_INIT(&free_Upages_segments);
	LIST_INIT(&allocated_Upages_segments);
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void *va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER | PERM_WRITEABLE | PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void *va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//* i DO NOT know what is this function DO
static struct uheapPageSegment *allocate_segment_struct(void)
{
	for (int i = 0; i < MAX_SEGMENTS; ++i)
	{
		if (!usegment_pool_used[i])
		{
			usegment_pool_used[i] = 1;
			usegment_pool[i].prev_next_info.le_next = NULL;
			usegment_pool[i].prev_next_info.le_prev = NULL;
			usegment_pool[i].pageCount = 0;
			usegment_pool[i].startPage_va = 0;
			return &usegment_pool[i];
		}
	}
	return NULL;
}

//* Split a segment into two segments
uint32 split_segment(struct uheapPageSegment *segment, uint32 size, uint32 *out_va)
{
	uint32 required_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;

	if (segment == NULL || segment->pageCount < required_pages)
		return 1;

	struct uheapPageSegment *new_segment = allocate_segment_struct();
	if (new_segment == NULL)
		return 1;

	new_segment->pageCount = required_pages;
	new_segment->startPage_va = segment->startPage_va;
	segment->startPage_va = segment->startPage_va + required_pages * PAGE_SIZE;
	segment->pageCount = segment->pageCount - required_pages;

	sys_allocate_user_mem(new_segment->startPage_va, size);

	LIST_INSERT_TAIL(&allocated_Upages_segments, new_segment);
	*out_va = new_segment->startPage_va;
	return 0;
}

//* Custom fit strategy implementation
void *custom_fit(uint32 size)
{

	if (size == 0)
		return NULL;

	uint32 required_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;

	// means that no free segments and no enough space to break
	if (LIST_EMPTY(&free_Upages_segments) && uheapPageAllocBreak + required_pages * PAGE_SIZE > USER_HEAP_MAX)
	{
		return NULL;
	}

	struct uheapPageSegment *segment = NULL;
	//* Exact-fit
	LIST_FOREACH(segment, &free_Upages_segments)
	{
		if (segment->pageCount == required_pages)
		{
			sys_allocate_user_mem(segment->startPage_va, size);
			LIST_REMOVE(&free_Upages_segments, segment);
			LIST_INSERT_TAIL(&allocated_Upages_segments, segment);
			return (void *)segment->startPage_va;
		}
	}

	//* Worst-fit
	struct uheapPageSegment *max_sized_segment = NULL;
	LIST_FOREACH(segment, &free_Upages_segments)
	{
		if (segment->pageCount >= required_pages)
		{
			if (max_sized_segment == NULL || segment->pageCount > max_sized_segment->pageCount)
			{
				max_sized_segment = segment;
			}
		}
	}

	if (max_sized_segment != NULL)
	{
		uint32 result_va;
		uint32 split_status = split_segment(max_sized_segment, size, &result_va);
		if (split_status == 1)
		{
			return NULL;
		}
		// sys_allocate_user_mem(result_va, size);
		return (void *)result_va;
	}

	//* Break-update
	if ((uheapPageAllocBreak + (required_pages * PAGE_SIZE)) <= USER_HEAP_MAX)
	{

		uint32 new_break = uheapPageAllocBreak + required_pages * PAGE_SIZE;

		// Check for break overflow
		if (new_break < uheapPageAllocBreak || new_break > USER_HEAP_MAX)
		{
			return NULL;
		}

		struct uheapPageSegment *newseg = allocate_segment_struct();
		if (newseg == NULL)
		{
			return NULL;
		}

		newseg->pageCount = required_pages;
		newseg->startPage_va = uheapPageAllocBreak;

		//* Allocate user memory for the new segment
		sys_allocate_user_mem(newseg->startPage_va, size);

		uheapPageAllocBreak = new_break;

		LIST_INSERT_TAIL(&allocated_Upages_segments, newseg);

		return (void *)newseg->startPage_va;
	}

	return NULL;
	// TODO: **ERROR** can not allocate return 1
}

//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void *malloc(uint32 size)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0)
		return NULL;
	//==============================================================
	// TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc
	// Your code is here
	// if the size is less than or equal to the max block size allocate using the dynamic allocator
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		return alloc_block(size);
	}
	// Convert given size from bytes to pages

	void *result = custom_fit(size);

	return result;
	// Comment the following line
	// panic("malloc() is not implemented yet...!!");
}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================

struct uheapPageSegment *find_page_segment(uint32 va)
{

	struct uheapPageSegment *seg_iter = NULL;
	LIST_FOREACH(seg_iter, &allocated_Upages_segments)
	{
		if (seg_iter->startPage_va == va)
		{
			return seg_iter;
		}
	}
	return seg_iter;
}

void merge_free_segments(struct uheapPageSegment *segment)
{
	struct uheapPageSegment *seg1 = NULL, *seg2 = NULL;
	bool merge_down = 0;

	// Check if we can merge with previous segment
	if (segment->startPage_va > USER_HEAP_START)
	{
		LIST_FOREACH(seg1, &free_Upages_segments)
		{
			if (seg1->startPage_va + (seg1->pageCount * PAGE_SIZE) == segment->startPage_va)
			{
				seg2 = seg1;
				merge_down = 1;
				seg1->pageCount += segment->pageCount;
				break;
			}
		}
	}

	// Check if we can merge with next segment
	seg1 = NULL;
	if (!merge_down)
	{
		LIST_FOREACH(seg1, &free_Upages_segments)
		{
			if (seg1->startPage_va == (segment->startPage_va + (segment->pageCount * PAGE_SIZE)))
			{
				seg1->startPage_va = segment->startPage_va;
				seg1->pageCount += segment->pageCount;
				return;
			}
		}
	}
	else
	{
		LIST_FOREACH(seg1, &free_Upages_segments)
		{
			if (seg1->startPage_va == (seg2->startPage_va + (seg2->pageCount * PAGE_SIZE)))
			{
				seg1->startPage_va = seg2->startPage_va;
				seg1->pageCount += seg2->pageCount;
				LIST_REMOVE(&free_Upages_segments, seg2);
				return;
			}
		}
	}

	if (merge_down)
		return;

	LIST_INSERT_HEAD(&free_Upages_segments, segment);
}

int update_break_after_free(void)
{
	struct uheapPageSegment *seg;
	LIST_FOREACH(seg, &free_Upages_segments)
	{
		uint32 seg_end = seg->startPage_va + seg->pageCount * PAGE_SIZE;
		if (seg_end == uheapPageAllocBreak)
		{
			uheapPageAllocBreak = seg->startPage_va;
			return 0;
		}
	}
	return 1;
}

//=================================
void free(void *virtual_address)
{
	// TODO: [PROJECT'25.IM#2] USER HEAP - #3 free
	// Your code is here
	uint32 virtual_address_uint = (uint32)virtual_address;
	if (virtual_address_uint == 0 || virtual_address_uint < USER_HEAP_START || virtual_address_uint >= USER_HEAP_MAX)
	{
		panic("free: invalid virtual address");
		return;
	}
	if (virtual_address_uint >= USER_HEAP_START && virtual_address_uint < USER_HEAP_START + DYN_ALLOC_MAX_SIZE)
	{
		free_block(virtual_address);
		return;
	}
	else if (virtual_address_uint >= uheapPageAllocStart && virtual_address_uint < USER_HEAP_MAX)
	{
		virtual_address = ROUNDDOWN(virtual_address, PAGE_SIZE);

		struct uheapPageSegment *segment = find_page_segment((uint32)virtual_address);
		if (segment == NULL)
		{
			panic("free: segment not found for the given virtual address");
			return;
		}

		// Free the user memory associated with the segment
		sys_free_user_mem(segment->startPage_va, segment->pageCount * PAGE_SIZE);

		LIST_REMOVE(&allocated_Upages_segments, segment);
		merge_free_segments(segment);
		update_break_after_free();
		return;
	}
	// Comment the following line
	panic("there is an error in free function");
	// panic("free() is not implemented yet...!!");
}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void *smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0)
		return NULL;
	//==============================================================

	void *virsual_adress = malloc(ROUNDUP(size, PAGE_SIZE));
	if (virsual_adress == NULL)
	{
		return NULL;
	}
	uint32 ID = sys_create_shared_object(sharedVarName, ROUNDUP(size, PAGE_SIZE), isWritable, virsual_adress);
	if (ID == E_SHARED_MEM_EXISTS || ID == E_NO_SHARE)
	{
		free(virsual_adress);
		return NULL;
	}

	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	// Your code is here
	// Comment the following line
	// panic("smalloc() is not implemented yet...!!");
	return virsual_adress;
}

//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void *sget(int32 ownerEnvID, char *sharedVarName)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	int size = sys_size_of_shared_object(ownerEnvID, sharedVarName);
	if (size == 0 || size == E_SHARED_MEM_NOT_EXISTS)
	{
		return NULL;
	}
	void *virsual_aderss = malloc(ROUNDUP(size, PAGE_SIZE));
	if (virsual_aderss == NULL)
	{
		return NULL;
	}
	uint32 ID = sys_get_shared_object(ownerEnvID, sharedVarName, virsual_aderss);

	if (ID == E_SHARED_MEM_EXISTS)
	{
		free(virsual_aderss);
		return NULL;
	}

	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	// Your code is here
	// Comment the following line
	// panic("sget() is not implemented yet...!!");
	return virsual_aderss;
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}

//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void *virtual_address)
{
	// TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	// Your code is here
	// Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}

//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
