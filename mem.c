
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "mem.h"



/*
 * This structure serves as the header for each allocated and free block
 * It also serves as the footer for each free block
 * The blocks are ordered in the increasing order of addresses
 */
typedef struct blk_hdr {
    int size_status;
    
    /*
     * Size of the block is always a multiple of 8
     * => last two bits are always zero - can be used to store other information
     *
     * LSB -> Least Significant Bit (Last Bit)
     * SLB -> Second Last Bit
     * LSB = 0 => free block
     * LSB = 1 => allocated/busy block
     * SLB = 0 => previous block is free
     * SLB = 1 => previous block is allocated/busy
     *
     * When used as the footer the last two bits should be zero
     */
    
    /*
     * Examples:
     *
     * For a busy block with a payload of 20 bytes (i.e. 20 bytes data + an additional 4 bytes for header)
     * Header:
     * If the previous block is allocated, size_status should be set to 27
     * If the previous block is free, size_status should be set to 25
     *
     * For a free block of size 24 bytes (including 4 bytes for header + 4 bytes for footer)
     * Header:
     * If the previous block is allocated, size_status should be set to 26
     * If the previous block is free, size_status should be set to 24
     * Footer:
     * size_status should be 24
     *
     */
} blk_hdr;

/* Global variable - This will always point to the first block
 * i.e. the block with the lowest address */
blk_hdr *first_blk = NULL;

/*
 * Note:
 *  The end of the available memory can be determined using end_mark
 *  The size_status of end_mark has a value of 1
 *
 */

/*
 * Function for allocating 'size' bytes
 * Returns address of allocated block on success
 * Returns NULL on failure
 * Here is what this function should accomplish
 * - Check for sanity of size - Return NULL when appropriate
 * - Round up size to a multiple of 8
 * - Traverse the list of blocks and allocate the best free block which can accommodate the requested size
 * - Also, when allocating a block - split it into two blocks
 * Tips: Be careful with pointer arithmetic
 */
void* Mem_Alloc(int size) {
    if (size <= 0) {
        return NULL;
    }
    size += 4;
    if (size % 8 != 0) {
        size = size + 8 - (size % 8);
    }
    blk_hdr* current = first_blk;
    blk_hdr* bestFit = NULL;
    int bestSize = 2147483647;
    int current_size;
    while (current->size_status != 1) {
        current_size = current->size_status - current->size_status % 8;
        if (!(current->size_status & 1) && current_size >= size) {
            if (current_size < bestSize) {
                bestFit = current;
                bestSize = bestFit->size_status - (bestFit->size_status % 8);
            }
        }
        current = (blk_hdr*)((char*)current + current_size);
    }
    if (bestFit == NULL)
        return NULL;
    if ((bestFit->size_status - bestFit->size_status % 8) > size) {
        int temp = bestFit->size_status;
        bestFit->size_status = size + (temp & 2) + 1;
        int previous = bestFit->size_status - bestFit->size_status % 8;
        char* address = (char*)bestFit;
        bestFit = (blk_hdr*)((char*)bestFit + bestFit->size_status
                             - bestFit->size_status % 8);
        bestFit->size_status = (temp - temp % 8) - previous + 2;
        blk_hdr *footer = (blk_hdr*) ((char*)bestFit + bestFit->size_status
                                      - 2 - 4);
        footer->size_status = bestFit->size_status - 2;
        return address + 4;
    } else if ((bestFit->size_status - bestFit->size_status % 8)
               == size) {
        bestFit->size_status = bestFit->size_status + 1;
        int bestFitSize = (bestFit->size_status - (bestFit->size_status % 8));
        blk_hdr *next = (blk_hdr*) ((char*)bestFit + bestFitSize);
        if (next->size_status != 1) {
            next->size_status = next->size_status + 2;
        }
        return (char*)bestFit + 4;
    }
    return NULL;
}

/*
 * Function for freeing up a previously allocated block
 * Argument - ptr: Address of the block to be freed up
 * Returns 0 on success
 * Returns -1 on failure
 * Here is what this function should accomplish
 * - Return -1 if ptr is NULL
 * - Return -1 if ptr is not 8 byte aligned or if the block is already freed
 * - Mark the block as free
 * - Coalesce if one or both of the immediate neighbours are free
 */
int Mem_Free(void *ptr) {
    if (ptr == NULL || (int)ptr % 8 != 0) {
        return -1;
    }
    blk_hdr* check = first_blk;
    int flag = 0;
    while (check->size_status != 1) {
        if (check == ptr - 4) {
            flag = 1;
        }
        int size = (check->size_status - (check->size_status % 8));
        check = (blk_hdr*)((char*)check + size);
    }
    if (!flag) {
        return -1;
    }
    blk_hdr* curr = (blk_hdr*)(ptr - 4);
    if (!(curr->size_status & 1)) {
        return -1;
    }
    int curr_size = curr->size_status - (curr->size_status % 8);
    blk_hdr* front = (blk_hdr*)((char*)curr + curr_size);
    blk_hdr* temp = NULL;
    blk_hdr* back = NULL;
    if (!(curr->size_status & 2)) {
        temp = (blk_hdr*)((char*)curr - 4);
        back = (blk_hdr*)((char*)curr - temp->size_status);
    }
    if (!(front->size_status & 1) && back != NULL) {
        int front_size = front->size_status - (front->size_status % 8);
        int back_size = (back->size_status - (back->size_status % 8));
        back->size_status = front_size + curr_size + back_size + 2;
        blk_hdr *footer = (blk_hdr*) ((char*)front + front_size - 4);
        footer->size_status = front_size + curr_size + back_size;
        return 0;
    } else if (!(front->size_status & 1)) {
        int front_size = front->size_status - (front->size_status % 8);
        curr->size_status = front_size + curr_size + 2;
        blk_hdr *footer = (blk_hdr*) ((char*)front + front_size - 4);
        footer->size_status = front_size + curr_size;
        return 0;
    } else if (back != NULL) {
        int back_size = (back->size_status - (back->size_status % 8));
        back->size_status = curr_size + back_size + 2;
        blk_hdr *footer = (blk_hdr*) ((char*)curr + curr_size - 4);
        footer->size_status = curr_size + back_size;
        blk_hdr *temp = (blk_hdr*) ((char*)footer + 4);
        temp->size_status = temp->size_status - 2;
        return 0;
    } else {
        curr->size_status = curr->size_status - 1;
        blk_hdr *footer = (blk_hdr*) ((char*)curr + curr_size - 4);
        blk_hdr *temp = (blk_hdr*) ((char*)footer + 4);
        temp->size_status = temp->size_status - 2;
        footer->size_status = curr_size;
        return 0;
    }
    return -1;
}

