/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
/* 초기화되지 않은 페이지를 위한 구조체 생성 함수 */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
/* 초기화되지 않은 페이지를 위한 구조체 초기화 함수 */
/* 첫 페이지 폴트 시 페이지를 초기화하는 함수. vm_initializer와 aux를 가져와 함수 포인터로 페이지 초기화 함수 호출. */
/* 페이지 폴트 핸들러가 호출 체인을 따라가다 swap_in을 호출할 때 마지막으로 도달하는 함수. */
/* 전체 구현은 제공되지만 설계에 따라 수정이 필요할 수 있음. */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
/* 초기화되지 않은 페이지를 위한 구조체 삭제 함수 */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	/* process.c의 load_segment 함수에서 지연 로딩을 위해 malloc으로 할당한 */
	/* lazy_load_info 구조체를 해제해줘야 함. */
	if (uninit->aux && (VM_FILE == (uninit->type)))
	{
		free(uninit->aux);
		uninit->aux = NULL;
	}
}
