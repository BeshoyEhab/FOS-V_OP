/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

// 2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
//  0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
// 2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE;
}
void setPageReplacmentAlgorithmCLOCK() { _PageRepAlgoType = PG_REP_CLOCK; }
void setPageReplacmentAlgorithmFIFO() { _PageRepAlgoType = PG_REP_FIFO; }
void setPageReplacmentAlgorithmModifiedCLOCK() { _PageRepAlgoType = PG_REP_MODIFIEDCLOCK; }
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal() { _PageRepAlgoType = PG_REP_DYNAMIC_LOCAL; }
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps)
{
	_PageRepAlgoType = PG_REP_NchanceCLOCK;
	page_WS_max_sweeps = PageWSMaxSweeps;
}
/*2024*/ void setFASTNchanceCLOCK(bool fast) { FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL() { _PageRepAlgoType = PG_REP_OPTIMAL; };

// 2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE) { return _PageRepAlgoType == LRU_TYPE ? 1 : 0; }
uint32 isPageReplacmentAlgorithmCLOCK()
{
	if (_PageRepAlgoType == PG_REP_CLOCK)
		return 1;
	return 0;
}
uint32 isPageReplacmentAlgorithmFIFO()
{
	if (_PageRepAlgoType == PG_REP_FIFO)
		return 1;
	return 0;
}
uint32 isPageReplacmentAlgorithmModifiedCLOCK()
{
	if (_PageRepAlgoType == PG_REP_MODIFIEDCLOCK)
		return 1;
	return 0;
}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal()
{
	if (_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL)
		return 1;
	return 0;
}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK()
{
	if (_PageRepAlgoType == PG_REP_NchanceCLOCK)
		return 1;
	return 0;
}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL()
{
	if (_PageRepAlgoType == PG_REP_OPTIMAL)
		return 1;
	return 0;
}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt) { _EnableModifiedBuffer = enableIt; }
uint8 isModifiedBufferEnabled() { return _EnableModifiedBuffer; }

void enableBuffering(uint32 enableIt) { _EnableBuffering = enableIt; }
uint8 isBufferingEnabled() { return _EnableBuffering; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length; }
uint32 getModifiedBufferLength() { return _ModifiedBufferLength; }

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	// setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	// setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	// setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0);
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
uint8 num_repeated_fault = 0;
extern uint32 sys_calculate_free_frames();

