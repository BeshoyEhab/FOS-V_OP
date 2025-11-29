/*
 * chunk_operations.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include <kern/trap/fault_handler.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/proc/user_environment.h>
#include "kheap.h"
#include "memory_manager.h"
#include <inc/queue.h>

// extern void inctst();

/******************************/
/*[1] RAM CHUNKS MANIPULATION */
/******************************/

//===============================
// 1) CUT-PASTE PAGES IN RAM:
//===============================
// This function should cut-paste the given number of pages from source_va to dest_va on the given page_directory
//	If the page table at any destination page in the range is not exist, it should create it
//	If ANY of the destination pages exists, deny the entire process and return -1. Otherwise, cut-paste the number of pages and return 0
//	ALL 12 permission bits of the destination should be TYPICAL to those of the source
//	The given addresses may be not aligned on 4 KB
int cut_paste_pages(uint32 *page_directory, uint32 source_va, uint32 dest_va, uint32 num_of_pages)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("cut_paste_pages() is not implemented yet...!!");
}

//===============================
// 2) COPY-PASTE RANGE IN RAM:
//===============================
// This function should copy-paste the given size from source_va to dest_va on the given page_directory
//	Ranges DO NOT overlapped.
//	If ANY of the destination pages exists with READ ONLY permission, deny the entire process and return -1.
//	If the page table at any destination page in the range is not exist, it should create it
//	If ANY of the destination pages doesn't exist, create it with the following permissions then copy.
//	Otherwise, just copy!
//		1. WRITABLE permission
//		2. USER/SUPERVISOR permission must be SAME as the one of the source
//	The given range(s) may be not aligned on 4 KB
int copy_paste_chunk(uint32 *page_directory, uint32 source_va, uint32 dest_va, uint32 size)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("copy_paste_chunk() is not implemented yet...!!");
}

//===============================
// 3) SHARE RANGE IN RAM:
//===============================
// This function should share the given size from source_va to dest_va on the given page_directory
//	Ranges DO NOT overlapped.
//	It should set the permissions of the second range by the given perms
//	If ANY of the destination pages exists, deny the entire process and return -1.
//	Otherwise, share the required range and return 0
//	During the share process:
//		1. If the page table at any destination page in the range is not exist, it should create it
//	The given range(s) may be not aligned on 4 KB
int share_chunk(uint32 *page_directory, uint32 source_va, uint32 dest_va, uint32 size, uint32 perms)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("share_chunk() is not implemented yet...!!");
}

//===============================
// 4) ALLOCATE CHUNK IN RAM:
//===============================
// This function should allocate the given virtual range [<va>, <va> + <size>) in the given address space  <page_directory> with the given permissions <perms>.
//	If ANY of the destination pages exists, deny the entire process and return -1. Otherwise, allocate the required range and return 0
//	If the page table at any destination page in the range is not exist, it should create it
//	Allocation should be aligned on page boundary. However, the given range may be not aligned.
int allocate_chunk(uint32 *page_directory, uint32 va, uint32 size, uint32 perms)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("allocate_chunk() is not implemented yet...!!");
}

//=====================================
// 5) CALCULATE FREE SPACE:
//=====================================
// It should count the number of free pages in the given range [va1, va2)
//(i.e. number of pages that are not mapped).
// Addresses may not be aligned on page boundaries
uint32 calculate_free_space(uint32 *page_directory, uint32 sva, uint32 eva)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("calculate_free_space() is not implemented yet...!!");
}

//=====================================
// 6) CALCULATE ALLOCATED SPACE:
//=====================================
void calculate_allocated_space(uint32 *page_directory, uint32 sva, uint32 eva, uint32 *num_tables, uint32 *num_pages)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("calculate_allocated_space() is not implemented yet...!!");
}

//=====================================
// 7) CALCULATE REQUIRED FRAMES IN RAM:
//=====================================
// This function should calculate the required number of pages for allocating and mapping the given range [start va, start va + size) (either for the pages themselves or for the page tables required for mapping)
//	Pages and/or page tables that are already exist in the range SHOULD NOT be counted.
//	The given range(s) may be not aligned on 4 KB
uint32 calculate_required_frames(uint32 *page_directory, uint32 sva, uint32 size)
{
	// TODO: PRACTICE: fill this function.
	// Comment the following line
	panic("calculate_required_frames() is not implemented yet...!!");
}

