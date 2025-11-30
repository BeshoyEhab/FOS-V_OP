#include <inc/memlayout.h>
#include "shared_memory_manager.h"

#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/queue.h>
#include <inc/environment_definitions.h>

#include <kern/proc/user_environment.h>
#include <kern/trap/syscall.h>
#include "kheap.h"
#include "memory_manager.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] INITIALIZE SHARES:
//===========================
// Initialize the list and the corresponding lock
void sharing_init()
{
#if USE_KHEAP
	LIST_INIT(&AllShares.shares_list);
	init_kspinlock(&AllShares.shareslock, "shares lock");
	// init_sleeplock(&AllShares.sharessleeplock, "shares sleep lock");
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//=========================
// [2] Find Share Object:
//=========================
// Search for the given shared object in the "shares_list"
// Return:
//	a) if found: ptr to Share object
//	b) else: NULL
struct Share *find_share(int32 ownerID, char *name)
{
#if USE_KHEAP
	struct Share *ret = NULL;
	bool wasHeld = holding_kspinlock(&(AllShares.shareslock));
	if (!wasHeld)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}
	{
		struct Share *shr;
		LIST_FOREACH(shr, &(AllShares.shares_list))
		{
			// cprintf("shared var name = %s compared with %s\n", name, shr->name);
			if (shr->ownerID == ownerID && strcmp(name, shr->name) == 0)
			{
				// cprintf("%s found\n", name);
				ret = shr;
				break;
			}
		}
	}
	if (!wasHeld)
	{
		release_kspinlock(&(AllShares.shareslock));
	}
	return ret;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//==============================
// [3] Get Size of Share Object:
//==============================
int size_of_shared_object(int32 ownerID, char *shareName)
{
	// This function should return the size of the given shared object
	// RETURN:
	//	a) If found, return size of shared object
	//	b) Else, return E_SHARED_MEM_NOT_EXISTS
	//
	struct Share *ptr_share = find_share(ownerID, shareName);
	if (ptr_share == NULL)
		return E_SHARED_MEM_NOT_EXISTS;
	else
		return ptr_share->size;

	return 0;
}
//===========================================================

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=====================================
// [1] Alloc & Initialize Share Object:
//=====================================
// Allocates a new shared object and initialize its member
// It dynamically creates the "framesStorage"
// Return: allocatedObject (pointer to struct Share) passed by reference
struct Share *alloc_share(int32 ownerID, char *shareName, uint32 size, uint8 isWritable)
{
	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #1 alloc_share
	// Your code is here
	struct Share *share = kmalloc(sizeof(struct Share));
	if (share == NULL)
		return NULL;
	share->references = 1;
	share->ID = (uint32)share & 0x7FFFFFFF;
	share->ownerID = ownerID;
	strcpy(share->name, shareName);
	share->size = size;
	share->isWritable = isWritable;
	share->framesStorage = kmalloc((ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE) * sizeof(struct FrameInfo *));
	if (share->framesStorage == NULL)
	{
		kfree(share);
		return NULL;
	}
	memset(share->framesStorage, 0, (ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE) * sizeof(struct FrameInfo *));

	return share;
	// Comment the following line
	// panic("alloc_share() is not implemented yet...!!");
}

//=========================
// [4] Create Share Object:
//=========================
int create_shared_object(int32 ownerID, char *shareName, uint32 size, uint8 isWritable, void *virtual_address)
{
	// // TODO: [PROJECT'25.IM#3] SHARED MEMORY - #3 create_shared_object
	// // Your code is here
	// // Comment the following line
	// // panic("create_shared_object() is not implemented yet...!!");

	struct Env *myenv = get_cpu_proc(); // The calling environment
	uint32 j = 0;
	void *shared_obj = find_share(ownerID, shareName);
	if (shared_obj != NULL)
	{
		return E_SHARED_MEM_EXISTS;
	}
	struct Share *share = alloc_share(ownerID, shareName, size, isWritable);
	if (share == NULL)
	{
		return E_NO_SHARE;
	}

	for (uint32 i = ROUNDDOWN(virtual_address, PAGE_SIZE); i < virtual_address + size; i += PAGE_SIZE)
	{
		struct FrameInfo *frameinfo = NULL;
		int a_f = allocate_frame(&frameinfo);
		if (a_f == E_NO_MEM)
		{
			return E_NO_SHARE;
		}
		a_f = map_frame(myenv->env_page_directory, frameinfo, i, PERM_USER | PERM_WRITEABLE | PERM_PRESENT);
		if (a_f == E_NO_MEM)
		{
			for (uint32 k = ROUNDDOWN(virtual_address, PAGE_SIZE); k <= i; k += PAGE_SIZE)
			{
				unmap_frame(myenv->env_page_directory, k);
			}
			return E_NO_SHARE;
		}

		share->framesStorage[j] = frameinfo;
		j++;
	}
	acquire_kspinlock(&AllShares.shareslock);
	LIST_INSERT_TAIL(&AllShares.shares_list, share);
	release_kspinlock(&AllShares.shareslock);
	return share->ID;
}

//======================
// [5] Get Share Object:
//======================
int get_shared_object(int32 ownerID, char *shareName, void *virtual_address)
{
	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #5 get_shared_object
	// Your code is here
	// Comment the following line
	// panic("get_shared_object() is not implemented yet...!!");

	struct Env *myenv = get_cpu_proc(); // The calling environment
	uint32 j = 0;
	struct Share *share = find_share(ownerID, shareName);
	if (share == NULL)
	{
		return E_SHARED_MEM_NOT_EXISTS;
	}
	uint32 num = ROUNDUP(share->size, PAGE_SIZE) / PAGE_SIZE;
	for (uint32 i = virtual_address; i < (virtual_address + (num * PAGE_SIZE)); i += PAGE_SIZE)
	{
		struct FrameInfo *frameinfo = share->framesStorage[j];
		map_frame(myenv->env_page_directory, frameinfo, i, PERM_USER | PERM_PRESENT | (share->isWritable ? PERM_WRITEABLE : 0));

		j++;
	}

	share->references++;
	// 	This function should share the required object in the heap of the current environment
	//	starting from the given virtual_address with the specified permissions of the object: read_only/writable
	// 	and return the ShareObjectID
	// RETURN:
	//	a) ID of the shared object (its VA after masking out its msb) if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists
	return share->ID;
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//
//=========================
// [1] Delete Share Object:
//=========================
// delete the given shared object from the "shares_list"
// it should free its framesStorage and the share object itself
void free_share(struct Share *ptrShare)
{
	// TODO: [PROJECT'25.BONUS#5] EXIT #2 - free_share
	// Your code is here
	// Comment the following line
	panic("free_share() is not implemented yet...!!");
}

//=========================
// [2] Free Share Object:
//=========================
int delete_shared_object(int32 sharedObjectID, void *startVA)
{
	// TODO: [PROJECT'25.BONUS#5] EXIT #2 - delete_shared_object
	// Your code is here
	// Comment the following line
	panic("delete_shared_object() is not implemented yet...!!");

	struct Env *myenv = get_cpu_proc(); // The calling environment

	// This function should free (delete) the shared object from the User Heapof the current environment
	// If this is the last shared env, then the "frames_store" should be cleared and the shared object should be deleted
	// RETURN:
	//	a) 0 if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists

	// Steps:
	//	1) Get the shared object from the "shares" array (use get_share_object_ID())
	//	2) Unmap it from the current environment "myenv"
	//	3) If one or more table becomes empty, remove it
	//	4) Update references
	//	5) If this is the last share, delete the share object (use free_share())
	//	6) Flush the cache "tlbflush()"
}