struct Env *last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	// cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	// If same fault va for 3 times, then panic
	// UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env *cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va;
	last_faulted_env = cur_env;
	/******************************************************/
	// 2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3)
	{
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu *c = mycpu();
		// cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	// 2017: Check stack underflow for User
	else
	{
		// cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	// get a pointer to the environment that caused the fault at runtime
	// cprintf("curenv = %x\n", curenv);
	struct Env *faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	// check the faulted address, is it a table or not ?
	// If the directory entry of the faulted address is NOT PRESENT then
	if ((faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter++;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			if (fault_va >= USER_TOP)
			{
				env_exit();
			}

			int perm = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);

			if (!(perm & PERM_UHPAGE) && fault_va >= USER_HEAP_START && fault_va <= USER_HEAP_MAX)

			{
				env_exit();
			}

			if ((perm & PERM_PRESENT) && !(perm & PERM_WRITEABLE) && (tf->tf_err & FEC_WR))
			{
				env_exit();
			}
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va);
		/*============================================================================================*/

		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter++;

		//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
		//				cprintf("\nPage working set BEFORE fault handler...\n");
		//				env_page_ws_print(faulted_env);
		// int ffb = sys_calculate_free_frames();

		if (isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	// Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}

//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env *curenv, uint32 fault_va)
{
	// panic("table_fault_handler() is not implemented yet...!!");
	// Check if it's a stack page
	uint32 *ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	// TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	struct WS_List currentWorkingSet;
	LIST_INIT(&currentWorkingSet);

	struct WorkingSetElement *copyPTR;
	LIST_FOREACH(copyPTR, initWorkingSet)
	{
		struct WorkingSetElement *new_elem = (struct WorkingSetElement *)kmalloc(sizeof(struct WorkingSetElement));
		new_elem->virtual_address = copyPTR->virtual_address;
		LIST_INSERT_TAIL(&currentWorkingSet, new_elem);
	}

	uint32 counter = 0;
	struct PageRefElement *current;
	LIST_FOREACH(current, pageReferences)
	{
		int found = 0;
		struct WorkingSetElement *victim;
		LIST_FOREACH(victim, &currentWorkingSet)
		{
			if (current->virtual_address == victim->virtual_address)
			{
				found = 1;
				break;
			}
		}

		if (found)
			continue;

		counter++;

		if (LIST_SIZE(&currentWorkingSet) < maxWSSize)
		{
			struct WorkingSetElement *allocated_elem = (struct WorkingSetElement *)kmalloc(sizeof(struct WorkingSetElement));
			allocated_elem->virtual_address = current->virtual_address;
			LIST_INSERT_TAIL(&currentWorkingSet, allocated_elem);
			counter++;
			found = 1;
			continue;
		}

		struct WorkingSetElement *iterator;
		LIST_FOREACH(iterator, &currentWorkingSet)
		{
			iterator->calling_offset = 0;
			struct PageRefElement *ref_count = current->prev_next_info.le_next;

			while (ref_count != NULL)
			{
				iterator->calling_offset++;
				if (ref_count->virtual_address == iterator->virtual_address)
				{
					break;
				}
				ref_count = ref_count->prev_next_info.le_next;
			}
			if (ref_count == NULL)
				iterator->calling_offset = 0x7FFFFFFF;
		}

		struct WorkingSetElement *elem;
		struct WorkingSetElement *furthest = NULL;
		LIST_FOREACH(elem, &currentWorkingSet)
		{
			if (furthest == NULL || furthest->calling_offset < elem->calling_offset)
			{
				furthest = elem;
			}
		}

		struct WorkingSetElement *allocated_elem = (struct WorkingSetElement *)kmalloc(sizeof(struct WorkingSetElement));
		allocated_elem->virtual_address = current->virtual_address;
		allocated_elem->calling_offset = 0;

		LIST_INSERT_BEFORE(&currentWorkingSet, furthest, allocated_elem);
		LIST_REMOVE(&currentWorkingSet, furthest);
		kfree(furthest);

		if (current->prev_next_info.le_next == NULL)
		{
			break;
		}
	}
	return counter;
}

//=============================
// Helper Functions
//=============================
struct WorkingSetElement *clearUsed(struct Env *faulted_env)
{
	struct WorkingSetElement *i = faulted_env->page_last_WS_element;
	if (i == NULL)
	{
		i = LIST_FIRST(&(faulted_env->page_WS_list));
	}

	while (1)
	{
		uint32 element_va = i->virtual_address;
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, ROUNDDOWN(element_va, PAGE_SIZE));
		if (perms == -1)
		{
			env_exit();
		}

		if ((perms & PERM_USED) == 0)
		{
			return i;
		}

		pt_set_page_permissions(faulted_env->env_page_directory, element_va, 0, PERM_USED);
		i = LIST_NEXT(i);

		if (i == NULL)
		{
			i = LIST_FIRST(&faulted_env->page_WS_list);
		}
	}
}

//============================
// Page Fault Handler
//============================
void page_fault_handler(struct Env *faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	struct WorkingSetElement *victimWSElement = NULL;
	uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));

	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		// TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
		struct WorkingSetElement *elem = LIST_FIRST(&(faulted_env->page_WS_list));
		if (faulted_env->optimal_loaded != 1)
		{
			LIST_FOREACH(elem, &(faulted_env->page_WS_list))
			{
				struct WorkingSetElement *new_elem = kmalloc(sizeof(struct WorkingSetElement));
				new_elem->virtual_address = elem->virtual_address;
				new_elem->calling_offset = 0;
				LIST_INSERT_TAIL(&(faulted_env->ActiveOptimalList), new_elem);
			}
			faulted_env->optimal_loaded = 1;
		}

		struct FrameInfo *new_frame;
		uint32 fva = ROUNDDOWN(fault_va, PAGE_SIZE);

		struct FrameInfo *fi = get_frame_info(faulted_env->env_page_directory, fva, &new_frame);

		if (fi != NULL)
		{
			pt_set_page_permissions(faulted_env->env_page_directory, fva, PERM_PRESENT | PERM_WRITEABLE | PERM_USER, 0);
		}
		else
		{
			allocate_frame(&fi);
			map_frame(faulted_env->env_page_directory, fi, fva, PERM_PRESENT | PERM_USER | PERM_WRITEABLE);

			int read_page = pf_read_env_page(faulted_env, fva);
			if (read_page == E_PAGE_NOT_EXIST_IN_PF)
			{
				if (!((fva >= USER_HEAP_START && fva < USER_HEAP_MAX) || (fva >= USTACKBOTTOM && fva < USTACKTOP)))
				{
					env_exit();
				}
			}
		}

		if (LIST_SIZE(&(faulted_env->ActiveOptimalList)) == faulted_env->page_WS_max_size)
		{
			struct WorkingSetElement *free_elem = LIST_FIRST(&(faulted_env->ActiveOptimalList));
			LIST_FOREACH_SAFE(free_elem, &(faulted_env->ActiveOptimalList), WS_List)
			{
				pt_set_page_permissions(faulted_env->env_page_directory, free_elem->virtual_address, 0, PERM_PRESENT);
				LIST_REMOVE(&(faulted_env->ActiveOptimalList), free_elem);
				kfree((void *)free_elem);
			}
		}

		struct WorkingSetElement *ins_elem = LIST_FIRST(&faulted_env->ActiveOptimalList);
		uint32 foundInActiveList = 0;
		while (ins_elem != NULL)
		{
			if (ins_elem->virtual_address == fva)
			{
				foundInActiveList = 1;
				break;
			};
			ins_elem = ins_elem->prev_next_info.le_next;
		}

		struct WorkingSetElement *new_element;
		if (!foundInActiveList)
		{
			new_element = env_page_ws_list_create_element(faulted_env, fva);
			if (new_element == NULL)
			{
				cprintf("failed to create element at %x\n", fva);
			}
			LIST_INSERT_TAIL(&(faulted_env->ActiveOptimalList), new_element);
		}

		struct PageRefElement *refElem = (struct PageRefElement *)kmalloc(sizeof(struct PageRefElement));
		assert(fva >= 0 && fva <= USER_TOP);
		if (refElem == NULL)
		{
			panic("Couldn't allocate page reference element using kmalloc in optimal");
		}
		refElem->virtual_address = fva;
		LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), refElem);
	}
	else
	{

		if (wsSize < (faulted_env->page_WS_max_size))
		{
			// TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
			// Your code is here
			struct FrameInfo *ptr_Frame_Info;
			allocate_frame(&ptr_Frame_Info);
			map_frame(faulted_env->env_page_directory, ptr_Frame_Info, ROUNDDOWN(fault_va, PAGE_SIZE), PERM_WRITEABLE | PERM_USER | PERM_PRESENT);
			int read_page = pf_read_env_page(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));
			if (read_page == E_PAGE_NOT_EXIST_IN_PF)
			{
				if (!((ROUNDDOWN(fault_va, PAGE_SIZE) >= USER_HEAP_START && ROUNDDOWN(fault_va, PAGE_SIZE) < USER_HEAP_MAX) || (ROUNDDOWN(fault_va, PAGE_SIZE) >= USTACKBOTTOM && ROUNDDOWN(fault_va, PAGE_SIZE) < USTACKTOP)))
				{
					env_exit();
				}
			}

			struct WorkingSetElement *last_element = env_page_ws_list_create_element(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));

			// Insert BEFORE page_last_WS_element (clock hand) to protect new pages
			if (faulted_env->page_last_WS_element != NULL)
			{
				LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), faulted_env->page_last_WS_element, last_element);
			}
			else
			{
				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), last_element);
			}

			// Only set page_last_WS_element on the FIRST fill (from empty to full)
			if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size && faulted_env->page_last_WS_element == NULL)
			{
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
		}
		else
		{

			if (isPageReplacmentAlgorithmCLOCK())
			{
				// TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				struct WorkingSetElement *victim = clearUsed(faulted_env);

				// Get victim's frame info using get_frame_info (NOT get_page_table)
				uint32 *victim_ptr_page_table = NULL;
				struct FrameInfo *victim_frame_info = get_frame_info(faulted_env->env_page_directory, victim->virtual_address, &victim_ptr_page_table);

				if (victim_frame_info == NULL || victim_ptr_page_table == NULL)
				{
					env_exit();
				}

				// Check if victim is modified and write back to page file if needed
				int victim_perms = pt_get_page_permissions(faulted_env->env_page_directory, victim->virtual_address);
				if (victim_perms & PERM_MODIFIED)
				{
					pf_update_env_page(faulted_env, ROUNDDOWN(victim->virtual_address, PAGE_SIZE), victim_frame_info);
				}

				// Unmap victim frame
				unmap_frame(faulted_env->env_page_directory, victim->virtual_address);

				// Allocate new frame for the faulted page
				struct FrameInfo *new_element_frame;
				if (allocate_frame(&new_element_frame) != 0 || new_element_frame == NULL)
				{
					env_exit();
				}

				// Map the new frame at the fault address
				map_frame(faulted_env->env_page_directory, new_element_frame, ROUNDDOWN(fault_va, PAGE_SIZE), PERM_WRITEABLE | PERM_USER | PERM_PRESENT | PERM_USED);

				// Read the page content from disk
				int read_result = pf_read_env_page(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));
				if (read_result == E_PAGE_NOT_EXIST_IN_PF)
				{
					uint32 fault_va_rounded = ROUNDDOWN(fault_va, PAGE_SIZE);
					if (!((fault_va_rounded >= USER_HEAP_START && fault_va_rounded < USER_HEAP_MAX) ||
						  (fault_va_rounded >= USTACKBOTTOM && fault_va_rounded < USTACKTOP)))
					{
						env_exit();
					}
				}

				// Update the victim element's virtual address to point to the new page
				victim->virtual_address = ROUNDDOWN(fault_va, PAGE_SIZE);

				// Update page_last_WS_element to the next element after victim (circular)
				struct WorkingSetElement *next_elem = LIST_NEXT(victim);
				if (next_elem == NULL)
				{
					next_elem = LIST_FIRST(&(faulted_env->page_WS_list));
				}
				faulted_env->page_last_WS_element = next_elem;
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				// TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				// Your code is here
				// Comment the following line

				// LRU Aging Replacement (fault-time: only select victim, do placement/replacement)
				struct WorkingSetElement *elem = LIST_FIRST(&(faulted_env->page_WS_list));
				struct WorkingSetElement *victim = NULL;

				/* If there's still free space in WS, we don't evict — we do placement. */
				if (LIST_SIZE(&(faulted_env->page_WS_list)) < faulted_env->page_WS_max_size)
				{
					/* Placement: allocate & map frame for fault_va and insert new WS element */
					uint32 fault_vva = ROUNDDOWN(fault_va, PAGE_SIZE);

					struct FrameInfo *new_frame;
					if (allocate_frame(&new_frame) != 0)
					{
						// allocation failure handling (adapt to your kernel style)
						panic("LRU replacement: allocate_frame failed");
					}

					map_frame(faulted_env->env_page_directory, new_frame, fault_vva,
							  PERM_PRESENT | PERM_WRITEABLE | PERM_USED | PERM_USER);

					/* Read the page from the page file */
					int ret = pf_read_env_page(faulted_env, fault_vva);
					if (ret == E_PAGE_NOT_EXIST_IN_PF)
					{
						/* If page not in PF, check if it's a valid fresh allocation (heap/stack) */
						if (!((fault_vva >= USER_HEAP_START && fault_vva < USER_HEAP_MAX) ||
							  (fault_vva >= USTACKBOTTOM && fault_vva < USTACKTOP)))
						{
							/* Invalid access */
							unmap_frame(faulted_env->env_page_directory, fault_vva);
							env_exit();
						}
					}

					struct WorkingSetElement *new_elem = env_page_ws_list_create_element(faulted_env, fault_vva);
					LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_elem);

					/* Update page_last_WS_element when WS becomes full after this insertion */
					if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
					{
						/* If the list was not full before, set last to first (or tail) */
						faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
					}

					return; /* placement done */
				}

				/* Replacement scenario: WS is full — pick the page with smallest time_stamp */
				elem = LIST_FIRST(&(faulted_env->page_WS_list));
				uint32 min_time = 0xFFFFFFFF;

				while (elem != NULL)
				{
					if (elem->time_stamp < min_time)
					{
						min_time = elem->time_stamp;
						victim = elem;
					}
					elem = LIST_NEXT(elem);
				}

				if (victim == NULL)
				{
					/* Should not happen if WS is full — fallback: pick first element */
					victim = LIST_FIRST(&(faulted_env->page_WS_list));
					if (victim == NULL)
					{
						panic("LRU replacement: no victim found and WS is empty");
					}
				}

				struct WorkingSetElement *next_victim = LIST_NEXT(victim);
				uint32 victim_va = victim->virtual_address;

				/* Check if victim is modified and write back if necessary */
				uint32 *ptr_page_table;
				struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victim_va, &ptr_page_table);
				int perms = pt_get_page_permissions(faulted_env->env_page_directory, victim_va);

				if (perms & PERM_MODIFIED)
				{
					pf_update_env_page(faulted_env, victim_va, victim_frame);
				}

				/* Unmap victim's frame and remove from WS list */
				unmap_frame(faulted_env->env_page_directory, victim_va);
				LIST_REMOVE(&(faulted_env->page_WS_list), victim);
				kfree(victim);

				/* Now allocate & map a new frame for the faulting virtual address */
				uint32 fault_vva = ROUNDDOWN(fault_va, PAGE_SIZE);

				struct FrameInfo *new_frame;
				if (allocate_frame(&new_frame) != 0)
				{
					panic("LRU replacement: allocate_frame failed (after victim unmap)");
				}

				map_frame(faulted_env->env_page_directory, new_frame, fault_vva,
						  PERM_PRESENT | PERM_WRITEABLE | PERM_USED | PERM_USER);

				/* Read the page from the page file */
				int ret = pf_read_env_page(faulted_env, fault_vva);
				if (ret == E_PAGE_NOT_EXIST_IN_PF)
				{
					/* If page not in PF, check if it's a valid fresh allocation (heap/stack) */
					if (!((fault_vva >= USER_HEAP_START && fault_vva < USER_HEAP_MAX) ||
						  (fault_vva >= USTACKBOTTOM && fault_vva < USTACKTOP)))
					{
						/* Invalid access */
						unmap_frame(faulted_env->env_page_directory, fault_vva);
						env_exit();
					}
				}

				/* Insert the new working-set element at the tail */
				struct WorkingSetElement *new_elem = env_page_ws_list_create_element(faulted_env, fault_vva);
				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_elem);

				/* Update page_last_WS_element: follow same logic used elsewhere in your code */
				if (next_victim != NULL)
				{
					faulted_env->page_last_WS_element = next_victim;
				}
				else
				{
					faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
				}

				/* Done with replacement */
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				// TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				// Your code is here
				// Comment the following line

				// Modified Clock Replacement
				struct WorkingSetElement *elem = faulted_env->page_last_WS_element;

				if (!elem)
				{
					elem = LIST_FIRST(&(faulted_env->page_WS_list));
				}

				struct WorkingSetElement *victim = NULL;
				int ws_size = LIST_SIZE(&(faulted_env->page_WS_list));

				// The algorithm guarantees a victim will be found, so we loop until one is chosen.
				while (1)
				{
					// Trial 1: Search for (Used=0, Modified=0)
					// We need to save the starting element to know when we've done a full circle
					struct WorkingSetElement *start_elem = elem;

					for (int i = 0; i < ws_size; i++)
					{
						uint32 vva = ROUNDDOWN(elem->virtual_address, PAGE_SIZE);
						int perms = pt_get_page_permissions(faulted_env->env_page_directory, vva);

						// Skip if page is not present or page table doesn't exist
						if (perms == -1 || !(perms & PERM_PRESENT))
						{
							elem = LIST_NEXT(elem);
							if (!elem)
								elem = LIST_FIRST(&(faulted_env->page_WS_list));
							continue;
						}

						int used = (perms & PERM_USED) ? 1 : 0;
						int modified = (perms & PERM_MODIFIED) ? 1 : 0;

						if (!used && !modified)
						{
							victim = elem;
							break; // Found (0,0) - Best Victim
						}

						// Move to next element circularly
						elem = LIST_NEXT(elem);
						if (!elem)
							elem = LIST_FIRST(&(faulted_env->page_WS_list));
					}

					if (victim)
						break; // Found in Trial 1

					// Trial 2: Search for (Used=0, Modified=1) and reset Used bits
					// Start from where Trial 1 left off (which is the same as where it started, since it did a full loop)
					// Actually, the pointer 'elem' should be back at 'start_elem' after the loop if we did exactly ws_size iterations.
					// Let's ensure we start Trial 2 from the correct position as per algorithm description:
					// "starts a second circular scan from its starting position" - which is 'start_elem'

					elem = start_elem; // Reset to start of the search

					for (int i = 0; i < ws_size; i++)
					{
						uint32 vva = ROUNDDOWN(elem->virtual_address, PAGE_SIZE);
						int perms = pt_get_page_permissions(faulted_env->env_page_directory, vva);

						// Skip if page is not present or page table doesn't exist
						if (perms == -1 || !(perms & PERM_PRESENT))
						{
							elem = LIST_NEXT(elem);
							if (!elem)
								elem = LIST_FIRST(&(faulted_env->page_WS_list));
							continue;
						}

						int used = (perms & PERM_USED) ? 1 : 0;
						int modified = (perms & PERM_MODIFIED) ? 1 : 0;

						if (!used && modified)
						{
							victim = elem;
							break; // Found (0,1) - Second Best Victim
						}

						// Reset Used bit for next trials (give second chance)
						if (used)
						{
							pt_set_page_permissions(faulted_env->env_page_directory, vva, 0, PERM_USED);
						}

						// Move to next element circularly
						elem = LIST_NEXT(elem);
						if (!elem)
							elem = LIST_FIRST(&(faulted_env->page_WS_list));
					}

					if (victim)
						break; // Found in Trial 2

					// If we reach here, both trials failed.
					// The algorithm says: "If both trials complete one full pass without finding a victim,
					// the algorithm repeats the entire process, starting again with Trial 1."
					// Since Trial 2 reset Used bits, the next Trial 1 is guaranteed to find something eventually.
					// We just continue the while(1) loop.
					// 'elem' is already at the correct position (start_elem) because the loop ran ws_size times.
					elem = start_elem;
				}

				// Replacement Logic
				if (victim)
				{
					uint32 victim_vva = ROUNDDOWN(victim->virtual_address, PAGE_SIZE);

					// Print perms BEFORE doing anything that may change PTE
					int vperms = pt_get_page_permissions(faulted_env->env_page_directory, victim_vva);
					// cprintf("DEBUG: (before) victim_vva=%x perms=%x\n", victim_vva, vperms);

					// Get the frame info immediately (must be present)
					uint32 *ptr_page_table = NULL;
					struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victim_vva, &ptr_page_table);

					if (!victim_frame)
					{
						// cprintf("DEBUG: victim_frame is NULL for vva=%x; perms now=%x\n", victim_vva, pt_get_page_permissions(faulted_env->env_page_directory, victim_vva));
						panic("mod clock : cannot get the victim frame");
					}

					// If modified, write back to page file
					if (vperms & PERM_MODIFIED)
					{
						pf_update_env_page(faulted_env, victim_vva, victim_frame);
					}

					// Unmap the victim mapping (this may free the frame or decrease refcount)
					unmap_frame(faulted_env->env_page_directory, victim_vva);

					// Now load the needed page at fault_vva
					uint32 fault_vva = ROUNDDOWN(fault_va, PAGE_SIZE);

					// Allocate a new frame
					struct FrameInfo *new_frame = NULL;
					if (allocate_frame(&new_frame) != 0)
					{
						panic("Modified Clock: Failed to allocate frame!");
					}

					// Map the new frame
					if (map_frame(faulted_env->env_page_directory, new_frame, fault_vva, PERM_PRESENT | PERM_WRITEABLE | PERM_USER | PERM_USED) != 0)
					{
						panic("Modified Clock: Failed to map frame!");
					}

					// Read from page file
					int read_page = pf_read_env_page(faulted_env, fault_vva);
					if (read_page == E_PAGE_NOT_EXIST_IN_PF)
					{
						if (!((fault_vva >= USER_HEAP_START && fault_vva < USER_HEAP_MAX) ||
							  (fault_vva >= USTACKBOTTOM && fault_vva < USTACKTOP)))
						{
							// cprintf("ya lahwaaaaaaay\n");
							// Unmap and exit if invalid
							unmap_frame(faulted_env->env_page_directory, fault_vva);
							env_exit();
						}
						else
						{
							// page not exist but within heap/stack range: it's a fresh page.
							// We already mapped it. We might want to zero it.
							// memset((void*)fault_vva, 0, PAGE_SIZE);
						}
					}

					// Reuse victim element: update its virtual address to the new mapped page
					victim->virtual_address = fault_vva;

					// Set last_WS_element to the element after the victim (circular)
					struct WorkingSetElement *after = LIST_NEXT(victim);
					if (!after)
						after = LIST_FIRST(&(faulted_env->page_WS_list));
					faulted_env->page_last_WS_element = after;

					// cprintf("DEBUG: replaced victim %x with %x, last_ws set to %x\n", victim_vva, fault_vva, (after? after->virtual_address:0));
				}
				else
				{
					// This should be unreachable due to the while(1) loop and algorithm guarantees
					panic("Modified_Clock No victim found! This should not happen.");
				}
			}
		}
	}
#endif
}

void __page_fault_handler_with_buffering(struct Env *curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}