//=================================================================================//
//===========================END RAM CHUNKS MANIPULATION ==========================//
//=================================================================================//

/*******************************/
/*[2] USER CHUNKS MANIPULATION */
/*******************************/

//======================================================
/// functions used for USER HEAP (malloc, free, ...)
//======================================================

//=====================================
/* DYNAMIC ALLOCATOR SYSTEM CALLS */
//=====================================
/*2024*/
void *sys_sbrk(int numOfPages)
{
	panic("not implemented function");
}

//=====================================
// 1) ALLOCATE USER MEMORY:
//=====================================
void allocate_user_mem(struct Env *e, uint32 virtual_address, uint32 size)
{
	// cprintf("\n\n\n\nallocate_user_mem called with size %d\n", size);
	// TODO: [PROJECT'25.IM#2] USER HEAP - #2 allocate_user_mem
	// Your code is here
	virtual_address = ROUNDDOWN(virtual_address, PAGE_SIZE);
	uint32 required_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	uint32 *page_table = NULL;

	for (uint32 va = virtual_address; required_pages > 0; required_pages--, va += PAGE_SIZE)
	{
		int ret = get_page_table(e->env_page_directory, va, &page_table);

		if (ret == TABLE_NOT_EXIST)
		{
			create_page_table(e->env_page_directory, va);
		}

		pt_set_page_permissions(e->env_page_directory, va, PERM_UHPAGE, 0);
	}

	// Comment the following line
	//  panic("allocate_user_mem() is not implemented yet...!!");
}

//=====================================
// 2) FREE USER MEMORY:
//=====================================

//!!!!!!!!!!!!!!!!!!!!!
void free_user_mem(struct Env *e, uint32 virtual_address, uint32 size)
{
	// cprintf("\n\n\n\nfree_user_mem called with size %d\n", size);
	// TODO: [PROJECT'25.IM#2] USER HEAP - #4 free_user_mem
	// Your code is here
	// Calculate number of pages
	int page_count = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	uint32 *page_table = NULL;
	// Iterate through all pages in the range
	for (uint32 va = virtual_address; page_count > 0; page_count--, va += PAGE_SIZE)
	{
		// 1. Check if page table exists
		int ret_page_table = get_page_table(e->env_page_directory, va, &page_table);
		if (ret_page_table == TABLE_NOT_EXIST)
		{
			continue;
		}
		// 2. Unmark the page (logic: allow future allocation)
		pt_set_page_permissions(e->env_page_directory, va, 0, PERM_UHPAGE);
		// 3. Remove from Page File
		pf_remove_env_page(e, va);
		// 4. Check if page is currently in RAM
		struct FrameInfo *frame = get_frame_info(e->env_page_directory, va, &page_table);
		if (!frame)
		{
			continue; // Not in RAM, done with this page
		}
		// 5. Remove from Working Set (RAM)
		struct WorkingSetElement *ws_element = NULL;
		LIST_FOREACH(ws_element, &(e->page_WS_list))
		{
			if (ROUNDDOWN(ws_element->virtual_address, PAGE_SIZE) == ROUNDDOWN(va, PAGE_SIZE))
			{
				// --- CRITICAL FIX: Update Clock Hand ---
				if (e->page_last_WS_element == ws_element)
				{
					e->page_last_WS_element = LIST_NEXT(ws_element);
					// Wrap around if we hit the end
					if (e->page_last_WS_element == NULL)
					{
						e->page_last_WS_element = LIST_FIRST(&(e->page_WS_list));
					}
					// EDGE CASE: If list had only 1 element, prev logic sets it back to self.
					// We must explicitly set it to NULL.
					if (e->page_last_WS_element == ws_element)
					{
						e->page_last_WS_element = NULL;
					}
				}
				// ---------------------------------------
				LIST_REMOVE(&(e->page_WS_list), ws_element);
				kfree(ws_element);
				break; // Found and removed, exit loop
			}
		}
		// 6. Unmap the frame (Hardware)
		// [FIX]: This must be OUTSIDE the LIST_FOREACH loop
		unmap_frame(e->env_page_directory, va);
	}
	// Comment the following line
	// panic("free_user_mem() is not implemented yet...!!");
}
//!!!!!!!!!!!!!!!!!!!!!

