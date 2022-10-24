/* On demand Paging Implementation
 * (c) Jack Lange, 2012
 */

#include <linux/slab.h>
#include <linux/string.h>

#include "petmem.h"
#include "pgtables.h"
#include "on_demand.h"
#include "swap.h"

#define PHYSICAL_OFFSET(x) (((u64)x) & 0xfff)
#define PAGE_SIZE_BYTES 4096
#define PAGE_IN_USE 1
#define PAGE_NOT_IN_USE 2
#define ERROR_PERMISSION 2
#define NOT_VALID_RANGE 1
#define ALLOCATED_ADDRESS_RANGE 2
#define DEBUG 1
#define CLOCK_POLICY "clock"
#define FIFO_POLICY "fifo"



/* when user testing program opens /dev/petmem, this function gets called by petmem_open(),
 * which initializes a list head represented by new_proc->memory_allocations,
 * and adds it to the list first_node->list. 
 * but both new_proc and first_node are local pointers. 
 * in order to access these two later, here we return new_proc and in petmem_open(),
 * this return value is assigned to flip->private_data, so
 * later on when we need to access new_proc and first_node, we
 * just need to access this flip->private_data. We can see this in petmem_ioctl().
 * this is how we pass information from petmem_open() to petmem_ioctl(). */
struct mem_map * petmem_init_process(void) {
	struct mem_map * new_proc;
	struct vaddr_reg * first_node = (struct vaddr_reg *) kmalloc(sizeof(struct vaddr_reg), GFP_KERNEL);
	struct swap_space * swaps = swap_init();
    printk(KERN_INFO "process initialization...\n");
	new_proc = (struct mem_map *)kmalloc(sizeof(struct mem_map), GFP_KERNEL);
	INIT_LIST_HEAD(&(new_proc->memory_allocations));  // Makes circular list. Sets next and prev by itself
    INIT_LIST_HEAD(&(new_proc->clock_hand));
    new_proc->policy_name = CLOCK_POLICY;

	first_node->status = FREE;
	first_node->size = ((PETMEM_REGION_END - PETMEM_REGION_START) >> PAGE_POWER_4KB); // No of pages
	first_node->page_addr = PETMEM_REGION_START;

	INIT_LIST_HEAD(&(first_node->list));
    new_proc->swap = swaps;
	list_add(&(first_node->list), &(new_proc->memory_allocations));
    // void list_add(struct list_head *new, struct list_head *head); add a new entry just after the head node.
    // It works as stack. New node is placed just after the head node.
    // new_proc->clock_hand = new_proc->memory_allocations = first_node->list
    // filp->private_data = new_proc
    return new_proc;

}

// de-initialize the whole address space.
void petmem_deinit_process(struct mem_map * map) {  // map gets the filp->private_data
	struct list_head * pos, * next;
	struct vaddr_reg *entry;
    struct vp_node *node;
    int i;
    //Frees up the swap space

    swap_free(map->swap);
	list_for_each_safe(pos, next, &(map->memory_allocations)){ // https://www.kernel.org/doc/htmldocs/kernel-api/API-list-for-each-safe.html
        // next is actually n; a temporary storage
		entry = list_entry(pos, struct vaddr_reg, list); // cast pos to vaddr_reg. list = the name of the list_head within the struct.
        for(i = 0; i < entry->size; i++){ // Takes each virtual page tries to free it if physical memory is attached.
            attempt_free_physical_address(entry->page_addr + (4096*i));
        }
		list_del(pos);
		kfree(entry);
	}

    list_for_each_safe(pos, next, &(map->clock_hand)) {
        node = list_entry(pos, struct vp_node, list);
        list_del(pos);
        kfree(node);
    }

	kfree(map);

}

/* called by petmem_ioctl() in case of LAZY_ALLOC. */
uintptr_t petmem_alloc_vspace(struct mem_map * map, u64 num_pages) { // Only for allocating virtual memory
    printk("Memory allocation\n");
    return allocate(&(map->memory_allocations), num_pages);
}

void petmem_dump_vspace(struct mem_map * map) {
}

// Only the PML needs to stay, everything else can be freed
void petmem_free_vspace(struct mem_map * map, uintptr_t vaddr) {
    printk("Free memory\n");
	free_address(&(map->memory_allocations), vaddr);
    return;

}

