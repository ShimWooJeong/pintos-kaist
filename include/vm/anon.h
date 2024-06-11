#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page
{
    /* anon_page가 swap out될 때 해당 페이지가 swap_disk에 저장되는 slot 번호 */
    uint32_t slot_num;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