//*
// void free_user_mem(struct Env *e, uint32 virtual_address, uint32 size)
// {
// 	// Calculate number of pages to free
// 	int page_count = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
// 	uint32 *cur_page_table = NULL;
// 	// Iterate page by page
// 	// for (uint32 cur_va = virtual_address; page_count > 0; page_count--, cur_va += PAGE_SIZE)
// 	// {
// 	// 	// 1. Get Page Table (Check if exists)
// 	// 	int ret = get_page_table(e->env_page_directory, cur_va, &cur_page_table);
// 	// 	if (ret == TABLE_NOT_EXIST)
// 	// 	{
// 	// 		continue;
// 	// 	}
// 	// 	// 2. Unmark pages (Use the correct flag PERM_UHPAGE)
// 	// 	// We set '0' to clear permissions, passing PERM_UHPAGE to indicate what to clear
// 	// 	pt_set_page_permissions(e->env_page_directory, cur_va, 0, PERM_UHPAGE);
// 	// 	// 3. Free pages from page file
// 	// 	pf_remove_env_page(e, cur_va);
// 	// 	// 4. Check if page is in RAM (Working Set)
// 	// 	// We get the frame to see if it is physically present
// 	// 	struct FrameInfo *frame = get_frame_info(e->env_page_directory, cur_va, &cur_page_table);
// 	// 	if (frame) // If frame != NULL, the page is in RAM
// 	// 	{
// 	// 		// Assuming you have implemented the O(1) Bonus by adding 'wse' to FrameInfo:
// 	// 		// If not, you must search the list manually (O(N)).
// 	// 		// Let's assume O(N) loop here for safety if O(1) isn't ready,
// 	// 		// OR use frame->wse if you did the bonus.
// 	// 		struct WorkingSetElement *wse = NULL;
// 	// 		// --- SEARCHING FOR ELEMENT (Safe O(N) fallback) ---
// 	// 		LIST_FOREACH(wse, &(e->page_WS_list))
// 	// 		{
// 	// 			if (ROUNDDOWN(wse->virtual_address, PAGE_SIZE) == ROUNDDOWN(cur_va, PAGE_SIZE))
// 	// 			{
// 	// 				// === CRITICAL FIX: Update Clock Hand ===
// 	// 				if (e->page_last_WS_element == wse)
// 	// 				{
// 	// 					e->page_last_WS_element = LIST_NEXT(wse);
// 	// 					// Wrap around
// 	// 					if (e->page_last_WS_element == NULL)
// 	// 					{
// 	// 						e->page_last_WS_element = LIST_FIRST(&(e->page_WS_list));
// 	// 					}
// 	// 					// If list became empty
// 	// 					if (e->page_last_WS_element == wse)
// 	// 					{
// 	// 						e->page_last_WS_element = NULL;
// 	// 					}
// 	// 				}
// 	// 				// =======================================
// 	// 				LIST_REMOVE(&(e->page_WS_list), wse);
// 	// 				cprintf("LIST_REMOVE called in free_user_mem to free wse %p\n", wse);
// 	// 				kfree(wse);
// 	// 				break; // Found and removed
// 	// 			}
// 	// 		}
// 	// 	}
// 	// 	// 5. Unmap the frame (Hardware)
// 	// 	// Must be done AFTER dealing with the Working Set
// 	// 	unmap_frame(e->env_page_directory, cur_va);
// 	// }
// 	// ---------- REPLACE the core loop inside free_user_mem(...) with this ----------
// 	for (uint32 va = virtual_address; page_count > 0; page_count--, va += PAGE_SIZE)
// 	{
// 		uint32 *page_table = NULL;
// 		// Try to get page table (may not exist)
// 		int ret_page_table = get_page_table(e->env_page_directory, va, &page_table);
// 		// Don't skip: even if TABLE_NOT_EXIST, the page might be in RAM / WS
// 		// So we handle permissions clearing if table exists, but still check frame/WS.
// 		if (ret_page_table != TABLE_NOT_EXIST)
// 		{
// 			// Unmark the page (allow future allocation): clear UHPAGE flag
// 			pt_set_page_permissions(e->env_page_directory, va, 0, PERM_UHPAGE);
// 			// Remove from Page File (Disk)
// 			pf_remove_env_page(e, va);
// 		}
// 		// See if page is in RAM (frame)
// 		struct FrameInfo *frame = get_frame_info(e->env_page_directory, va, &page_table);
// 		if (!frame)
// 		{
// 			// nothing in RAM; continue
// 			continue;
// 		}
// 		// Remove from Working Set (RAM) safely:
// 		struct WorkingSetElement *ws_element = LIST_FIRST(&(e->page_WS_list));
// 		struct WorkingSetElement *next_ws = NULL;
// 		while (ws_element != NULL)
// 		{
// 			next_ws = LIST_NEXT(ws_element);
// 			if (ROUNDDOWN(ws_element->virtual_address, PAGE_SIZE) == ROUNDDOWN(va, PAGE_SIZE))
// 			{
// 				// Update page_last_WS_element safely
// 				if (e->page_last_WS_element == ws_element)
// 				{
// 					// pick successor; if none, pick first; if list will be empty, set NULL after removal
// 					struct WorkingSetElement *candidate = LIST_NEXT(ws_element);
// 					if (candidate == NULL)
// 						candidate = LIST_FIRST(&(e->page_WS_list));
// 					if (candidate == ws_element) // only one element case
// 						e->page_last_WS_element = NULL;
// 					else
// 						e->page_last_WS_element = candidate;
// 				}
// 				LIST_REMOVE(&(e->page_WS_list), ws_element);
// 				kfree(ws_element);
// 				break; // found and removed for this VA
// 			}
// 			ws_element = next_ws;
// 		}
// 		// Finally unmap frame (Hardware)
// 		unmap_frame(e->env_page_directory, va);
// 	}
// }