/* called by page fault handler to handle the multiple level of page tables. */
/*
 * Though this does use pte64_t, it works with
 * all types of 64, but there is no general one.
 */
int handle_table_memory(void * mem, struct mem_map * map){
    uintptr_t temp;
    uintptr_t memory;
    pte64_t * handle = (pte64_t *)mem;
    struct vp_node *new_node;

    memory = petmem_alloc_pages(1);
    if (memory == 0) {
        clear_up_memory(map, handle);
        memory = petmem_alloc_pages(1);
    } else {
        new_node = (struct vp_node *)kmalloc(sizeof(struct vp_node), GFP_KERNEL);
        new_node->pte = handle;
		INIT_LIST_HEAD(&(new_node->list));
        list_add_tail(&(new_node->list), &(map->clock_hand)); // It should be a queue.
    }
    temp = (uintptr_t)__va(memory);
    printk("Allocated virtual memory is: 0x%012lx, and its physical memory is:0x%012lx\n", temp, __pa(temp));
   //screw it, other way didn't copy permissions, must set them!
    memset((void *)temp, 0, 512*8);
	handle->present = 1;
    handle->writable = 1;
    handle->user_page =1;
	handle->page_base_addr = PAGE_TO_BASE_ADDR( __pa(temp ));
    return 0;
}

uintptr_t get_valid_page_entry(uintptr_t address){
    pte64_t * entries[4];
    /* If one of them isn't there, we don't need to free any physical address because there is none.*/
    entries[3] = (pte64_t *) (CR3_TO_PML4E64_VA( get_cr3() ) + PML4E64_INDEX( address ) * 8);
    if(!entries[3]->present) {
        return 0;
    }
    entries[2] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[3]->page_base_addr ) + (PDPE64_INDEX( address ) * 8)) ;
    if(!entries[2]->present) {
        return 0;
    }

    entries[1] = (pte64_t *)__va(BASE_TO_PAGE_ADDR( entries[2]->page_base_addr ) + PDE64_INDEX( address )* 8);
    if(!entries[1]->present) {
        return 0;
    }

    // entries[1]->page_base_addr  = Physical page number of PTE and PTE64_INDEX( address ) 12-20 bits of CR3
    entries[0] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[1]->page_base_addr ) + PTE64_INDEX( address ) * 8 );
    if(!entries[0]->present) {
        return 0;
    }
    return (uintptr_t)entries[0];
}

// mem: page to replace, pte: new page
void * page_replacement_clock(struct mem_map * map, void ** mem, void * pte){
    pte64_t * page;
    struct list_head * pos, * next;
    struct vp_node *node;

    while (1) {
        list_for_each_safe(pos, next, &(map->clock_hand)) {
            node = list_entry(pos, struct vp_node, list);
            page = (pte64_t *)node->pte;

            if (page && page->accessed) {
                page->accessed = 0;
                printk("Found a page, but it gets a second chance. lucky bastard.\n");
            }
            else if (page) {
                list_move_tail(&(map->clock_hand), &(node->list)); // Change clock hand
                node->pte = pte;
                printk("FOUND A PAGE TO REPLACE!!!\n");
                *mem = (__va( BASE_TO_PAGE_ADDR( page->page_base_addr ) ));
                return (void *)page;
            }
        }
    }
}

void * page_replacement_fifo(struct mem_map * map, void ** mem, void * pte){
    pte64_t * page;
    struct vp_node *node;

    node = list_entry(map->clock_hand.next, struct vp_node, list);
    page = (pte64_t *)node->pte;
    list_move_tail(&(node->list), &(map->clock_hand)); // Move the node so that it will pop last from queue
    node->pte = pte;

    printk("FOUND A PAGE TO REPLACE!!!\n");
    *mem = (__va( BASE_TO_PAGE_ADDR( page->page_base_addr ) ));
    return (void *)page;
}

