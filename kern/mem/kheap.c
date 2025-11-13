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
#define PTEs_KERNEL (PERM_PRESENT | PERM_USED | PERM_WRITEABLE) // page table entries of the kernel
#define MAX_SEGMENTS 1048576									//(KERNEL_HEAP_MAX - (KERNEL_HEAP_START+dynAllocEnd+PAGE_SIZE)) / PAGE_SIZE)

int allocate_page_to_frame(uint32 va, uint32 perm);
void *custom_fit(uint32 required_pages);
uint32 split_segment(struct kheapPageSegment *segment, uint32 required_pages, uint32 *out_va);
void merge_free_segments(void);
int update_break_after_free(void);
struct kheapPageSegment *find_page_segment(uint32 va);
//*
static struct kheapPageSegment segment_pool[MAX_SEGMENTS];
static int segment_pool_used[MAX_SEGMENTS];

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
struct kspinlock kheap_page_lock;

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
	init_kspinlock(&kheap_page_lock, "kheap_page_lock");
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

//* i DO NOT know what is this function DO
static struct kheapPageSegment *allocate_segment_struct(void)
{
	for (int i = 0; i < MAX_SEGMENTS; ++i)
	{
		if (!segment_pool_used[i])
		{
			segment_pool_used[i] = 1;
			segment_pool[i].prev_next_info.le_next = NULL;
			segment_pool[i].prev_next_info.le_prev = NULL;
			segment_pool[i].pageCount = 0;
			segment_pool[i].startPage_va = 0;
			return &segment_pool[i];
		}
	}
	return NULL;
}

//* i DO NOT know what is this function DO
static void free_segment_struct(struct kheapPageSegment *seg)
{
	if (seg == NULL)
		return;

	int idx = seg - segment_pool;
	if (idx < 0 || idx >= MAX_SEGMENTS)
		return;

	segment_pool_used[idx] = 0;
	seg->prev_next_info.le_next = NULL;
	seg->prev_next_info.le_prev = NULL;
	seg->pageCount = 0;
	seg->startPage_va = 0;
}