//*

// void free_user_mem(struct Env *e, uint32 virtual_address, uint32 size)
// {
// 	int pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
// 	uint32 va = virtual_address;
// 	uint32 *pt = NULL;

// 	while (pages--)
// 	{

// 		int exists = get_page_table(e->env_page_directory, va, &pt);
// 		if (exists != TABLE_NOT_EXIST)
// 		{
// 			pt_set_page_permissions(e->env_page_directory, va, 0, PERM_UHPAGE);
// 			pf_remove_env_page(e, va);

// 			struct FrameInfo *frame = get_frame_info(e->env_page_directory, va, &pt);

// 			// SAFE WS ITERATION
// 			struct WorkingSetElement *ws = LIST_FIRST(&e->page_WS_list);
// 			while (ws)
// 			{
// 				struct WorkingSetElement *next = LIST_NEXT(ws);
// 				if (ROUNDDOWN(ws->virtual_address, PAGE_SIZE) == ROUNDDOWN(va, PAGE_SIZE))
// 				{
// 					if (e->page_last_WS_element == ws)
// 					{
// 						e->page_last_WS_element = next;
// 						if (!e->page_last_WS_element)
// 							e->page_last_WS_element = LIST_FIRST(&e->page_WS_list);
// 					}
// 					LIST_REMOVE(&e->page_WS_list, ws);
// 					kfree(ws);
// 					break;
// 				}
// 				ws = next;
// 			}

// 			if (frame)
// 				unmap_frame(e->env_page_directory, va);
// 		}

// 		va += PAGE_SIZE;
// 	}
// }

//*

//=====================================
// 4) FREE USER MEMORY (BUFFERING):
//=====================================
void __free_user_mem_with_buffering(struct Env *e, uint32 virtual_address, uint32 size)
{
	// your code is here, remove the panic and write your code
	panic("__free_user_mem_with_buffering() is not implemented yet...!!");
}

//=====================================
// 3) MOVE USER MEMORY:
//=====================================
void move_user_mem(struct Env *e, uint32 src_virtual_address, uint32 dst_virtual_address, uint32 size)
{
	panic("move_user_mem() is not implemented yet...!!");
}

//=================================================================================//
//========================== END USER CHUNKS MANIPULATION =========================//
//=================================================================================//
