#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
#define PAGE_ALLOCATOR_AREA_START (KERNEL_HEAP_START + dynAllocEnd + PAGE_SIZE)
#define NUM_OF_KHEAP_ALLOCATION_PAGES ((KERNEL_HEAP_MAX - PAGE_ALLOCATOR_AREA_START) / PAGE_SIZE)
#define PTEs_KERNEL (PERM_PRESENT | PERM_USED | PERM_WRITEABLE | PERM_BUFFERED) // page table entries of the kernel

int allocate_page_to_map(uint32 va, uint32 perm);
uint32 custom_fit(uint32 required_pages);
uint32 split_segment(struct kheapPageSegment *segment, uint32 required_pages);
//============================================
// PAGE ALLOCATOR TRACKING STRUCTURES
//============================================
struct free_pages_segments free_pages_segments;
struct allocated_pages_segments allocated_pages_segments;

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
// Remember to initialize locks (if any)
struct kspinlock kheap_block_lock;

void kheap_init()
{

	//==================================================================================
	// DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
	}
	//==================================================================================
	//==================================================================================

	// init the free list and allocated segments list
	// struct kheapPageSegment *initial_segment = (struct kheapPageSegment *)kmalloc(sizeof(struct kheapPageSegment));
	LIST_INIT(&free_pages_segments);
	LIST_INIT(&allocated_pages_segments);

	// Initialize locks the page and block locks
	init_kspinlock(&kheap_block_lock, "kheap_block_lock");
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void *va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void *va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//* to allocate page to the physical frame
int allocate_page_to_frame(uint32 va, uint32 perm)
{
	uint32 *pageTable = NULL;
	struct FrameInfo *frame_info = get_frame_info(ptr_page_directory, va, &pageTable);

	if (frame_info != NULL)
	{
		panic("allocate_page_to_map() is trying to allocate frame that is allready taken");
		return -1;
	}

	int status = allocate_frame(&frame_info);
	if (status == E_NO_MEM)
	{
		return -1;
	}

	status = map_frame(ptr_page_directory, frame_info, va, perm);
	if (status == E_NO_MEM)
	{
		free_frame(frame_info);
		return -1;
	}

	return 0; ///< in case of success
}

uint32 split_segment(struct kheapPageSegment *segment, uint32 required_pages)
{

	if (segment == NULL || segment->pageCount < required_pages)
		return 1;

	struct kheapPageSegment *new_segment = (struct kheapPageSegment *)kmalloc(sizeof(struct kheapPageSegment));
	if (new_segment == NULL)
		return 1;

	new_segment->pageCount = required_pages;
	new_segment->startPage_va = segment->startPage_va;
	segment->startPage_va = segment->startPage_va + required_pages * PAGE_SIZE;

	segment->pageCount = segment->pageCount - required_pages;

	for (uint32 i = 0; i < new_segment->pageCount; i++)
	{
		uint32 va = new_segment->startPage_va + i * PAGE_SIZE;
		int status = allocate_page_to_frame(va, PTEs_KERNEL);
		if (status != 0)
		{
			// Unmap allocated frame in case of failure
			for (uint32 j = 0; j < i; j++)
			{
				uint32 rva = new_segment->startPage_va + j * PAGE_SIZE;
				unmap_frame(ptr_page_directory, ROUNDDOWN(rva, PAGE_SIZE));
			}
			kfree(new_segment);
			return 1;
		}
	}
	LIST_INSERT_TAIL(&allocated_pages_segments, new_segment);
	return new_segment->startPage_va;
}

//* the strategy fo allocate Block of pages (Segments of pages)
uint32 custom_fit(uint32 required_pages)
{

	// make a page block with needed sise
	struct kheapPageSegment *segment = NULL;
	if (segment == NULL)
		return 1;

	// means that no free segments and no enough space to break
	if (LIST_EMPTY(&free_pages_segments) && kheapPageAllocBreak + required_pages * PAGE_SIZE >= KERNEL_HEAP_MAX)
	{
		return 1;
	}

	// Exact-fit
	LIST_FOREACH(segment, &free_pages_segments)
	{
		if (segment->pageCount == required_pages)
		{
			for (uint32 i = 0; i < required_pages; i++)
			{
				uint32 va = segment->startPage_va + i * PAGE_SIZE;
				int status = allocate_page_to_frame(va, PTEs_KERNEL);
				if (status != 0)
				{
					return -1;
				}
			}
			LIST_REMOVE(&free_pages_segments, segment);
			LIST_INSERT_TAIL(&allocated_pages_segments, segment);
			return segment->startPage_va;
		}
	}

	// Worst-fit
	struct kheapPageSegment *max_sized_segment = NULL;
	LIST_FOREACH(segment, &free_pages_segments)
	{
		if (segment->pageCount >= required_pages)
		{
			if (max_sized_segment == NULL || segment->pageCount > max_sized_segment->pageCount)
			{
				max_sized_segment = segment;
			}
		}
	}

	if (max_sized_segment->pageCount != NULL)
	{
		uint32 split_status = split_segment(max_sized_segment, required_pages);
		if (split_status == 1)
		{
			return 1;
		}
		return split_status;
	}

	// Break-update
	if (kheapPageAllocBreak + required_pages * PAGE_SIZE < KERNEL_HEAP_MAX)
	{
		for (uint32 i = 0; i < required_pages; i++)
		{
			uint32 va = kheapPageAllocBreak + i * PAGE_SIZE;
			int status = allocate_page_to_frame(va, PTEs_KERNEL);
			if (status != 0)
			{
				return 1;
			}
		} // FOR LOOP END

		segment->pageCount = required_pages;
		segment->startPage_va = kheapPageAllocBreak;
		LIST_INSERT_TAIL(&allocated_pages_segments, segment);

		kheapPageAllocBreak += required_pages * PAGE_SIZE;
		return segment->startPage_va;
	}

	return 1;
	// TODO: **ERROR** can not allocate return 1
}

