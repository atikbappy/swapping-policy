#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "petmem.h"
#include "buddy.h"
#include "on_demand.h"
#include "pgtables.h"

MODULE_LICENSE("GPL");


struct class * petmem_class = NULL;
static struct cdev ctrl_dev;
static int major_num = 0;


LIST_HEAD(petmem_pool_list);

/* does this function return 0 when there is no physical memory available? */
uintptr_t petmem_alloc_pages(u64 num_pages) {
    uintptr_t vaddr = 0;
    struct buddy_mempool * tmp_pool = NULL;
    int page_order = get_order(num_pages * PAGE_SIZE) + PAGE_SHIFT; // PAGE_SHIFT is the number of bits to shift one bit left to get the PAGE_SIZE value; by default on x86 it should be 12, 2^12=4KB.

    // allocate from buddy
    list_for_each_entry(tmp_pool, &petmem_pool_list, node) {
	    // Get allocation size order
        vaddr = (uintptr_t)buddy_alloc(tmp_pool, page_order);
        if (vaddr) break;
    }

    if (!vaddr) {
	printk("Failed to allocate %llu pages\n", num_pages);
	return (uintptr_t)NULL;
    }

    return (uintptr_t)__pa(vaddr);
}


void petmem_free_pages(uintptr_t page_addr, u64 num_pages) {
    struct buddy_mempool * tmp_pool = NULL;
    int page_order = get_order(num_pages * PAGE_SIZE) + PAGE_SHIFT;
    uintptr_t page_va = (uintptr_t)__va(page_addr);

    printk("Freeing %llu pages at %p\n", num_pages, (void *)page_va);

    list_for_each_entry(tmp_pool, &petmem_pool_list, node) {
	if ((page_va >= tmp_pool->base_addr) &&
	    (page_va < tmp_pool->base_addr + (0x1 << tmp_pool->pool_order))) {

        printk("Actually freeing it.\n");
	    buddy_free(tmp_pool, (void *)page_va, page_order);
	    break;
	}
    }

    return;
}


static long petmem_ioctl(struct file * filp,
			 unsigned int ioctl, unsigned long arg) {
    void __user * argp = (void __user *)arg;


    printk("petmem ioctl\n");

    switch (ioctl) {
	case ADD_MEMORY: {
	    struct memory_range reg;
	    int reg_order = 0;
	    uintptr_t base_addr = 0;
	    u32 num_pages = 0;

	    if (copy_from_user(&reg, argp, sizeof(struct memory_range))) {
		printk("Error copying memory region from user space\n");
		return -EFAULT;
	    };


	    base_addr = (uintptr_t)__va(reg.base_addr);
	    num_pages = reg.pages/64;

	    // The order is equal to the highest order bit; ffs returns the position of the most significant set bit.
	    for (reg_order = fls(num_pages); reg_order != 0; reg_order = fls(num_pages)) {
		struct buddy_mempool * tmp_pool = NULL;

		printk("Adding pool of order %d (%d pages) at %p\n",
		       reg_order + PAGE_SHIFT - 1, num_pages, (void *)base_addr);

		/* in petmem.c, we call ioctl() to send the ADD_MEMORY command, which takes us to here to initialize the buddy system. 
 		 * page size is the minimum allocation size; pool order is (reg_order+PAGE_SHIFT-1), minimum order is PAGE_SHIFT). 
 		 * buddy system itself doesn't have the concept of page, rather it considers the concept of bytes. thus we need to convert
 		 * from the context of pages to the context to bytes. back and forth every time buddy system is involved. */
		tmp_pool = buddy_init(base_addr, reg_order + PAGE_SHIFT - 1, PAGE_SHIFT);

		if (tmp_pool == NULL) {
		    printk("ERROR: Could not initialize buddy pool for memory at %p (order=%d)\n",
			   (void *)base_addr, reg_order);
		    break;
		}
		/* and we call buddy_free right away? */
		buddy_free(tmp_pool, (void *)base_addr, reg_order + PAGE_SHIFT - 1);

		/* and we add tmp_pool->node to the global list petmem_pool_list,
		 * looks like they are trying to support multiple add operations. 
		 * in case the user sends ADD_MEMORY ioctl commands more than once. */
		list_add(&(tmp_pool->node), &petmem_pool_list);

		/* num_pages is changed here, thus the for loop will check again. it looks like even
		 * if the user only call ioctl with a command of ADD_MEMORY once, we may still iterate multiple times
		 * in this for loop - we keep looping as long as reg_order is not 0. this didn't make sense to me, then
		 * I converted num_pages to binary, for example, if num_pages is 11100, then in the 1st iteration, reg_order is 4,
		 * in the 2nd iteration, reg_order is 3, in the 3rd iteration, reg_order is 2, in the 4th iteration reg_order would finally be 0
		 * and we get out of the loop.  */
		num_pages -= (0x1 << (reg_order - 1));
		/* at first this base addr is passed in by the user, who obtained the address from the hot removeable memory subsystem. 
		 * this base_addr is just a local variable, we only use it as the 1st parameter to give to buddy_init. once again
		 * use the above 11100 example, in the 1st iteration, we have reg_order of 4,
		 * so we allocate 3 pages, and then move base_addr 3 pages forward to perform the next allocation, which is allocate 2 pages - because
		 * reg_order is 3, then we move base_addr 2 pages forward and allocate 1 page, because reg_order is 2. and then we are done, because
		 * reg_order is now 0. In such a case, after this for loop, the petmem_pool_list should have 3 more nodes added, each representing a pool of different size. */
		base_addr += ((0x1 << (reg_order - 1)) * PAGE_SIZE);
	    }

	    break;
	}


	/* allocate virtual memory? because ADD Memory is allocating physical memory. user/petmem.c doesn't issue this command, user/test.c does. */
	case LAZY_ALLOC: {
	    struct alloc_request req;
	    struct mem_map * map = filp->private_data;
	    u64 page_size = 0;
	    u64 num_pages = 0;

	    memset(&req, 0, sizeof(struct alloc_request));

	    if (copy_from_user(&req, argp, sizeof(struct alloc_request))) {
		printk("Error copying allocation request from user space\n");
		return -EFAULT;
	    }

	    printk("Requested allocation of %llu bytes\n", req.size);

	    page_size = (req.size + (PAGE_SIZE - 1)) & (~(PAGE_SIZE - 1));
	    num_pages = page_size >> PAGE_SHIFT;

	    req.addr = petmem_alloc_vspace(map, num_pages);

	    if (req.addr == 0) {
		printk("Error: Could not allocate virtual address region\n");
		return 0;
	    }

	    if (copy_to_user(argp, &req, sizeof(struct alloc_request))) {
		printk("Error copying allocation request to user space\n");
		return -EFAULT;
	    }

	    break;
	}

	/* when user calls pet_free(), it sends LAZY_FREE command to the kernel, we free the virtual memory - the entire virtual memory managed by us. */
	case LAZY_FREE: {
	    uintptr_t addr = (uintptr_t)arg;
	    struct mem_map * map = filp->private_data;

	    petmem_free_vspace(map, addr);

	    break;

	}

	case LAZY_DUMP_STATE: {
	    struct mem_map * map = filp->private_data;

	    petmem_dump_vspace(map);
	    break;
	}

	case PAGE_FAULT: {
	    struct page_fault fault;
	    struct mem_map * map = filp->private_data;

	    memset(&fault, 0, sizeof(struct page_fault));

	    if (copy_from_user(&fault, argp, sizeof(struct page_fault))) {
		printk("Error copying page fault info from user space\n");
		return -EFAULT;
	    }

	    if (petmem_handle_pagefault(map, (uintptr_t)fault.fault_addr, (u32)fault.error_code) != 0) {
		printk("error handling page fault for Addr:%p (error=%d)\n", (void *)fault.fault_addr, fault.error_code);
		return 1;
	    }



	    // 0 == success
	    return 0;
	}

	case INVALIDATE_PAGE: {
	    uintptr_t addr = (uintptr_t)arg;
	    invlpg(PAGE_ADDR(addr));
	    break;
	}


	case RELEASE_MEMORY:

	default:
	    printk("Unhandled ioctl (%d)\n", ioctl);
	    break;
    }

    return 0;

}

