/* vm.c: 가상 메모리 객체를 위한 범용 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화한다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* Project 4용 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* 위쪽 줄은 수정하지 말 것. */
	/* TODO: 여기에 코드를 작성하세요. */
}

/* 페이지의 유형을 반환한다. 초기화된 뒤의 페이지 타입을 알고 싶을 때 유용하다.
 * 이 함수는 이미 완전히 구현되어 있다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 헬퍼 함수 선언 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화자를 가지는 지연 초기화 페이지 객체를 생성한다.
 * 페이지를 만들고 싶다면 직접 만들지 말고 이 함수나 `vm_alloc_page`를 통해 생성해야 한다. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* upage 자리가 이미 사용 중인지 확인한다. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM 타입에 맞는 초기화 함수를 찾은 뒤
		 * TODO: uninit_new를 호출해 "uninit" 페이지 구조체를 만든다.
		 * TODO: uninit_new 호출 이후에는 필요한 필드를 직접 수정해야 한다. */

		/* TODO: 생성한 페이지를 SPT에 삽입한다. */
	}
err:
	return false;
}

/* SPT에서 VA에 해당하는 페이지를 찾아 반환한다. 실패하면 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 구현하세요. */

	return page;
}

/* 검증 후 PAGE를 SPT에 삽입한다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: 이 함수를 구현하세요. */

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* 추방 대상이 될 프레임을 구한다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: 어떤 페이지를 추방할지는 전적으로 구현자에게 달려 있다. */

	return victim;
}

/* 한 페이지를 추방하고 해당 프레임을 반환한다.
 * 실패하면 NULL을 반환한다. */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: 선택된 프레임을 스왑 아웃한 뒤 그 프레임을 반환한다. */

	return NULL;
}

/* palloc()을 통해 프레임을 얻는다. 여유 페이지가 없다면 추방을 수행해 프레임을 돌려준다.
 * 항상 유효한 주소를 반환하며, 사용자 풀 메모리가 가득 찬 경우에도 프레임을 추방해
 * 사용 가능한 공간을 확보한다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: 이 함수를 구현하세요. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택을 확장한다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* 쓰기 보호된 페이지에서 발생한 페이지 폴트를 처리한다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공 시 true를 반환한다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: 페이지 폴트가 유효한지 검증한다. */
	/* TODO: 여기에 코드를 작성하세요 */

	return vm_do_claim_page (page);
}

/* 페이지를 해제한다.
 * 이 함수는 수정하지 말 것. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 확보(claim)한다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 구현하세요 */

	return vm_do_claim_page (page);
}

/* PAGE를 확보하고 MMU를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 물리 주소와 매핑하도록 페이지 테이블 엔트리를 삽입한다. */

	return swap_in (page, frame->kva);
}

/* 새로운 보조 페이지 테이블을 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
}

/* 보조 페이지 테이블을 src에서 dst로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* 보조 페이지 테이블이 보유한 자원을 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 가지고 있는 모든 보조 페이지 테이블을 파괴하고,
	 * TODO: 수정된 내용을 저장 장치에 모두 기록(writeback)한다. */
}
