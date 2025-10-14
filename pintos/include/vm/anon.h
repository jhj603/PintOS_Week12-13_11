#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

/* 익명 페이지를 설명하는 구조체 */
/* 구현하면서 익명 페이지의 필요한 정보나 상태를 저장할 멤버를 추가할 수 있음. */
struct anon_page {
    size_t swap_idx;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
