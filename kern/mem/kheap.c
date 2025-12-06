//==================================================================================//
//==================================================================================//
//========================== EFFICIENT KERNEL HEAP =================================//
//================= TIME COMPLEXITY IN WORST CASE ~ O(log(N)) ======================//
//=================== TIME COMPLEXITY IN AVERAGE CASE ~ O(1) =======================//
//==================================================================================//
//==================================================================================//

// THE INEFFICIENT VERSION IS IN kheap_not_eff.c AND kheap_not_eff.h

#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"
#include <inc/bst.h>
#include <inc/hash_table.h>

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
#define PTEs_KERNEL (PERM_PRESENT | PERM_USED | PERM_WRITEABLE) // page table entries of the kernel

int allocate_page_to_frame(uint32 va, uint32 perm);
void *custom_fit(uint32 required_pages);
//==================================================================================//
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

	bst_init(&free_size_tree);
	bst_init(&free_address_tree);
	hash_init(&free_ht);
	hash_init(&reverse_free_ht);
	hash_init(&allocated_ht);

	init_kspinlock(&kheap_block_lock, "kheap_block_lock");
	init_kspinlock(&kheap_page_lock, "kheap_page_lock");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

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

	return 0;
}

void *custom_fit(uint32 size)
{
	if (size == 0)
		return NULL;
	//* Exact-fit
	if (hash_key_has_values(&free_ht, size))
	{
		uint32 va = (uint32)hash_get_first_value(&free_ht, size);

		for (int i = 0; i < size / PAGE_SIZE; i++)
		{
			uint32 V_A = va + i * PAGE_SIZE;
			int status = allocate_page_to_frame(V_A, PTEs_KERNEL);
			if (status == -1)
			{
				for (uint32 j = 0; j < i; j++)
				{
					uint32 rva = va + j * PAGE_SIZE;
					unmap_frame(ptr_page_directory, rva);
				}
				return NULL;
			}
		}
		uint32 vva = hash_get_first_value(&free_ht, size);
		hash_delete_value(&free_ht, size, vva);
		hash_delete_key(&reverse_free_ht, vva);
		bst_remove(&free_address_tree, vva);
		if (!hash_key_has_values(&free_ht, size))
		{
			bst_remove(&free_size_tree, size);
		}
		hash_insert(&allocated_ht, va, size);
		return va;
	}
	//* Worst-fit
	uint32 max_size = bst_find_max(&free_size_tree);
	if (max_size > size)
	{
		uint32 address_key = (uint32)hash_get_first_value(&free_ht, max_size);
		for (int i = 0; i < size / PAGE_SIZE; i++)
		{
			uint32 V_A = address_key + i * PAGE_SIZE;
			int status = allocate_page_to_frame(V_A, PTEs_KERNEL);
			if (status == -1)
			{
				for (uint32 j = 0; j < i; j++)
				{
					uint32 rva = address_key + j * PAGE_SIZE;
					unmap_frame(ptr_page_directory, rva);
				}
				return NULL;
			}
		}
		hash_insert(&allocated_ht, address_key, size);
		uint32 vva = hash_get_first_value(&free_ht, max_size);
		hash_delete_value(&free_ht, max_size, vva);
		bst_remove(&free_address_tree, vva);
		hash_delete_key(&reverse_free_ht, vva);
		hash_insert(&free_ht, (max_size - size), (address_key + size));
		bst_append(&free_address_tree, (address_key + size));
		hash_insert(&reverse_free_ht, (address_key + size), (max_size - size));
		if (!hash_key_has_values(&free_ht, max_size))
		{
			bst_remove(&free_size_tree, max_size);
			bst_append(&free_size_tree, max_size - size);
		}
		return address_key;
	}
	//* Break-fit
	if ((kheapPageAllocBreak + size) <= KERNEL_HEAP_MAX)
	{

		uint32 new_break = kheapPageAllocBreak + size;

		if (new_break < kheapPageAllocBreak || new_break > KERNEL_HEAP_MAX)
		{
			return NULL;
		}

		for (uint32 i = 0; i < size / PAGE_SIZE; i++)
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
		}
		hash_insert(&allocated_ht, kheapPageAllocBreak, size);
		uint32 re = kheapPageAllocBreak;
		kheapPageAllocBreak = new_break;
		return re;
	}

	return NULL;
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
		bool block_lock_is_in_hold = holding_kspinlock(&kheap_block_lock);

		if (!block_lock_is_in_hold)
			acquire_kspinlock(&kheap_block_lock);

		void *va = alloc_block(size);

		if (!block_lock_is_in_hold)
			release_kspinlock(&kheap_block_lock);

		if (va != NULL)
		{
			return va;
		}
		else
		{
			return NULL;
		}
	}

	bool is_holding_page_lock = holding_kspinlock(&kheap_page_lock);

	if (!is_holding_page_lock)
	{
		acquire_kspinlock(&kheap_page_lock);
	}
	uint32 size_ = ROUNDUP(size, PAGE_SIZE);
	void *result = custom_fit(size_);
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

	if (va == 0 || va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX)
	{
		panic("kfree: invalid virtual address\nis va == 0? %s", (va == 0) ? "true" : "false");
		return;
	}

	if (va >= KERNEL_HEAP_START && va < KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE)
	{
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
		bool page_lock_held = holding_kspinlock(&kheap_page_lock);
		if (!page_lock_held)
			acquire_kspinlock(&kheap_page_lock);

		int valid = hash_key_has_values(&allocated_ht, va);
		if (valid == 0)
		{
			if (!page_lock_held)
				release_kspinlock(&kheap_page_lock);
			panic("kfree: address not found in allocated segments");
			return;
		}
		uint32 size = (uint32)hash_get_first_value(&allocated_ht, va);

		for (uint32 V_A = va; V_A < va + size; V_A += PAGE_SIZE)
		{
			uint32 pa = kheap_physical_address(V_A);
			struct FrameInfo *frame_info = to_frame_info(pa);

			if (!frame_info)
			{
				panic("kfree(): trying to free an unallocated frame '%x' (frame #%d)",
					  frame_info,
					  to_frame_number(frame_info));
			}

			unmap_frame(ptr_page_directory, V_A);
		}
		if (va + size == kheapPageAllocBreak)
		{
			kheapPageAllocBreak -= size;

			while (1)
			{
				uint32 prev_va = (uint32)bst_find_max_lt_value(&free_address_tree, kheapPageAllocBreak);
				if (prev_va == 0)
					break;

				uint32 prev_size = (uint32)hash_get_first_value(&reverse_free_ht, prev_va);
				if (prev_va + prev_size == kheapPageAllocBreak)
				{
					kheapPageAllocBreak -= prev_size;

					bst_remove(&free_address_tree, prev_va);
					hash_delete_key(&reverse_free_ht, prev_va);
					hash_delete_value(&free_ht, prev_size, (void *)prev_va);

					if (!hash_key_has_values(&free_ht, prev_size))
					{
						bst_remove(&free_size_tree, prev_size);
					}
				}
				else
				{
					break;
				}
			}
			return;
		}
		else
		{
			hash_insert(&free_ht, size, va);
			hash_insert(&reverse_free_ht, va, size);
			bst_append(&free_size_tree, size);
			bst_append(&free_address_tree, va);
			hash_delete_key(&allocated_ht, va);
			uint32 before_add = bst_find_max_lt_value(&free_address_tree, va);
			uint32 after_add = va + size;
			int bol = 0;
			if (before_add + (uint32)hash_get_first_value(&reverse_free_ht, before_add) == va)
			{
				bol = 1;
			}
			if (bol && hash_key_has_values(&reverse_free_ht, after_add))
			{
				uint32 size_before = (uint32)hash_get_first_value(&reverse_free_ht, before_add);
				uint32 size_after = (uint32)hash_get_first_value(&reverse_free_ht, after_add);
				hash_delete_key(&reverse_free_ht, before_add);
				hash_delete_key(&reverse_free_ht, after_add);
				hash_delete_key(&reverse_free_ht, va);
				hash_insert(&reverse_free_ht, before_add, size_before + size + size_after);

				bst_remove(&free_address_tree, after_add);
				bst_remove(&free_address_tree, va);

				hash_delete_value(&free_ht, size_before, before_add);
				if (!hash_key_has_values(&free_ht, size_before))
				{
					bst_remove(&free_size_tree, size_before);
				}
				hash_delete_value(&free_ht, size_after, after_add);
				if (!hash_key_has_values(&free_ht, size_after))
				{
					bst_remove(&free_size_tree, size_after);
				}
				hash_delete_value(&free_ht, size, va);
				if (!hash_key_has_values(&free_ht, size))
				{
					bst_remove(&free_size_tree, size);
				}
				hash_insert(&free_ht, size_before + size + size_after, before_add);
				bst_append(&free_size_tree, size_before + size + size_after);
			}
			else if (bol)
			{
				uint32 size_before = (uint32)hash_get_first_value(&reverse_free_ht, before_add);
				hash_delete_key(&reverse_free_ht, before_add);
				hash_delete_key(&reverse_free_ht, va);
				hash_insert(&reverse_free_ht, before_add, size_before + size);

				bst_remove(&free_address_tree, va);

				hash_delete_value(&free_ht, size_before, before_add);
				if (!hash_key_has_values(&free_ht, size_before))
				{
					bst_remove(&free_size_tree, size_before);
				}
				hash_delete_value(&free_ht, size, va);
				if (!hash_key_has_values(&free_ht, size))
				{
					bst_remove(&free_size_tree, size);
				}
				hash_insert(&free_ht, size_before + size, before_add);
				bst_append(&free_size_tree, size_before + size);
			}
			else if (hash_key_has_values(&reverse_free_ht, after_add))
			{
				uint32 size_after = (uint32)hash_get_first_value(&reverse_free_ht, after_add);
				hash_delete_key(&reverse_free_ht, after_add);
				hash_delete_key(&reverse_free_ht, va);
				hash_insert(&reverse_free_ht, va, size + size_after);

				bst_remove(&free_address_tree, after_add);

				hash_delete_value(&free_ht, size_after, after_add);
				if (!hash_key_has_values(&free_ht, size_after))
				{
					bst_remove(&free_size_tree, size_after);
				}
				hash_delete_value(&free_ht, size, va);
				if (!hash_key_has_values(&free_ht, size))
				{
					bst_remove(&free_size_tree, size);
				}
				hash_insert(&free_ht, size + size_after, va);
				bst_append(&free_size_tree, size + size_after);
			}
		}

		if (!page_lock_held)
			release_kspinlock(&kheap_page_lock);
	}
	else
	{
		panic("kfree: address not in heap space");
	}
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
	panic("krealloc() is implemented in the second version of kheap 'kheap_not_eff.c'...!!");
	// we can't implement this function agin because we don't have time
}
