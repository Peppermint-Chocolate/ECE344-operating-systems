#include "vms.h"
#include <errno.h>

#include "mmu.h"
#include "pages.h"

#include <stdio.h>
#include <string.h>

/* run each test case:  ./build/tests/copy-1  */
/* meson test --print-errorlogs -C build */

static int cnt_reference[MAX_PAGES] = {0}; /* This can be more space efficient */

static void print_pte_entry(uint64_t* entry) {
    const char* dash = "-";
    const char* custom = dash;
    const char* write = dash;
    const char* read = dash;
    const char* valid = dash;
    if (vms_pte_custom(entry)) {
        custom = "C";
    }
    if (vms_pte_write(entry)) {
        write = "W";
    }
    if (vms_pte_read(entry)) {
        read = "R";
    }
    if (vms_pte_valid(entry)) {
        valid = "V";
    }

    printf("PPN: 0x%lX Flags: %s%s%s%s\n",
        vms_pte_get_ppn(entry),
        custom, write, read, valid);
}

void page_fault_handler(void* virtual_address, int level, void* page_table) {

    // printf("page handler called \n"); 
    // if can be write, change c = 1 w = 0 
    // check if c == 1: if so change w = 1 and create copy 
    uint64_t* p_entry_l0 = vms_page_table_pte_entry(page_table, virtual_address, level); 

    if (! vms_pte_custom(p_entry_l0)) {
        return; 
    }

    // create new copy and set write bit 

    // if cnt_reference == 1, set custom to 0 and write to 1 
    // get page index 
    uint64_t p_ppn_p0 = vms_pte_get_ppn(p_entry_l0); 
    void* parent_p0 = vms_ppn_to_page(p_ppn_p0); 
    
    int index =  vms_get_page_index(parent_p0);

    // printf("cnt_reference[%d] in page fault: %d \n", index, cnt_reference[index]); 

    if (cnt_reference[index] == 0) { 
        // printf("page handler return \n"); 
        vms_pte_write_set(p_entry_l0);
        vms_pte_custom_clear(p_entry_l0); 
        return; 
    } 

    void* child_p0 = vms_new_page();
    cnt_reference[vms_get_page_index(child_p0)] = 1; 

    vms_pte_set_ppn(p_entry_l0, vms_page_to_ppn(child_p0));
    vms_pte_write_set(p_entry_l0);
    vms_pte_custom_clear(p_entry_l0); 

    // printf("page handler memcpy \n"); 
    memcpy(child_p0, parent_p0, PAGE_SIZE);
    cnt_reference[index]--; 
}


void* vms_fork_copy() {
    
    void* parent_l2 = vms_get_root_page_table(); 
    void* child_l2 = vms_new_page(); 

    // for i in pages: 
    // use vms_pte_set_ppn to update pointer to new address 
    for (int i = 0; i < NUM_PTE_ENTRIES; i++){
        uint64_t* p_entry_l2 = vms_page_table_pte_entry_from_index(parent_l2, i);
        uint64_t* c_entry_l2 = vms_page_table_pte_entry_from_index(child_l2, i);

        if (!vms_pte_valid(p_entry_l2)) {continue;}

        vms_pte_valid_set(c_entry_l2); 

        void* child_l1 = vms_new_page();
        vms_pte_set_ppn(c_entry_l2, vms_page_to_ppn(child_l1)); 

        uint64_t p_ppn_l1 = vms_pte_get_ppn(p_entry_l2); 
        void* parent_l1 = vms_ppn_to_page(p_ppn_l1);

        for (int i = 0; i < NUM_PTE_ENTRIES; i++){
            uint64_t* p_entry_l1 = vms_page_table_pte_entry_from_index(parent_l1, i); 
            uint64_t* c_entry_l1 = vms_page_table_pte_entry_from_index(child_l1, i);

            if (!vms_pte_valid(p_entry_l1)) {continue;}

            vms_pte_valid_set(c_entry_l1); 

            void* child_l0 = vms_new_page();
            vms_pte_set_ppn(c_entry_l1, vms_page_to_ppn(child_l0)); 

            uint64_t p_ppn_l0 = vms_pte_get_ppn(p_entry_l1); 
            void* parent_l0 = vms_ppn_to_page(p_ppn_l0);

            for (int i = 0; i < NUM_PTE_ENTRIES; i++){
                uint64_t* p_entry_l0 = vms_page_table_pte_entry_from_index(parent_l0, i); 
                uint64_t* c_entry_l0 = vms_page_table_pte_entry_from_index(child_l0, i);

                if (!vms_pte_valid(p_entry_l0)) {continue;} 

                vms_pte_valid_set(c_entry_l0); 

                if (vms_pte_write(p_entry_l0)) {
                    vms_pte_write_set(c_entry_l0); 
                } else {
                    vms_pte_write_clear(c_entry_l0); 
                }

                if (vms_pte_read(p_entry_l0)) {
                    vms_pte_read_set(c_entry_l0); 
                } else {
                    vms_pte_read_clear(c_entry_l0); 
                }

                vms_pte_custom_clear(c_entry_l0); 

                void* child_p0 = vms_new_page();

                uint64_t p_ppn_p0 = vms_pte_get_ppn(p_entry_l0); 
                void* parent_p0 = vms_ppn_to_page(p_ppn_p0); 

                memcpy(child_p0, parent_p0, PAGE_SIZE); 

                vms_pte_set_ppn(c_entry_l0, vms_page_to_ppn(child_p0)); 

            }

        } 

    }

    return child_l2;
}

