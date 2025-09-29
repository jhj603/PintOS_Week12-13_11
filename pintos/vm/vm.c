/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/vaddr.h"
#include "threads/mmu.h"

/* page의 가상 주소(va)를 이용해 해시 값을 생성하는 함수 */
unsigned int page_hash(const struct hash_elem* p_, void* aux UNUSED)
{
	const struct page* p = hash_entry(p_, struct page, hash_elem);

	return hash_bytes(&p->va, sizeof(p->va));
}

/* 두 페이지의 가상 주소(va)를 비교하는 함수 */
/* Pintos의 해시 테이블은 체이닝 방식으로 해시 충돌을 해결함. */
/* 같은 해싱 값을 가진 데이터들은 하나의 연결 리스트로 묶이는데 리스트에 저장될 때 주소 값을 기준으로 정렬 상태를 */
/* 유지하고 싶어서 page_less 함수가 필요한 것. 특정 요소를 더 효율적으로 찾거나, 중복된 요소가 있는지 확인할 수 있어 정렬 유지 */
bool page_less(const struct hash_elem* a_, const struct hash_elem* b_, void* aux UNUSED)
{
	const struct page* a = hash_entry(a_, struct page, hash_elem);
	const struct page* b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
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

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* 새로운 페이지 요청을 받으면 호출되는 함수. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	/* VM_UNINIT과 같을 경우 프로그램을 즉시 종료(패닉) */
	ASSERT (VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/* 1. 새 페이지 구조체 할당 */
		struct page* p = (struct page*)malloc(sizeof(struct page));

		if (NULL == p)
		{
			goto err;
		}

		/* 2. 페이지 타입에 따라 적절한 초기화 함수 설정 */
		/* 전달받은 vm_type에 따라 적절한 초기화 함수를 선택해야 함. */
		/* uninit 페이지의 swap_in 핸들러는 타입에 따라 페이지를 자동으로 초기화하고 주어진 aux 와 함께 init을 호출함 */
		/* 사용자 프로그램 실행 도중, 페이지 폴트가 발생하면 아직 내용이 없는 페이지에 접근하려는 것이므로 */
		/* 폴트 처리 과정에서 uninit_initialize 함수가 호출되고 앞서 설정한 초기화 함수가 실행됨. */
		/* 익명 페이지의 경우 anon_initializer, 파일 기반 페이지의 경우 file_backed_initializer가 사용 */
		bool (*page_initializer)(struct page*, enum vm_type, void* kva) = NULL;

		switch (VM_TYPE(type))
		{
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
		}

		/* TODO: Insert the page into the spt. */
		/* 3. uninit_new를 호출해 uninit 페이지를 생성하고 spt에 삽입. */
		/* 주어진 타입을 최종 페이지 타입으로 하는 초기화되지 않은 페이지를 생성함. */
		/* 페이지 구조체를 얻으면 프로세스의 보조 페이지 테이블에 삽입해야 함. */
		uninit_new(p, upage, init, type, aux, page_initializer);
		p->writable = writable;

		return spt_insert_page(spt, p);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* 보조 페이지 테이블에서 va에 해당하는 struct page를 찾는 함수. 실패하면 NULL 반환 */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	/* TODO: Fill this function. */
	/* spt에서 va에 해당하는 page를 찾기 위한 임시 page */
	struct page p;
	struct hash_elem* e;

	p.va = va;
	e = hash_find(&spt->hash_table, &p.hash_elem);

	/* page를 찾지 못했다면 NULL 반환 */
	if (NULL == e)
	{
		return NULL;
	}

	return hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
/* 보조 페이지 테이블에 struct page를 삽입하는 함수. 해당 가상 주소가 이미 존재하지 않는지 확인해야 함. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	/* hash_insert는 동일한 요소가 없을 때 NULL 반환 */
	if (NULL == hash_insert(&spt->hash_table, &page->hash_elem))
	{
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->hash_table, &page->hash_elem);
	vm_dealloc_page (page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* 유저 풀에서 palloc_get_page로 새로운 물리 페이지를 가져오는 함수. */
/* 성공적으로 페이지를 얻으면 프레임을 할당하고, 멤버를 초기화해 반환함. */
/* 구현 후에는 모든 사용자 공간 페이지 할당(PALLOC_USER)을 이 함수로 해야함. */
/* 페이지 할당 실패 시 스왑 아웃 처리 아직 필요 없음. PANIC("todo")로 표시할 것. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	/* 1. palloc_get_page로 유저 풀에서 페이지 할당 */
	/* MMU가 일단 켜지면, 커널이든 유저 프로세스든 모든 코드의 메모리 접근은 예외 없이 가상 주소를 통해 이뤄지기 때문에 */
	/* 물리 주소를 바로 사용하지 않고 커널 가상 주소를 사용해야 한다. */
	void* frame_kernel_virtual_addr = palloc_get_page(PAL_USER);

	/* 2. 성공적으로 페이지를 얻었을 때 */
	if (NULL != frame_kernel_virtual_addr)
	{
		/* frame 구조체 할당 */
		/* 물리적인 데이터 저장 공간을 관리하기 위한 메타데이터를 저장하는 구조체 */
		frame = (struct frame*)malloc(sizeof(struct frame));

		/* 물리 메모리 할당 실패했을 때 */
		if (NULL == frame)
		{
			/* 할당받은 페이지 해제 */
			palloc_free_page(frame_kernel_virtual_addr);
			return NULL;
		}

		/* 3. 프레임 멤버 초기화 */
		frame->kva = frame_kernel_virtual_addr;
	}
	/* 4. 페이지 할당 실패했을 때 */
	else
	{
		/* 아직 스왑 아웃 처리 필요없기 때문에 PANIC으로 처리 */
		PANIC("todo");
	}

	ASSERT (frame != NULL);

	/* 이 프레임은 아직 어떤 페이지와도 연결되지 않음 */
	frame->page = NULL;

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	/* 폴트가 유효한지 확인. */
	/* 1. 접근한 주소가 NULL이거나, 커널 영역 주소인데 유저가 접근한 경우 */
	if ((NULL == addr) || (is_kernel_vaddr(addr)))
	{
		return false;
	}

	/* 2. spt에서 페이지 찾기. 페이지 폴트가 발생한 가상 주소를 페이지 시작 주소로 변환 */
	void* fault_page_addr = pg_round_down(addr);
	struct supplemental_page_table* spt = &thread_current()->spt;
	struct page* page = spt_find_page(spt, fault_page_addr);
	
	if (NULL == page)
	{
		/* 3. 페이지가 spt에 없는 경우 : 스택 확장 가능성 확인 */
		/* 스택 확장 조건 */
		/* 1) 스택 포인터보다 아래 주소에서 폴트 발생 */
		/* 2) 스택 포인터와 너무 멀리 떨어져 있지 않음 */
		/* 3) 스택 크기가 한계(1MB)를 넘지 않음 */
		return false;
	}

	/* 3. 쓰기 권한 검사 */
	/* 쓰기 금지된 페이지에 쓰려고 한 경우 */
	if (write && !page->writable)
	{
		return false;
	}
	
	/* bogus 폴트라면 페이지에 내용을 로드하고 사용자 프로그램에 제어를 반환해야 함. */
	/* bogus 페이지 폴트에는 세 가지 경우가 있음 */
	/* 1. lazy-loaded */
	/* vm_alloc_page_with_initializer에서 설정한 초기화 함수를 호출해 세그먼트를 lazy load함. */
	if (VM_UNINIT == page_get_type(page))

	/* 2. swap-out된 페이지 */
	/* 3. 쓰기 보호된 페이지(Copy-On-Write 참고) */


	/* 1. 폴트가 발생한 가상 주소의 페이지를 보조 페이지 테이블에서 찾아야 함. */
	/* 해당 주소에 데이터가 있어야 한다면, 파일 시스템, 스왑 슬롯, 또는 0으로 채워진 페이지 등에서 */
	/* 데이터를 가져옴. 만약 복사 시 쓰기(Copy-On-Write)를 구현했다면, 이미 프레임에 데이터가 있을 수 있음. */
	/* 보조 페이지 테이블에 해당 주소에 데이터가 없어야 한다고 되어 있거나, 커널 가상 메모리 내 주소이거나, */
	/* 읽기 전용 페이지에 쓰기 시도라면, 프로세스를 종료시켜야 함. */

	/* 2. 페이지를 저장할 프레임을 확보. 공유를 구현했다면, 이미 프레임에 데이터가 있을 수 있음. */

	/* 3. 데이터를 프레임에 불러옴(파일 시스템, 스왑, 또는 0으로 초기화 등). 공유를 구현했다면, 별도 작업이 필요 없을 수 있음 */

	/* 4. 폴트가 발생한 가상 주소의 페이지 테이블 엔트리를 해당 물리 페이지로 지정. threads/mmu.c의 함수를 사용할 수 있음. */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* va에 해당하는 페이지에 물리 프레임을 할당하는 헬퍼 함수. */
bool
vm_claim_page (void *va) {
	/* TODO: Fill this function */
	/* 1. va가 속한 페이지의 시작 주소 계산해 현재 스레드의 spt에서 spt_find_page 함수로 페이지를 얻어옴. */
	struct page *page = spt_find_page(&thread_current()->spt, pg_round_down(va));

	if (NULL == page)
	{
		return false;
	}

	/* 2. vm_do_claim_page를 호출 */
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* 페이지에 물리 프레임을 할당(claim)하는 함수. */
static bool
vm_do_claim_page (struct page *page) {
	/* 1. 프레임을 얻어옴. */
	struct frame *frame = vm_get_frame ();

	if (NULL == frame)
	{
		return false;
	}

	/* Set links */
	/* 2. 페이지와 프레임 연결 : page와 frame이 서로를 가리키도록 포인터 설정 */
	/* 프레임을 뺏어야 할 때(evict) 어떤 페이지가 이 프레임을 쓰고 있었는지 역추적하는 데 사용 */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* 3. 페이지 테이블 매핑(MMU 설정) : 가장 중요한 단계 */
	/* MMU를 설정해 가상 주소와 물리 주소를 페이지 테이블에 매핑함. */
	/* pml4_set_page() 함수로 가상 주소(page->va)와 물리 프레임의 커널 가상 주소(frame->kva)를 */
	/* 현재 프로세스의 페이지 테이블에 등록해야 CPU가 해당 가상 주소를 물리 주소로 변환할 수 있음. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
	{
		/* 매핑 실패 시, 할당받은 프레임 해제 후 실패 반환 */
		frame->page = NULL;
		page->frame = NULL;

		palloc_free_page(frame->kva);

		free(frame);

		return false;
	}

	/* 성공 여부를 반환함 */
	/* 4. 데이터 로드 : page->operations->swap_in을 통해 페이지 타입에 맞는 실제 함수를 호출 */
	/* VM_UNINIT : uninit_initialize가 호출돼 lazy_load_segment 같은 초기화 함수 실행, 파일에서 데이터를 읽어 프레임 채움 */
	/* VM_ANON : anon_swap_in이 호출돼 스왑 영역에서 데이터를 읽어옴. */
	/* VM_FILE : file_backed_swap_in이 호출됨 */
	if (!swap_in (page, frame->kva))
	{
		/* 데이터 로딩 실패 시, 매핑과 프레임 할당 모두 되돌림 */
		pml4_clear_page(thread_current()->pml4, page->va);

		frame->page = NULL;
		page->frame = NULL;

		palloc_free_page(frame->kva);

		free(frame);

		return false;
	}

	return true;
}

/* 페이지 폴트 및 자원 관리를 위해 각 페이지의 추가 정보를 저장할 보조 페이지 테이블이 필요 */
/* Initialize new supplemental page table */
/* 보조 페이지 테이블을 초기화하는 함수. 사용할 자료구조는 자유롭게 선택 가능(해시 쓸 것) */
/* 새 프로세스가 시작될 때(userprog/process.c의 initd), 프로세스가 fork될 때(userprog/process.c의 __do_fork) 호출됨. */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->hash_table, page_hash, page_less, NULL);

	
}

/* Copy supplemental page table from src to dst */
/* Copy-On-Write를 위해 구현 필요 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	return false;
}

/* spt의 모든 페이지를 파괴하는데 사용되는 헬퍼 함수 */
void spt_destroy_func(struct hash_elem* e, void* aux UNUSED)
{
	struct page* p = hash_entry(e, struct page, hash_elem);
	
	destroy(p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/* hash_destroy를 사용해 해시 테이블의 모든 요소 정리 */
	hash_destroy(&spt->hash_table, spt_destroy_func);
}