/* whenever /dev/petmem is opened, we call petmem_init_process().
 * and put the return value in private_data; and in this project,
 * the one who opens /dev/petmem is the user testing program, who calls
 * init_petmem(), who calls open() to open /dev/petmem. 
 * in addition, in petmem.c, they also call open() so as to send the ADD_MEMORY command. */
static int petmem_open(struct inode * inode, struct file * filp) {


    printk(KERN_INFO "openning /dev/petmem...\n");
    filp->private_data = petmem_init_process();

    return 0;
}

/* in the user program, they eventually call close(fd) to close the file 
 * /dev/petmem, and this release all the resource? if that's the case, then
 * are we supposed to access the added memory. And then when we use a test program,
 * we call init_petmem(), which will open /dev/petmem again, but this time the test program
 * won't add physical memory. therefore, buddy_system won't be initialized twice. 
 * when we call this petmem_deinit_process, it doesn't affect the buddy system, rather,
 * it affects the virtual memory of this process, that's why this function is called petmem_deinit_process. */
static int petmem_release(struct inode * inode, struct file * filp) {
    struct mem_map * map = filp->private_data;

    printk(KERN_INFO "closing /dev/petmem...\n");
    // garbage collect
    petmem_deinit_process(map);

    return 0;
}


static struct file_operations ctrl_fops = {
    .owner = THIS_MODULE,
    .open = petmem_open,
    .release = petmem_release,
    .unlocked_ioctl = petmem_ioctl,
    .compat_ioctl = petmem_ioctl
};



static int __init petmem_init(void) {
    dev_t dev = MKDEV(0, 0);
    int ret = 0;

    printk("-------------------------\n");
    printk("-------------------------\n");
    printk("Initializing Pet Memory manager\n");
    printk("-------------------------\n");
    printk("-------------------------\n");


    petmem_class = class_create(THIS_MODULE, "petmem");

    if (IS_ERR(petmem_class)) {
	printk("Failed to register Pet Memory class\n");
	return PTR_ERR(petmem_class);
    }

    ret = alloc_chrdev_region(&dev, 0, 1, "petmem");

    if (ret < 0) {
	printk("Error Registering memory controller device\n");
	class_destroy(petmem_class);
	return ret;
    }


    major_num = MAJOR(dev);
    dev = MKDEV(major_num, 0);

    cdev_init(&ctrl_dev, &ctrl_fops);
    ctrl_dev.owner = THIS_MODULE;
    ctrl_dev.ops = &ctrl_fops;
    cdev_add(&ctrl_dev, dev, 1);


    device_create(petmem_class, NULL, dev, NULL, "petmem");

    return 0;
}


static void __exit petmem_exit(void) {
    dev_t dev = 0;

    printk("Unloading Pet Memory manager\n");
    dev = MKDEV(major_num, 0);

    unregister_chrdev_region(MKDEV(major_num, 0), 1);
    cdev_del(&ctrl_dev);
    device_destroy(petmem_class, dev);

    class_destroy(petmem_class);


    // deinit buddy pools
    //    list_for_each_entry_safe(...)

}

module_init(petmem_init);
module_exit(petmem_exit);