void clear_up_memory(struct mem_map * map, void * pte) { 
    u32 index;
    pte64_t * page_to_replace, * mem_location;

    index = 0;
    printk("GETTING SOME MO MEMZ\n");
    /* pick a page based on the swap policy - clock policy is default */
    if (strcmp(map->policy_name, FIFO_POLICY) == 0) {
        page_to_replace = (pte64_t *)page_replacement_fifo(map, (void **)&mem_location, pte);
    } else {
        page_to_replace = (pte64_t *)page_replacement_clock(map, (void **)&mem_location, pte);
    }
    page_to_replace->present = 0;
    page_to_replace->dirty = 1;
    swap_out_page(map->swap, &index, mem_location);
    petmem_free_pages((uintptr_t)__pa(mem_location), 1);
	/* we memorize that this page is written to index page of the swap space. */
    page_to_replace->page_base_addr = index;

}
int petmem_handle_pagefault(struct mem_map * map, uintptr_t fault_addr, u32 error_code) {
	pml4e64_t * cr3;
	pdpe64_t * pdp;
	pde64_t * pde;
	pte64_t * pte;
    int bad_signal = 0;
    int valid_range = check_address_range(map, fault_addr);
    char * space;

    printk("Handling segfault\n");
    if(valid_range == NOT_VALID_RANGE|| error_code == ERROR_PERMISSION ){
        return -1;
    }

    cr3 = (pml4e64_t *) (CR3_TO_PML4E64_VA( get_cr3() ) + PML4E64_INDEX( fault_addr ) * 8);
    if(!cr3->present) {
       GENERATE_TABLE(cr3, pdp_base_addr);
    }

    pdp = (pdpe64_t *)__va( BASE_TO_PAGE_ADDR( cr3->pdp_base_addr ) + (PDPE64_INDEX( fault_addr ) * 8)) ;
    if(!pdp->present) {
        GENERATE_TABLE(pdp, pd_base_addr);

    }

    pde = (pde64_t *)__va(BASE_TO_PAGE_ADDR( pdp->pd_base_addr ) + PDE64_INDEX( fault_addr )* 8);
    if(!pde->present) {
        GENERATE_TABLE(pde, pt_base_addr);
    }

    pte = (pte64_t *)__va( BASE_TO_PAGE_ADDR( pde->pt_base_addr ) + PTE64_INDEX( fault_addr ) * 8 );

    // TODO: Check the dirty bit as well, to differentiate between compulsory vs swapped out.

    if (!pte->present) {
        if(!pte->dirty) { // Dirty means it was touched at least once in its lifetime
            bad_signal += handle_table_memory((void *) pte, map);
        }
        else {
            void * page = kmalloc(4096,GFP_KERNEL);
            //Swap out memory using page_address.
            printk("Got here\n");
            /* in page fault handler, we know we run of memory, so we swap a page in. */
            swap_in_page(map->swap, pte->page_base_addr, page);
            printk("Swapped in the page\n");
            space = (void *)petmem_alloc_pages(1);
            /* when space is 0, it tells us it's time to swap some pages out. */
            if (space == 0){
                clear_up_memory(map, pte);
                space = (void * )petmem_alloc_pages(1);
            }
            printk("Allocated space for new page.\n");
            space = (void *)__va(space);
            memcpy(space, page, PAGE_SIZE_BYTES);
            kfree(page);
            printk("SPACE\n");
            printk("Should be a b: %c\n", space[0]);
            pte->present = 1;
            pte->writable = 1;
            pte->user_page = 1;
            pte->dirty = 0;
            pte->page_base_addr = PAGE_TO_BASE_ADDR( __pa(space));
            printk("Done.\n");
        }
    }
#ifdef DEBUG
    printk("~~~~~~~~~~~~~~~~~~~~~NEW PAGE FAULT!~~~~~~~~~~~~\n");
    printk("Error code: %d\n", error_code);
	printk("Fault Address 0x%012lx\n", fault_addr);

    printk("\nCR3 FULL SUMMARY:\n");
    printk("PML4 offset: %lld\n", PML4E64_INDEX(fault_addr));
    printk("PML4 index: %lld (0x%03x)\n", PML4E64_INDEX(fault_addr) * 8, (int)PML4E64_INDEX(fault_addr) * 8);
    printk("The Physical Address:0x%012llx\n", (CR3_TO_PML4E64_PA( get_cr3() ) + PML4E64_INDEX( fault_addr) * 8));
    printk("Virtual Address: 0x%012lx\n", (long unsigned int)cr3);
    printk("Page address to next level from cr3 : 0x%012lx\n", (long unsigned int)cr3->pdp_base_addr);
    printk("\nPDP FULL SUMMARY:\n");
    printk("PDP offset: %lld\n", PDPE64_INDEX(fault_addr));
    printk("PDP index: %lld (0x%03x)\n", PDPE64_INDEX(fault_addr) * 8, (int)PDPE64_INDEX(fault_addr) * 8);
    printk("The Physical Address:0x%012llx\n", (BASE_TO_PAGE_ADDR(cr3->pdp_base_addr) + PDPE64_INDEX( fault_addr) * 8));
    printk("Virtual Address: 0x%012lx\n", (long unsigned int)pdp);
    printk("Page address to next level from pdp : 0x%012lx\n", (long unsigned int)pdp->pd_base_addr);
    printk("\nPDE FULL SUMMARY:\n");
    printk("PDE offset: %lld\n", PDE64_INDEX(fault_addr));
    printk("PDE index: %lld (0x%03x)\n", PDE64_INDEX(fault_addr) * 8, (int)PDE64_INDEX(fault_addr) * 8);
    printk("Hmmm...what is the PDE index? %lld\n", PDE64_INDEX(fault_addr) * 8);
    printk("The Physical Address:0x%012llx\n", (BASE_TO_PAGE_ADDR(pdp->pd_base_addr) + PDE64_INDEX( fault_addr) * 8));   printk("Virtual Address: 0x%012lx\n", (long unsigned int)pde);
    printk("Page address to next level from pde : 0x%012lx\n", (long unsigned int)pde->pt_base_addr);
    printk("\nPTE FULL SUMMARY:\n");
    printk("PTE offset: %lld\n", PTE64_INDEX(fault_addr));
    printk("PTE index: %lld (0x%03x)\n", PTE64_INDEX(fault_addr) * 8, (int)PTE64_INDEX(fault_addr) * 8);
    printk("The Physical Address:0x%012llx\n", (BASE_TO_PAGE_ADDR(pde->pt_base_addr) + PTE64_INDEX( fault_addr) * 8));
    printk("Virtual Address: 0x%012lx\n", (long unsigned int)pte);
    printk("Memory at this : 0x%012lx\n", (long unsigned int)pte->page_base_addr);
#endif
    if(bad_signal){
        return -1;
    }
    return 0;

}


