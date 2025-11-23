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
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
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
				// cprintf("User process accessing protected address space >= USER_TOP. Exiting.\n");
				env_exit();
			}

			int perm = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);

			if (!(perm & PERM_UHPAGE) && fault_va >= USER_HEAP_START && fault_va <= USER_HEAP_MAX)

			{
				// cprintf("User process accessing unmarked page in heap. Exiting.\n");
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
	// Your code is here
	// Comment the following line
	// panic("get_optimal_num_faults() is not implemented yet...!!");
}

//=============================
// Helper Functions
//=============================
struct WorkingSetElement* clearUsed(struct Env *faulted_env){
	struct WorkingSetElement* i = faulted_env->page_last_WS_element;
	if(i == NULL){
		i = LIST_FIRST(&(faulted_env->page_WS_list));
	}

	while(1){

		uint32 element_va = i->virtual_address;
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, ROUNDDOWN(element_va, PAGE_SIZE));
		if(perms == -1){
			env_exit();
		}

		if((perms & PERM_USED) == 0)
		{
			faulted_env->page_last_WS_element = LIST_NEXT(i);
			if(faulted_env->page_last_WS_element == NULL){
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
			return i;
		}

		pt_set_page_permissions(faulted_env->env_page_directory, element_va, 0, PERM_USED);
		i = LIST_NEXT(i);

		if(i == NULL){
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
		LIST_INSERT_TAIL(&(faulted_env->page_WS_list), last_element);
		if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
		{
			faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
		}
	}
	else
	{

		if (isPageReplacmentAlgorithmOPTIMAL())
		{
			// TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
			// Your code is here
			// Comment the following line
			panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		} else {

			if (isPageReplacmentAlgorithmCLOCK())
			{
				// TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				struct WorkingSetElement* victim =  clearUsed(faulted_env);

				int perms = pt_get_page_permissions(faulted_env->env_page_directory, victim->virtual_address);
			
				struct FrameInfo *frame_info;
				uint32 *ptr_page_table;
				get_page_table(faulted_env->env_page_directory, victim->virtual_address, &ptr_page_table);
				
				if(ptr_page_table == NULL) {
                	env_exit();
            	}

				pf_update_env_page(faulted_env, victim->virtual_address, frame_info);

				unmap_frame(faulted_env->env_page_directory, victim->virtual_address);

				LIST_REMOVE(&(faulted_env->page_WS_list), victim);

				struct FrameInfo *new_frame;
				allocate_frame(&new_frame);

				map_frame(faulted_env->env_page_directory, new_frame, ROUNDDOWN(fault_va, PAGE_SIZE), PERM_WRITEABLE | PERM_USER | PERM_PRESENT);

				 int read_result = pf_read_env_page(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));

				if (read_result == E_PAGE_NOT_EXIST_IN_PF) {
					if (!((ROUNDDOWN(fault_va, PAGE_SIZE) >= USER_HEAP_START && ROUNDDOWN(fault_va, PAGE_SIZE) < USER_HEAP_MAX) || 
						(ROUNDDOWN(fault_va, PAGE_SIZE) >= USTACKBOTTOM && ROUNDDOWN(fault_va, PAGE_SIZE) < USTACKTOP))) {
						env_exit();
					}
				}

				struct WorkingSetElement* new_element = env_page_ws_list_create_element(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));
            	LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_element);
				kfree(victim);
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				// TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				// Your code is here
				// Comment the following line

				struct WorkingSetElement *elem = LIST_FIRST(&(faulted_env->page_WS_list));
				struct WorkingSetElement *victim = NULL;

				//aging updater
				while (elem != NULL){
					uint32 va = elem->virtual_address;

					int perms = pt_get_page_permissions(faulted_env->env_page_directory, va);
					int used = (perms & PERM_USED) ? 1:0;

					elem->time_stamp = elem->time_stamp >> 1;
					elem->time_stamp |= (used << 31);

					if (used){ 
						pt_set_page_permissions(faulted_env->env_page_directory, va, 0, PERM_USED);
					}

					elem = LIST_NEXT(elem);
				}

				//min called victim
				elem = LIST_FIRST(&(faulted_env->page_WS_list));
				uint32 min_time = 0xFFFFFFFF;

				while(elem != NULL){
					if (elem->time_stamp < min_time){
						min_time = elem->time_stamp;
						victim = elem;
					}
					elem = LIST_NEXT(elem);
				}

				//del vctm
				if (victim != NULL){
					unmap_frame(faulted_env->env_page_directory, victim->virtual_address);
					LIST_REMOVE(&(faulted_env->page_WS_list),victim);
				}

				//add new frame
				struct FrameInfo *new_frame;
				allocate_frame(&new_frame);

				map_frame(faulted_env->env_page_directory, new_frame, ROUNDDOWN(fault_va, PAGE_SIZE),PERM_PRESENT | PERM_WRITEABLE | PERM_USED);
				

				struct WorkingSetElement *new_elem = env_page_ws_list_create_element(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));
				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_elem);

				if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size){
					faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
				}

				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK()){
				// TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				// Your code is here
				// Comment the following line
				struct WorkingSetElement *elem = faulted_env->page_last_WS_element;
				if(!elem){
					elem = LIST_FIRST(&(faulted_env->page_WS_list));
				}

				struct WorkingSetElement *victim = NULL;
				int found = 0;
				int ws_size = LIST_SIZE(&(faulted_env->page_WS_list));

				//trial 1
				for(int i =0; i<ws_size, i++){
					uint32 va = elem->virtual_address;
					int perms = pt_get_page_permissions(faulted_env->env_page_directory,va);
					int used = (perms & PERM_USED) ? 1:0;
					int modified = (perms & PERM_MODIFIED) ? 1:0;

					if(!used && !modified){
						victim = elem;
						found = 1;
						break;
					}

					elem = LIST_NEXT(elem);
					if(!elem){
						elem = LIST_FIRST(&(faulted_env->page_WS_list));
					}
				}

				//trial 2
				if (!found){
					for (int i = 0; i < ws_size; i++){
						uint32 va = elem->virtual_address;
						int perms = pt_get_page_permissions(faulted_env->env_page_directory, va, 0, PERM_MODIFIED);
						int used = (perm & PERM_USED) ? 1:0;
						int modified = (perms & PERM_MODIFIED) ? 1:0;
						
						if (!used && modified){
							victim = elem;
							found = 1;
							break;
						}

						if (used){
							pt_set_page_permissions(faulted_env->env_page_directory, va, 0, PERM_USED);
						}

						elem = LIST_NEXT(elem);
						if(!elem){
							LIST_FIRST(&(faulted_env->page_WS_list));
						}

					}
				}

				if (victim) {
					struct FrameInfo *frame;
					allocate_frame(&frame);
					map_frame(faulted_env->env_page_directory, frame, ROUNDDOWN(fault_va, PAGE_SIZE),PERM_PRESENT|PERM_WRITEABLE|PERM_USER);

					int read_page = pf_read_env_page(faulted_env, ROUNDDOWN(fault_va, PAGE_SIZE));
					if (read_page == E_PAGE_NOT_EXIST_IN_PF){
						env_exit();
					}

					struct WorkingSetElement *new_elem = env_page_ws_list_create_element(faulted_env,ROUNDDOWN(fault_va, PAGE_SIZE));

					LIST_INSERT_AFTER(&(faulted_env->page_WS_list),victim);
					LIST_REMOVE(&(faulted_env->page_WS_list),victim);

					faulted_env->page_last_WS_element = LIST_NEXT(new_elem);
					if (!faulted_env->page_last_WS_element){
						faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
					} 
	
				} else {
					panic("Modified Clock Replacement: No victim found, something went wrong!")
				}
				
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
// #else
// 	int iWS = faulted_env->page_last_WS_index;
// 	uint32 wsSize = env_page_ws_get_size(faulted_env);
#endif

}


void __page_fault_handler_with_buffering(struct Env *curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}
