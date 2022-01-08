/* Swap file implementation
 * (c) Jack Lange, 2012
 */

#include <linux/slab.h>
#include <linux/string.h>

#include "file_io.h"
#include "swap.h"
#define POWER_4KB 12
#define BITS_IN_A_BYTE 3 // 1 byte = 2^3 bits?

/* this function doesn't need a parameter; 
 * the swap size is specified when we 
 * manually create /tmp/cs452.swap 
 * the command we use: dd if=/dev/zero of=/tmp/cs452.swap bs=4096 count=256 */
struct swap_space * swap_init(void) {
    u32 pages, char_map_page_size;
    struct swap_space * swap = kmalloc(sizeof(struct swap_space), GFP_KERNEL);
	printk(KERN_INFO "initializing the swap space\n");
    swap->swap_file = file_open("/tmp/cs452.swap", O_RDWR);
    if(!(swap->swap_file)){
        //BIG PROBLEM!
        return (struct swap_space * ) 0x0;
    }
    swap->size = file_size(swap->swap_file);
    pages = swap->size >> POWER_4KB; // 4kb per page
    swap->size = pages; // if we have 256 pages in total, then swap->size is 256.
	/* let's say we have 256 pages, then we need 256>>3, 
	 * which is 2^8>>3=2^5, and then 2^5+1=33, 
	 * so we first read 33 bytes into a memory buffer pointed by swap->alloc_map,
	 * and 33 bytes is 264 bits. */
    char_map_page_size = pages >> BITS_IN_A_BYTE;
    if(char_map_page_size % BITS_IN_A_BYTE != 0){
        char_map_page_size += 1;
    }
	/* read the swap file, and read its content into swap->alloc_map. */
    swap->alloc_map = kmalloc(char_map_page_size, GFP_KERNEL);
    file_read(swap->swap_file, (swap->alloc_map), char_map_page_size, 0);

    return swap;
}


/*
 * Returns 0 if the space is free.
 * Returns 1 if the space is allocated.
 * Returns -1 if you tried to access reserved space or outside the bounds.
 */
int check_bitmap(struct swap_space * swap, u32 index){
    if(index < swap->size){
        u32 byte_location, bit_location;
        char b;
        byte_location = index >> 3;
        bit_location = index % 8;
        b = swap->alloc_map[byte_location];
        return (b & (1 << bit_location)) >> bit_location;
    }
    return -1;
}

void put_value(struct swap_space * swap, u32 index, u8 value){
    if(index < swap->size){
        u32 byte_location, bit_location;
        char b;
        byte_location = index >> 3;
        bit_location = index % 8;
        b = swap->alloc_map[byte_location];
        b &= ~(1 << bit_location);
        b |= value << bit_location;
        swap->alloc_map[byte_location] = b;
    }
}



void swap_free(struct swap_space * swap) {
	printk(KERN_INFO "free the swap space\n");
    file_close(swap->swap_file);
    kfree(swap->alloc_map);
    kfree(swap);
}


int swap_out_page(struct swap_space * swap, u32 * index, void * page) {
	int i;
	for(i = 0; i < swap->size; i++){
		/* as soon as we find one bit which is 0, then we set it to 1 and use it. */
		if(check_bitmap(swap, i) == 0){
			put_value(swap, i, 1);
			/* and we record this page is written into page i of the swap space. */
			*index = i;
			/* swap out to disk. write page into swap space. */
			file_write(swap->swap_file, page, 4096, i * 4096);
			printk("FOUND DAT FILE AT %d\n", i);
			break;
		}
	}
	return -1;
}

int swap_in_page(struct swap_space * swap, u32 index, void * dst_page) {
    printk("Index is: %d", index);
	/* swap into memory, read the page into dst_page. */
    file_read(swap->swap_file, dst_page, 4096, index * 4096);
    put_value(swap, index, 0); //Free up space in the swap bitmap
    return 0;
}

/* vim: set ts=4: */