void attempt_free_physical_address(uintptr_t address){
    pte64_t * tables[4];
    pte64_t * entries[4];
    void * actual_mem;
    int i;
    /* If one of them isn't there, we don't need to free any physical address because there is none.*/
    entries[3] = (pte64_t *) (CR3_TO_PML4E64_VA( get_cr3() ) + PML4E64_INDEX( address ) * 8);
    tables[3] = (pte64_t *)(CR3_TO_PML4E64_VA(get_cr3()));
    if(!entries[3]->present) {
        return;
    }
    entries[2] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[3]->page_base_addr ) + (PDPE64_INDEX( address ) * 8)) ;
    tables[2] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[3]->page_base_addr )) ;
    if(!entries[2]->present) {
        return;
    }

    entries[1] = (pte64_t *)__va(BASE_TO_PAGE_ADDR( entries[2]->page_base_addr ) + PDE64_INDEX( address )* 8);
    tables[1] = (pte64_t *)__va(BASE_TO_PAGE_ADDR( entries[2]->page_base_addr ));
    if(!entries[1]->present) {
        return;
    }

    entries[0] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[1]->page_base_addr ) + PTE64_INDEX( address ) * 8 );
    tables[0] = (pte64_t *)__va( BASE_TO_PAGE_ADDR( entries[1]->page_base_addr ));
    if(!entries[0]->present) {
        return;
    }

    actual_mem = (void *)__va( BASE_TO_PAGE_ADDR( entries[0]->page_base_addr ) + PHYSICAL_OFFSET( address ) );
    petmem_free_pages((uintptr_t)actual_mem, 1);
    for(i = 0; i < 4; i++){
        pte64_t * cur = entries[i];
        cur->writable = 0;
        cur->user_page = 0;
        cur->present = 0;
        cur->page_base_addr = 0;
		/* invalidate tlb entry for this page */
        invlpg((uintptr_t) cur);
        if(is_entire_page_free((void *) tables[i]) == PAGE_NOT_IN_USE){
            printk("Table %d is being freed\n", i+1);
            petmem_free_pages((uintptr_t) tables[i], 1);
            invlpg((uintptr_t) tables[i]);
        }
        else{
            return;
        }
    }}