void* vms_fork_copy_on_write() {
    void* parent_l2 = vms_get_root_page_table(); 
    void* child_l2 = vms_new_page(); 

    // for i in pages: 
    // use vms_pte_set_ppn to update pointer to new address 
    for (int i = 0; i < NUM_PTE_ENTRIES; i++){
        uint64_t* p_entry_l2 = vms_page_table_pte_entry_from_index(parent_l2, i);
        uint64_t* c_entry_l2 = vms_page_table_pte_entry_from_index(child_l2, i);

        if (!vms_pte_valid(p_entry_l2)) {continue;}

        vms_pte_valid_set(c_entry_l2); 

        void* child_l1 = vms_new_page();
        vms_pte_set_ppn(c_entry_l2, vms_page_to_ppn(child_l1)); 

        uint64_t p_ppn_l1 = vms_pte_get_ppn(p_entry_l2); 
        void* parent_l1 = vms_ppn_to_page(p_ppn_l1);

        for (int j = 0; j < NUM_PTE_ENTRIES; j++){
            uint64_t* p_entry_l1 = vms_page_table_pte_entry_from_index(parent_l1, j); 
            uint64_t* c_entry_l1 = vms_page_table_pte_entry_from_index(child_l1, j);

            if (!vms_pte_valid(p_entry_l1)) {continue;}

            // check cow, check parent write permission
            // review copy on write 

            vms_pte_valid_set(c_entry_l1); 

            void* child_l0 = vms_new_page();
            vms_pte_set_ppn(c_entry_l1, vms_page_to_ppn(child_l0)); 

            uint64_t p_ppn_l0 = vms_pte_get_ppn(p_entry_l1); 
            void* parent_l0 = vms_ppn_to_page(p_ppn_l0);

            for (int k = 0; k < NUM_PTE_ENTRIES; k++){
                uint64_t* p_entry_l0 = vms_page_table_pte_entry_from_index(parent_l0, k); 
                uint64_t* c_entry_l0 = vms_page_table_pte_entry_from_index(child_l0, k);

                if (!vms_pte_valid(p_entry_l0)) {continue;} 

                vms_pte_valid_set(c_entry_l0); 

                if (vms_pte_read(p_entry_l0)) {
                    vms_pte_read_set(c_entry_l0); 
                } else {
                    vms_pte_read_clear(c_entry_l0); 
                }
                // printf("cowing\n");
                // print_pte_entry(p_entry_l0);
                // if write || custom, set parent & child write to 0 and custom to 1 
                // cnt_reference ++ 
                uint64_t p_ppn_p0 = vms_pte_get_ppn(p_entry_l0); 
                void* parent_p0 = vms_ppn_to_page(p_ppn_p0); 
                vms_pte_set_ppn(c_entry_l0, vms_page_to_ppn(parent_p0));

                if (vms_pte_write(p_entry_l0) || vms_pte_custom(p_entry_l0)) {

                    vms_pte_write_clear(p_entry_l0); 
                    vms_pte_custom_set(p_entry_l0); 
                    vms_pte_write_clear(c_entry_l0); 
                    vms_pte_custom_set(c_entry_l0);

                    int index =  vms_get_page_index(parent_p0);
                    cnt_reference[index]++; 
                    // printf("cnt_reference[%d] in cow: %d \n", index, cnt_reference[index]); 
                }

            }

        } 

    }

    return child_l2;
}
