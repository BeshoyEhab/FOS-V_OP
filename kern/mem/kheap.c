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

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
// Remember to initialize locks (if any)
struct kspinlock allocate_lock;
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
	init_kspinlock(&allocate_lock, "Block Allocator Lock");
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

//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void *kmalloc(unsigned int size)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	// Your code is here
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		// we need to make lock while allocation
		bool lock_is_in_hold = holding_kspinlock(&allocate_lock); // that return 0 if if free
		if (!lock_is_in_hold)
			acquire_kspinlock(&allocate_lock);

		void *va = alloc_block(size);
		release_kspinlock(&allocate_lock);

		if (va != NULL)
			return va;
		else
			return NULL;
	} else {
		bool lock_is_in_hold = holding_kspinlock(&MemFrameLists.mfllock); //

		// If the lock is not held, acquire it
		if (!lock_is_in_hold)
		acquire_kspinlock(&MemFrameLists.mfllock);
		
		uint32 number_of_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
		uint32 start_va = ROUNDDOWN(dynAllocEnd + PAGE_SIZE, PAGE_SIZE);
		uint32 end_va = start_va + number_of_pages * PAGE_SIZE;
		uint32 *ptr;
		uint32 *ptr_page_table = get_page_table(ptr_page_directory, start_va, &ptr);

		if(ptr_page_table == TABLE_NOT_EXIST)
		{
			create_page_table(ptr_page_directory, start_va);
			ptr_page_table = get_page_table(ptr_page_directory, start_va, &ptr);
		}

		struct frameInfo *ptr_frame_info = get_frame_info(ptr_page_directory, start_va, ptr_page_table);
		if (end_va > KERNEL_HEAP_MAX)
		{
			release_kspinlock(&MemFrameLists.mfllock);
			return NULL;
		}
		
		if(end_va > kheapPageAllocBreak)
		{
			kheapPageAllocBreak = end_va;
		}

		for (uint32 va = start_va; va < end_va; va += PAGE_SIZE)
		{
			if (allocate_frame(&ptr_frame_info) != 0)
			{
				for (uint32 free_va = start_va; free_va < va; free_va += PAGE_SIZE)
				{
					unmap_frame(ptr_page_directory, free_va);
				}
				release_kspinlock(&MemFrameLists.mfllock);
				return NULL;
			}
			map_frame(ptr_page_directory, ptr_frame_info, va, PERM_WRITEABLE);
		}
		release_kspinlock(&MemFrameLists.mfllock);
		return (void *)start_va;
		// the code to allocate pages
	}

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
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	bool lock_is_in_hold = holding_kspinlock(&allocate_lock); // that return 0 if if free
	if (!lock_is_in_hold)
	acquire_kspinlock(&allocate_lock);
	
	uint32 va = (uint32)virtual_address;
	if(va == NULL || va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX){
		panic("kfree: virtual_address is invalid");
		release_kspinlock(&allocate_lock);
		return;
	}

	uint32 block_size = get_block_size(va);
	uint32 *ptr;
	uint32 *ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);

	if(ptr_page_table == TABLE_NOT_EXIST) {
		create_page_table(ptr_page_directory, va);
		ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	}
		
	struct FrameInfo *ptr_frame_info = get_frame_info(ptr_page_directory, va, ptr_page_table);
	if(block_size <= DYN_ALLOC_MAX_BLOCK_SIZE)
		free_block(va);
	else
	{
		uint32 number_of_pages = ROUNDUP(block_size, PAGE_SIZE) / PAGE_SIZE;
		for (uint32 i = 0; i < number_of_pages; i++)
		{
			free_frame(ptr_frame_info);
			//unmap_frame(ptr_page_directory, ROUNDDOWN(va + i * PAGE_SIZE, PAGE_SIZE));
		}
		decrement_references(ptr_frame_info);
	}
	release_kspinlock(&allocate_lock);
	//panic("kfree() is not implemented yet...!!");
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	struct FrameInfo *ptr_frame_info = to_frame_info(physical_address);
	uint32 va;
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
	uint32 va = (uint32)virtual_address;
	uint32 *ptr;
	uint32 *ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	if(ptr_page_table == TABLE_NOT_EXIST)
	{
		create_page_table(ptr_page_directory, va);
		ptr_page_table = get_page_table(ptr_page_directory, va, &ptr);
	}
	struct FrameInfo *ptr_frame_info = get_frame_info(ptr_page_directory, va, ptr_page_table);
	int physical_address = allocate_frame( ptr_frame_info);
	if (physical_address == -1)
		return 0;
	else
		return physical_address;
	//panic("kheap_physical_address() is not implemented yet...!!");

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