//* to allocate page to the physical frame
int allocate_page_to_frame(uint32 va, uint32 perm)
{
	uint32 *pageTable = NULL;
	struct FrameInfo *frame_info = get_frame_info(ptr_page_directory, va, &pageTable);

	if (frame_info != NULL)
	{
		panic("allocate_page_to_fram() is trying to allocate frame that is allready taken");
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

//* Split a segment into two segments
uint32 split_segment(struct kheapPageSegment *segment, uint32 required_pages, uint32 *out_va)
{

	if (segment == NULL || segment->pageCount < required_pages)
		return 1;

	struct kheapPageSegment *new_segment = allocate_segment_struct();
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
		if (status == -1)
		{
			// Unmap allocated frame in case of failure
			for (uint32 j = 0; j < i; j++)
			{
				uint32 rva = new_segment->startPage_va + j * PAGE_SIZE;
				unmap_frame(ptr_page_directory, ROUNDDOWN(rva, PAGE_SIZE));
			}
			free_segment_struct(new_segment); // kfree(new_segment);
			return 1;
		}
	}
	LIST_INSERT_TAIL(&allocated_pages_segments, new_segment);
	*out_va = new_segment->startPage_va;
	return 0; // success
}

//* the strategy fo allocate Block of pages (Segments of pages)
void *custom_fit(uint32 required_pages)
{

	if (required_pages == 0)
		return NULL;

	// means that no free segments and no enough space to break
	if (LIST_EMPTY(&free_pages_segments) && kheapPageAllocBreak + required_pages * PAGE_SIZE > KERNEL_HEAP_MAX)
	{
		return NULL;
	}

	struct kheapPageSegment *segment = NULL;
	// Exact-fit
	LIST_FOREACH(segment, &free_pages_segments)
	{
		if (segment->pageCount == required_pages)
		{
			for (uint32 i = 0; i < required_pages; i++)
			{
				uint32 va = segment->startPage_va + i * PAGE_SIZE;
				int status = allocate_page_to_frame(va, PTEs_KERNEL);
				if (status == -1)
				{
					for (uint32 j = 0; j < i; j++)
					{
						uint32 rva = segment->startPage_va + j * PAGE_SIZE;
						unmap_frame(ptr_page_directory, rva);
					}
					return NULL;
				}
			}
			LIST_REMOVE(&free_pages_segments, segment);
			LIST_INSERT_TAIL(&allocated_pages_segments, segment);
			return (void *)segment->startPage_va;
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

	if (max_sized_segment != NULL)
	{
		uint32 result_va;
		uint32 split_status = split_segment(max_sized_segment, required_pages, &result_va);
		if (split_status == 1)
		{
			return NULL;
		}
		return (void *)result_va;
	}

	// Break-update
	if ((kheapPageAllocBreak + (required_pages * PAGE_SIZE)) <= KERNEL_HEAP_MAX)
	{

		uint32 new_break = kheapPageAllocBreak + required_pages * PAGE_SIZE;

		// Check for break overflow
		if (new_break < kheapPageAllocBreak || new_break > KERNEL_HEAP_MAX)
		{
			return NULL;
		}

		for (uint32 i = 0; i < required_pages; i++)
		{
			uint32 va = kheapPageAllocBreak + i * PAGE_SIZE;
			int status = allocate_page_to_frame(va, PTEs_KERNEL);
			if (status == -1)
			{
				for (uint32 j = 0; j < i; j++)
				{
					uint32 rva = kheapPageAllocBreak + j * PAGE_SIZE;
					unmap_frame(ptr_page_directory, rva);
				}
				return NULL;
			}
		} // FOR LOOP END

		struct kheapPageSegment *newseg = allocate_segment_struct();
		if (newseg == NULL)
		{
			for (uint32 j = 0; j < required_pages; j++)
			{
				uint32 rva = kheapPageAllocBreak + j * PAGE_SIZE;
				unmap_frame(ptr_page_directory, rva);
			}
			return NULL;
		}

		newseg->pageCount = required_pages;
		newseg->startPage_va = kheapPageAllocBreak;

		kheapPageAllocBreak = new_break;

		LIST_INSERT_TAIL(&allocated_pages_segments, newseg);

		return (void *)newseg->startPage_va;
	}

	return NULL;
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

		if (!block_lock_is_in_hold)
			release_kspinlock(&kheap_block_lock);

		if (va != NULL)
			return va;
		else
			return NULL;
	}

	bool is_holding_page_lock = holding_kspinlock(&kheap_page_lock);

	if (!is_holding_page_lock)
	{
		acquire_kspinlock(&kheap_page_lock);
	}

	// Convert given size from bytes to pages
	uint32 required_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	// struct kheapPagesBlock *block = NULL;

	// TODO: int custom_fit(uint32 required_pages); , return the status 0 sucsess , 1 fail , -1 panic
	void *result = custom_fit(required_pages);

	if (!is_holding_page_lock)
		release_kspinlock(&kheap_page_lock);

	return result;

	// Comment the following line
	// kpanic_into_prompt("kmalloc() is not implemented yet...!!");

	// TODO: [PROJECT'25.BONUS#3] FAST PAGE ALLOCATOR
}

// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
//=================================
// Details:
// 	- takes the virtual address allocated by kmalloc.‍‍‍‍
// 	- check if the virtual address belongs to a block or pages and free it accordingly.
//
void kfree(void *virtual_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	uint32 va = (uint32)virtual_address;

	// Validate address
	if (va == 0 || va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX)
	{
		panic("kfree: invalid virtual address");
		return;
	}

	// check if BLOCK or PAGE allocator
	if (va >= KERNEL_HEAP_START && va < KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE)
	{
		// BLOCK ALLOCATOR
		bool block_lock_held = holding_kspinlock(&kheap_block_lock);
		if (!block_lock_held)
			acquire_kspinlock(&kheap_block_lock);

		free_block((void *)va);

		if (!block_lock_held)
		{
			release_kspinlock(&kheap_block_lock);
		}
	}
	else if (va >= kheapPageAllocStart && va < KERNEL_HEAP_MAX)
	{
		// PAGE ALLOCATOR
		bool page_lock_held = holding_kspinlock(&kheap_page_lock);
		if (!page_lock_held)
			acquire_kspinlock(&kheap_page_lock);

		// Find the segment containing this VA
		struct kheapPageSegment *segment = find_page_segment(va);
		uint32 size = segment->pageCount * PAGE_SIZE;
		if (segment == NULL)
		{
			if (!page_lock_held)
				release_kspinlock(&kheap_page_lock);
			panic("kfree: address not found in allocated segments");
			return;
		}

		for (void *va = virtual_address; va < virtual_address + size; va += PAGE_SIZE)
		{
			uint32 pa = kheap_physical_address((uint32 *)va);
			struct FrameInfo *frame_info = to_frame_info(pa);

			if (!frame_info)
			{
				panic("kfree(): trying to free an unallocated frame '%x' (frame #%d)",
					  frame_info,
					  to_frame_number(frame_info));
			}

			// Should invalidate cache
			unmap_frame(ptr_page_directory, (uint32)va);
		}

		// Remove from allocated list and add to free list
		LIST_REMOVE(&allocated_pages_segments, segment);
		LIST_INSERT_HEAD(&free_pages_segments, segment);

		// TODO: Merge adjacent free segments
		merge_free_segments();
		// TODO: Update break if freeing last segment
		update_break_after_free();

		if (!page_lock_held)
			release_kspinlock(&kheap_page_lock);
	}
	else
	{
		panic("kfree: address not in any valid heap region");
	}

	// panic("kfree() is not implemented yet...!!");
}

struct kheapPageSegment *find_page_segment(uint32 va)
{
	// assert(va && (va >= PAGE_ALLOCATOR_START));
	// uint32 offset = (va - PAGE_ALLOCATOR_START) / PAGE_SIZE;
	// return heap_blocks + offset;

	struct kheapPageSegment *seg_iter = NULL;
	LIST_FOREACH(seg_iter, &allocated_pages_segments)
	{
		if (seg_iter->startPage_va == va)
		{
			return seg_iter;
		}
	}
}

void merge_free_segments(void)
{
	struct kheapPageSegment *seg1, *seg2;
	// if two segments after each othe
restart:
	LIST_FOREACH(seg1, &free_pages_segments)
	{
		LIST_FOREACH(seg2, &free_pages_segments)
		{
			if (seg1 == seg2)
				continue;
			// Check if seg1 is immediately before seg2
			if (seg1->startPage_va + seg1->pageCount * PAGE_SIZE == seg2->startPage_va)
			{
				// Merge seg2 into seg1
				seg1->pageCount += seg2->pageCount;
				LIST_REMOVE(&free_pages_segments, seg2);
				free_segment_struct(seg2);
				goto restart; // Restart to check for more merges
			}
		}
	}
}

int update_break_after_free(void)
{
	// Find the segment with highest end address
	struct kheapPageSegment *seg;
	uint32 max_end = kheapPageAllocStart;
	LIST_FOREACH(seg, &allocated_pages_segments)
	{
		uint32 seg_end = seg->startPage_va + seg->pageCount * PAGE_SIZE;
		if (seg_end == kheapPageAllocBreak)
		{
			kheapPageAllocBreak = seg->startPage_va;
			return 0;
		}
	}
	return 1;
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	struct FrameInfo *ptr_frame_info = to_frame_info(physical_address);
	uint32 va = ptr_frame_info->va;
	if (va >= KERNEL_HEAP_START && va < KERNEL_HEAP_MAX)
	{
		return va + PGOFF(physical_address);
	}
	return 0;
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	uint32 *ptr_page_table = NULL;
	struct FrameInfo *frame_info = get_frame_info(ptr_page_directory, virtual_address, &ptr_page_table);
	if (frame_info == NULL)
	{
		return 0;
	}
	return to_physical_address(frame_info) + PGOFF(virtual_address);
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