/*
 * Function used to initialize the memory allocator
 * Not intended to be called more than once by a program
 * Argument - sizeOfRegion: Specifies the size of the chunk which needs to be allocated
 * Returns 0 on success and -1 on failure
 */
int Mem_Init(int sizeOfRegion) {
    int pagesize;
    int padsize;
    int fd;
    int alloc_size;
    void* space_ptr;
    blk_hdr* end_mark;
    static int allocated_once = 0;
    
    if (0 != allocated_once) {
        fprintf(stderr, "Error:mem.c: Mem_Init has allocated"
                "space during a previous call\n");
        return -1;
    }
    if (sizeOfRegion <= 0) {
        fprintf(stderr, "Error:mem.c: Requested block size is not positive\n");
        return -1;
    }
    
    // Get the pagesize
    pagesize = getpagesize();
    
    // Calculate padsize as the padding required to round up sizeOfRegion
    // to a multiple of pagesize
    padsize = sizeOfRegion % pagesize;
    padsize = (pagesize - padsize) % pagesize;
    
    alloc_size = sizeOfRegion + padsize;
    
    // Using mmap to allocate memory
    fd = open("/dev/zero", O_RDWR);
    if (-1 == fd) {
        fprintf(stderr, "Error:mem.c: Cannot open /dev/zero\n");
        return -1;
    }
    space_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                     fd, 0);
    if (MAP_FAILED == space_ptr) {
        fprintf(stderr, "Error:mem.c: mmap cannot allocate space\n");
        allocated_once = 0;
        return -1;
    }
    allocated_once = 1;
    
    // for double word alignement and end mark
    alloc_size -= 8;
    
    // To begin with there is only one big free block
    // initialize heap so that first block meets
    // double word alignment requirement
    first_blk = (blk_hdr*) space_ptr + 1;
    end_mark = (blk_hdr*)((void*)first_blk + alloc_size);
    
    // Setting up the header
    first_blk->size_status = alloc_size;
    
    // Marking the previous block as busy
    first_blk->size_status += 2;
    
    // Setting up the end mark and marking it as busy
    end_mark->size_status = 1;
    
    // Setting up the footer
    blk_hdr *footer = (blk_hdr*) ((char*)first_blk + alloc_size - 4);
    footer->size_status = alloc_size;
    
    return 0;
}

/*
 * Function to be used for debugging
 * Prints out a list of all the blocks along with the following information i
 * for each block
 * No.      : serial number of the block
 * Status   : free/busy
 * Prev     : status of previous block free/busy
 * t_Begin  : address of the first byte in the block (this is where the header starts)
 * t_End    : address of the last byte in the block
 * t_Size   : size of the block (as stored in the block header) (including the header/footer)
 */
void Mem_Dump() {
    int counter;
    char status[5];
    char p_status[5];
    char *t_begin = NULL;
    char *t_end = NULL;
    int t_size;
    
    blk_hdr *current = first_blk;
    counter = 1;
    
    int busy_size = 0;
    int free_size = 0;
    int is_busy = -1;
    
    fprintf(stdout, "************************************Block list***\
            ********************************\n");
    fprintf(stdout, "No.\tStatus\tPrev\tt_Begin\t\tt_End\t\tt_Size\n");
    fprintf(stdout, "-------------------------------------------------\
            --------------------------------\n");
    
    while (current->size_status != 1) {
        t_begin = (char*)current;
        t_size = current->size_status;
        
        if (t_size & 1) {
            // LSB = 1 => busy block
            strcpy(status, "Busy");
            is_busy = 1;
            t_size = t_size - 1;
        } else {
            strcpy(status, "Free");
            is_busy = 0;
        }
        
        if (t_size & 2) {
            strcpy(p_status, "Busy");
            t_size = t_size - 2;
        } else {
            strcpy(p_status, "Free");
        }
        
        if (is_busy)
            busy_size += t_size;
        else
            free_size += t_size;
        
        t_end = t_begin + t_size - 1;
        
        fprintf(stdout, "%d\t%s\t%s\t0x%08lx\t0x%08lx\t%d\n", counter, status,
                p_status, (unsigned long int)t_begin,
                (unsigned long int)t_end, t_size);
        
        current = (blk_hdr*)((char*)current + t_size);
        counter = counter + 1;
    }
    
    fprintf(stdout, "---------------------------------------------------\
            ------------------------------\n");
    fprintf(stdout, "***************************************************\
            ******************************\n");
    fprintf(stdout, "Total busy size = %d\n", busy_size);
    fprintf(stdout, "Total free size = %d\n", free_size);
    fprintf(stdout, "Total size = %d\n", busy_size + free_size);
    fprintf(stdout, "***************************************************\
            ******************************\n");
    fflush(stdout);
    
    return;
}

