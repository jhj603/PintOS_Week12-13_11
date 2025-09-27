#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

/* 초기화되지 않은 페이지. "지연 로딩"을 구현할 때 사용하는 타입이다. */
struct uninit_page {
	/* 페이지의 내용을 초기화하는 함수 */
	vm_initializer *init;
	enum vm_type type;
	void *aux;
	/* struct page를 초기화하고 물리 주소를 가상 주소와 매핑하는 함수 */
	bool (*page_initializer) (struct page *, enum vm_type, void *kva);
};

void uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