int is_entire_page_free(void * page_structure){
    int i = 0;
    for(i = 0; i < 512; i++){
        int offset = i * 8;
        pte64_t * page = (page_structure + offset);
        if(page->present == 1){
            return PAGE_IN_USE;
        }
    }
    return PAGE_NOT_IN_USE;
}

void free_address(struct list_head * head_list, u64 page){ // Page is the address here
	struct vaddr_reg * cur, * found, *next, *prev;
    int i;
	found = NULL;
	list_for_each_entry(cur ,head_list, list){
		if(cur->page_addr == page){
		    found = cur;
        }
	}
	if(found == NULL){
		return;
	}
	//Remove actually allocated pages here.
    for( i = 0; i < found->size; i++){
        attempt_free_physical_address(found->page_addr + (i * 4096));

    }
	//Set the clear values.
	found->status = FREE;
    for(i = 0; i < found->size; i++){

        invlpg(found->page_addr + (i * 4096)); // NOTE: Remember that you must notify the hardware with invlpg whenever you modify a
                                               //page entry that might have been cached in the TLB.
    }

	//Coalesce nodes.
	next = list_entry(found->list.next, struct vaddr_reg, list);
	prev = list_entry(found->list.prev, struct vaddr_reg, list);

	if(next->page_addr != page && next->status == FREE){
		list_del(found->list.next); // We will delete the next because FREE next was generated from this current node
		found->size += next->size;
		kfree(next);
	}
	if(prev->page_addr != page && prev->status == FREE){  // prev allocated first then current allocated. prev freed first then now current is freeing
        found->page_addr = prev->page_addr;
		list_del(found->list.prev);
		found->size += prev->size;
		kfree(prev);
	}

}

/* petmem_ioctl() calls petmem_alloc_vspace() using LAZY_ALLOC, which calls this allocate(),
 * and the 1st parameter passed in is that new_proc. */
uintptr_t  allocate(struct list_head * head_list, u64 size){  // head_list is new_proc->memory_allocations and size is num of pages
	struct vaddr_reg *cur, *node_to_consume, *new_node;
	u64 current_size;
	node_to_consume = NULL;
	/* traverse map->memory_allocations, find one whose status is FREE. */
	list_for_each_entry(cur, head_list, list){  // cur = position; head_list = head of the list; list = name of the list_head within the struct.
		if(cur->status == FREE && cur->size >= size){
			node_to_consume = cur;
			break;
		}
	}
	/* if no nodes is free and has a size larger than the requested size, then return -1. */
	if(node_to_consume == NULL){
		return -1;
	}

	printk("Node to break apart: %p\n", (void *)node_to_consume->page_addr);
	node_to_consume->status = ALLOCATED;
	if(node_to_consume->size == size){
		return node_to_consume->page_addr;
	}

	current_size = node_to_consume->size;
	current_size -= size;
	new_node = (struct vaddr_reg*)kmalloc(sizeof(struct vaddr_reg), GFP_KERNEL);
	INIT_LIST_HEAD(&(new_node->list));
	/* add new_node->list to the existing list. */
	list_add(&(new_node->list), &(node_to_consume->list));
	new_node->size = current_size;
	new_node->page_addr = node_to_consume->page_addr + (size << PAGE_POWER_4KB);
	new_node->status = FREE;

	node_to_consume->size = size;
	/* so basically we add new_node to the list and keep both new_node and node_to_consume,
	 * but we return node_to_consume->page_addr to the caller. note that the size of node_to_consumer is deducted,
	 * and we keep the remainder in new_node. */
	return node_to_consume->page_addr;
}

void print_bits(u64 * num){
    u64 current_bit = 1;
    int i = 0;
    for(i = 0; i < 64; i++){
        printk("%1d", (int)((*num) & current_bit) >> (i));
        current_bit = current_bit << 1;
    }
}
int check_address_range(struct mem_map * map, uintptr_t address){
 	struct vaddr_reg * cur;
	list_for_each_entry(cur, &(map->memory_allocations), list){
        if(cur->status == ALLOCATED && address >= cur->page_addr && address < (cur->page_addr + 4096 * cur->size)){
            return ALLOCATED_ADDRESS_RANGE;
        }
	}
    return NOT_VALID_RANGE;

}

/* vim: set ts=4: */