//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void *kmalloc(unsigned int size)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	if (size == 0)
		return NULL;

	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		// we need to make lock while allocation
		bool block_lock_is_in_hold = holding_kspinlock(&kheap_block_lock); // that return 0 if if free

		if (!block_lock_is_in_hold)
			acquire_kspinlock(&kheap_block_lock);

		void *va = alloc_block(size);
		release_kspinlock(&kheap_block_lock);

		if (va != NULL)
			return va;
		else
			return NULL;
	}

	bool is_holding_page_lock = holding_kspinlock(&MemFrameLists.mfllock);

	if (!is_holding_page_lock)
	{
		acquire_kspinlock(&MemFrameLists.mfllock);
	}

	// Convert given size from bytes to pages
	uint32 required_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	// struct kheapPagesBlock *block = NULL;

	// TODO: int custom_fit(uint32 required_pages); , return the status 0 sucsess , 1 fiels , -1 panic
	uint32 result = custom_fit(required_pages);

	release_kspinlock(&MemFrameLists.mfllock);

	if (result == 1)
	{
		return NULL;
	}

	return (void *)result;

	// return NULL if it failer to allocate
	return NULL;

	// Comment the following line
	// kpanic_into_prompt("kmalloc() is not implemented yet...!!");

	// TODO: [PROJECT'25.BONUS#3] FAST PAGE ALLOCATOR
}

// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
//=================================
// Details:
// 	- takes the virtual address allocated by kmalloc.
// 	- check if the virtual address belongs to a block or pages and free it accordingly.
//
void kfree(void *virtual_address)
{
	cprintf("kfree is called with va %x\n", (uint32)virtual_address);
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	// bool lock_is_in_hold = holding_kspinlock(&kheap_block_lock); // that return 0 if if free
	// if (!lock_is_in_hold)
	// 	acquire_kspinlock(&kheap_block_lock);

	// uint32 va = (uint32)virtual_address;
	// if (va == NULL || va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX)
	// {
	// 	panic("kfree: virtual_address is invalid");
	// 	release_kspinlock(&kheap_block_lock);
	// 	return;
	// }

	// uint32 block_size = get_block_size(va);
	// uint32 *ptr;
	// uint32 *ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);

	// if (ptr_page_table == TABLE_NOT_EXIST)
	// {
	// 	create_page_table(ptr_page_directory, va);
	// 	ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	// }

	// struct FrameInfo *ptr_frame_info = get_frame_info(ptr_page_directory, va, ptr_page_table);
	// if (block_size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	// 	free_block(va);
	// else
	// {
	// 	uint32 number_of_pages = ROUNDUP(block_size, PAGE_SIZE) / PAGE_SIZE;
	// 	for (uint32 i = 0; i < number_of_pages; i++)
	// 	{
	// 		free_frame(ptr_frame_info);
	// 		// unmap_frame(ptr_page_directory, ROUNDDOWN(va + i * PAGE_SIZE, PAGE_SIZE));
	// 	}
	// 	decrement_references(ptr_frame_info);
	// }
	// release_kspinlock(&kheap_block_lock);
	// panic("kfree() is not implemented yet...!!");
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	// struct FrameInfo *ptr_frame_info = to_frame_info(physical_address);
	// uint32 va;
	// Comment the following line
	// panic("kheap_virtual_address() is not implemented yet...!!");

	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	// uint32 va = (uint32)virtual_address;
	// uint32 *ptr;
	// uint32 *ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	// if (ptr_page_table == TABLE_NOT_EXIST)
	// {
	// 	create_page_table(ptr_page_directory, va);
	// 	ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	// }
	// struct FrameInfo *ptr_frame_info = get_frame_info(ptr_page_directory, va, ptr_page_table);
	// int physical_address = allocate_frame(ptr_frame_info);
	// if (physical_address == -1)
	// 	return 0;
	// else
	// 	return physical_address;
	// panic("kheap_physical_address() is not implemented yet...!!");

	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	// TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
	// Your code is here
	// Comment the following line
	panic("krealloc() is not implemented yet...!!");
}
