/* vm.c: 가상 메모리 객체를 위한 범용 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h" // pml4 함수 모듈

uint64_t page_hash(const struct hash_elem *e, void *aux);
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void hash_page_destroy(struct hash_elem *e, void *aux);

struct list frame_table;	// 현재 할당된 frame 추적, 관리용 리스트
struct lock frame_lock;		// frame table 접근 동기화를 위한 전역 락

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
	list_init(&frame_table);
	lock_init(&frame_lock);
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

	/* upage 자리가 이미 사용 중인지 확인한다.
	   이미 매핑된 페이지가 없는 경우에만 진행한다.  */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM 타입에 맞는 초기화 함수를 찾은 뒤
		 * TODO: uninit_new를 호출해 "uninit" 페이지 구조체를 만든다.
		 * TODO: uninit_new 호출 이후에는 필요한 필드를 직접 수정해야 한다. */
		// 새 page 구조체를 동적 할당 해준다.
		struct page *page = malloc(sizeof(struct page));
		if (!page) {
			return false;
		}
		// 타입 별 초기화 함수 선택
		typedef bool (*initializer_by_type) (struct page *, enum vm_type, void *);
		initializer_by_type initializer = NULL;

		switch (VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
		}

		// uninit page 생성 및 초기화 정보 등록
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;

		/* TODO: 생성한 페이지를 SPT에 삽입한다. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* SPT에서 VA에 해당하는 페이지를 찾아 반환한다. 실패하면 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// 해시 탐색용 임시 페이지
	struct page page;

	// va를 페이지 기준 주소로 정렬
    page.va = pg_round_down(va);

	// 해당 주소에 대응하는 page를 해시 테이블에서 탐색
    struct hash_elem *elem = hash_find(&spt->spt_hash, &page.hash_elem);

	// 찾으면 page 반환, 없으면 NULL
    return elem != NULL ? hash_entry(elem, struct page, hash_elem) : NULL;
}

/* 검증 후 PAGE를 SPT에 삽입한다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	/* TODO: 이 함수를 구현하세요. */
	// 삽입 성공 시 NULL 반환 → true, 실패(중복) 시 false
	return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL;
}

/* spt에서 page를 제거하고 메모리 해제 */
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	// 해시 테이블에서 page 제거
	hash_delete(&spt->spt_hash, &page->hash_elem);

	// page 메모리 해제
	vm_dealloc_page(page);
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
	// frame 구조체 동적 할당
	struct frame *frame = (struct frame *) malloc(sizeof(struct frame));
	ASSERT (frame != NULL);

	// User 영역용 물리 페이지 할당 (0으로 초기화)
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);

	// 할당 실패 시, frame을 하나 선택해서 교체한다.
	if (frame->kva == NULL) {
		frame = vm_evict_frame();
	} else {
		// frame 테이블에 추가
		lock_acquire(&frame_lock);
		list_push_back(&frame_table, &frame->frame_elem);
		lock_release(&frame_lock);
	}

	// 초기에는 매핑된 page가 없음
	frame->page = NULL;

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

/* 성공 시 true를 반환한다. 
   Page Fault 발생 시, 유효한 접근이면 페이지를 메모리에 매핑 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	/* TODO: 페이지 폴트가 유효한지 검증한다. */
	/* TODO: 여기에 코드를 작성하세요 */
	// 유효하지 않은 주소(커널 영역 또는 NULL) 접근은 처리 불가
	if (addr == NULL || is_kernel_vaddr(addr)) {
		return false;
	}
	
	// 접근한 페이지가 존재하지 않아 발생한 Page Fault인 경우
	if (not_present) {
		// SPT에서 해당 주소에 대한 페이지 정보 탐색
		struct page *page = spt_find_page(spt, addr);
		if (page == NULL) {
			return false;
		}

		// 쓰기 권한이 없는 페이지에 대한 쓰기 접근은 처리 불가
		if (write && !page->writable) {
			return false;
		}

		return vm_do_claim_page(page);
	}

	return false;
}

/* 페이지를 해제한다.
 * 이 함수는 수정하지 말 것. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 확보(claim)한다.
   주어진 VA에 해당하는 page를 찾아 물리 메모리에 매핑 */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL) {
		return false;
	}

	// page를 실제 물리 메모리에 매핑
	return vm_do_claim_page (page);
}

/* PAGE를 확보하고 MMU를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	// 새 frame을 할당해준다.
	struct frame *frame = vm_get_frame ();

	/* 연결 설정 */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 물리 주소와 매핑하도록 페이지 테이블 엔트리를 삽입한다. */
	// 가상 주소(page->va)와 물리 주소(frame->kva)를 매핑
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
		return false;
	}

	// 디스크나 swap 영역에서 실제 데이터 로드
	return swap_in (page, frame->kva);
}

/* 새로운 보조 페이지 테이블을 hash table로 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* page_hash : 해시 함수, page_less : 비교 함수 사용 */
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* 보조 페이지 테이블을 src(복제하려는 보조 페이지 테이블)에서 dst(내용을 받아야 하는 테이블)로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	
	// src SPT hash table을 순회할 반복자 초기화
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);

	while (hash_next(&i)) {
		// 현재 항목의 page 정보 가져오기
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type src_type = src_page->operations->type;

		if (src_type == VM_UNINIT) {
			// lazy load용 uninit 페이지는 메타데이터만 복사한다.
			vm_alloc_page_with_initializer(
				src_page->uninit.type,
				src_page->va,
				src_page->writable,
				src_page->uninit.init,
				src_page->uninit.aux
			);
		} else {
			// 이미 초기화된 페이지는 새로 할당하고 데이터를 복사한다.
			if (vm_alloc_page(src_type, src_page->va, src_page->writable) && vm_claim_page(src_page->va)) {
				struct page *dst_page = spt_find_page(dst, src_page->va);
				memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
			}
		}
	}
	return true;
}

/* 보조 페이지 테이블이 보유한 자원을 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 가지고 있는 모든 보조 페이지 테이블을 파괴하고,
	 * TODO: 수정된 내용을 저장 장치에 모두 기록(writeback)한다. */
	// 해시 테이블 전체 삭제, 각 항목은 hash_page_destroy로 정리한다.
	hash_clear(&spt->spt_hash, hash_page_destroy);
}

/* page의 va를 기준으로 해시 값을 계산 */
uint64_t 
page_hash(const struct hash_elem *p_, void *aux UNUSED) {
    // 해시 요소에서 struct page 포인터 추출
    const struct page *p = hash_entry(p_, struct page, hash_elem);

    // va 값을 바이트 단위로 해싱하여 해시값 반환
    return hash_bytes(&p->va, sizeof p->va);
}

/* 두 page의 va를 비교하여 정렬 순서를 결정 (va 기준 오름차순) */ 
bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    // 해시 요소에서 struct page 포인터 추출
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    // va 기준으로 작은 쪽이 먼저 오도록 비교
    return a->va < b->va;
}

/* hash table에서 페이지 제거 시 호출되는 정리 함수 */
void
hash_page_destroy(struct hash_elem *elem, void *aux) {
	// hash_elem에서 page 구조체 포인터를 호출한다.
	struct page *page = hash_entry(elem, struct page , hash_elem);

	// page 내부 자원을 해제한다(frame, swap 등).
	destroy(page);

	// page 구조체 자체 메모리를 해제한다.
	free(page);
}