/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/synch.h"
#include "userprog/process.h"

/* 전역 프레임 테이블 */
static struct list frame_table;
/* 프레임 테이블을 보호할 lock. 여러 스레드가 동시 접근이 가능한 공유 자원이므로 보호해야함 */
static struct lock frame_table_lock;

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

	/* 전역 프레임 테이블 초기화 */
	list_init(&frame_table);
	/* 전역 프레임 테이블 보호 락 초기화 */
	lock_init(&frame_table_lock);
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
			default:
				free(p);
				return false;
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

	p.va = pg_round_down(va);
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
	if (list_empty(&frame_table))
	{
		return NULL;
	}

	struct list_elem* e = list_pop_front(&frame_table);

	victim = list_entry(e, struct frame, elem);

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (NULL == victim)
	{
		return NULL;
	}

	struct page* page = victim->page;
	if (NULL == page)
	{
		PANIC("Victim has no page");
	}

	if (!swap_out(page))
	{
		return NULL;
	}

	victim->page = NULL;
	page->frame = NULL;
	
	pml4_clear_page(thread_current()->pml4, page->va);

	return victim;
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
	/* 스택이나 BSS 세그먼트처럼 0으로 채워져야 하는 페이지를 위해 PAL_ZERO 플래그 추가 */
	void* frame_kernel_virtual_addr = palloc_get_page(PAL_USER | PAL_ZERO);

	/* 2. 페이지 할당 실패했을 때 */
	if (NULL == frame_kernel_virtual_addr)
	{
		struct frame* evicted = vm_evict_frame();

		if (NULL == evicted)
		{
			PANIC("Eviction failed : No frame available");
		}

		evicted->page = NULL;

		lock_acquire(&frame_table_lock);
		list_push_back(&frame_table, &evicted->elem);
		lock_release(&frame_table_lock);

		return evicted;
	}

	/* 3. 성공적으로 페이지를 얻었을 때 */
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

	/* 4. 프레임 멤버 초기화 */
	frame->kva = frame_kernel_virtual_addr;
	/* 이 프레임은 아직 어떤 페이지와도 연결되지 않음 */
	frame->page = NULL;

	ASSERT (frame != NULL);

	/* 프레임 테이블에 새로 생성된 프레임 추가 (락으로 보호) */
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->elem);
	lock_release(&frame_table_lock);

	return frame;
}

/* 프레임 해제 헬퍼 함수 */
void vm_free_frame(struct frame* frame)
{
	lock_acquire(&frame_table_lock);
	/* 전역 프레임 테이블에서 프레임 제거 */
	list_remove(&frame->elem);
	lock_release(&frame_table_lock);

	/* 프레임이 할당받은 물리 페이지 해제 */
	palloc_free_page(frame->kva);

	/* 프레임 구조체 해제 */
	free(frame);
}

/* Growing the stack. */
/* 스택 확장 함수 */
static void
vm_stack_growth (void *addr) {
	/* 스택 크기를 늘려서 addr이 더 이상 폴트가 발생하지 않도록 */
	/* 하나 이상의 익명 페이지를 할당합니다. */
	/* 할당 시에는 반드시 addr을 PGSIZE 단위로 내림(round down) */
	/* 처리해야 합니다. */
	/* 1. addr에 쓰기 가능한 익명 페이지를 할당 */
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), true);
	// if ()
	// {
	// 	/* 2. 페이지 할당 성공 시, 즉시 물리 메모리에 올림 */
	// 	/* 이후 페이지 폴트 핸들러가 리턴하면 CPU는 명령 재실행 */
	// 	return vm_claim_page(addr);
	// }

	// return false;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/* 페이지 폴트가 발생했을 때, 그 원인을 분석하고 해결하는 함수 */
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

	/* 2. 페이지가 이미 메모리에 있는 경우 */
	if (!not_present)
	{
		/* Copy-On-Write 등을 여기서 처리. 지금은 실패 처리 */
		return false;
	}

	struct thread* cur = thread_current();
	struct supplemental_page_table* spt = &cur->spt;
	
	/* 폴트가 유저 모드에서 발생했다면 f->rsp가 최신 값 */
	void* rsp = f->rsp;
	/* 커널 모드에서 발생했다면 미리 저장해 둔 user_rsp를 사용 */
	if (!user)
	{
		rsp = cur->user_rsp;
	}

	/* 스택 확장 조건 */
	/* 1) 스택 포인터보다 아래 주소에서 폴트 발생(pintos에서는 8바이트 아래까지만 확인하면 됨) */
	/* 먼저 rsp를 8만큼 뺀 후에 로직을 처리하기 때문에 rsp랑 바로 비교해도 상관 없음. */
	/* 2) 스택 포인터와 너무 멀리 떨어져 있지 않음 */
	/* 3) 스택 크기가 한계(1MB)를 넘지 않음 */
	if ((addr >= (rsp - 8)) && (addr <= (void*)USER_STACK) && ((rsp - 8) >= (void*)(USER_STACK - (1 << 20))))
	{
		vm_stack_growth(addr);
	}

	/* 3. spt에서 페이지 찾기. 페이지 폴트가 발생한 가상 주소를 페이지 시작 주소로 변환 */
	struct page* page = spt_find_page(spt, addr);

	if (NULL == page)
	{
		return false;
	}

	/* 4. 페이지가 spt에 있는 경우 쓰기 권한 검사 */
	/* 쓰기 금지된 페이지에 쓰려고 한 경우 */
	if (write && !page->writable)
	{
		return false;
	}

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
	struct page *page = spt_find_page(&thread_current()->spt, va);

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
	// if (NULL == frame)
	// {
	// 	/* vm_get_frame이 실패하면 swap_out */
	// 	return false;
	// }

	/* Set links */
	/* 2. 페이지와 프레임 연결 : page와 frame이 서로를 가리키도록 포인터 설정 */
	/* 프레임을 뺏어야 할 때(evict) 어떤 페이지가 이 프레임을 쓰고 있었는지 역추적하는 데 사용 */
	frame->page = page;
	page->frame = frame;

	// /* TODO: Insert page table entry to map page's VA to frame's PA. */
	// /* 3. 페이지 테이블 매핑(MMU 설정) : 가장 중요한 단계 */
	// /* MMU를 설정해 가상 주소와 물리 주소를 페이지 테이블에 매핑함. */
	// /* pml4_set_page() 함수로 가상 주소(page->va)와 물리 프레임의 커널 가상 주소(frame->kva)를 */
	// /* 현재 프로세스의 페이지 테이블에 등록해야 CPU가 해당 가상 주소를 물리 주소로 변환할 수 있음. */
	// if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
	// {
	// 	/* 매핑 실패 시, 할당받은 프레임 해제 후 실패 반환 */
	// 	frame->page = NULL;
	// 	page->frame = NULL;

	// 	palloc_free_page(frame->kva);

	// 	free(frame);

	// 	return false;
	// }

	// /* 성공 여부를 반환함 */
	// /* 4. 데이터 로드 : page->operations->swap_in을 통해 페이지 타입에 맞는 실제 함수를 호출 */
	// /* VM_UNINIT : uninit_initialize가 호출돼 lazy_load_segment 같은 초기화 함수 실행, 파일에서 데이터를 읽어 프레임 채움 */
	// /* VM_ANON : anon_swap_in이 호출돼 스왑 영역에서 데이터를 읽어옴. */
	// /* VM_FILE : file_backed_swap_in이 호출됨 */
	// if (!swap_in (page, frame->kva))
	// {
	// 	/* 데이터 로딩 실패 시, 매핑과 프레임 할당 모두 되돌림 */
	// 	pml4_clear_page(thread_current()->pml4, page->va);

	// 	frame->page = NULL;
	// 	page->frame = NULL;

	// 	palloc_free_page(frame->kva);

	// 	free(frame);

	// 	return false;
	// }

	pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

	return swap_in(page, frame->kva);
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
/* src의 spt를 dst로 복사하는 함수. 자식 프로세스가 부모의 실행 컨텍스트를 상속받을 때(fork) 사용 */
/* src의 모든 페이지를 순회하며 dst에 정확히 복사해야 함. uninit 페이지를 할당하고 즉시 claim 해야함. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) 
{
	struct page* parent_page;
	enum vm_type type;
	void* upage;
	bool writable;
	bool success;

	/* 부모의 spt(src)를 순회하기 위해 사용 */
	struct hash_iterator iter;
	hash_first(&iter, &src->hash_table);

	while (hash_next(&iter))
	{
		parent_page = hash_entry(hash_cur(&iter), struct page, hash_elem);
		type = parent_page->operations->type;//page_get_type(parent_page);
		upage = parent_page->va;
		writable = parent_page->writable;

		/* UNINIT 페이지 : 아직 물리 메모리를 차지하지 않으므로 vm_alloc_page_with_initializer를 */
		/* 호출해 동일한 초기화 정보를 가진 새로운 uninit 페이지를 자식에게 만들어 줌 */
		if (VM_UNINIT == type)
		{
			/* 초기화 정보를 복사해 새로운 UNINIT 페이지 생성 */
			vm_initializer* init = parent_page->uninit.init;
			void* aux = parent_page->uninit.aux;

			/* aux 데이터가 있다면 복사본을 만들어 전달(메모리 이중 해제 방지) */
			/* lazy_load_info 같은 aux 데이터는 malloc과 memcpy로 복사해서 전달해야함 */
			// if (aux)
			// {
			// 	void* new_aux = malloc(sizeof(struct lazy_load_info));

			// 	if (NULL == new_aux)
			// 	{
			// 		return false;
			// 	}

			// 	memcpy(new_aux, aux, sizeof(struct lazy_load_info));
			// 	aux = new_aux;
			// }

			/* 최종 타입을 넘겨줘야 함. */
			if (!vm_alloc_page_with_initializer(VM_ANON/*parent_page->uninit.type*/, upage, writable, init, aux))
			{
				return false;
			}

			continue;
		}
		/* ANON/FILE 페이지(메모리에 로드된 페이지) : 이미 물리 프레임에 내용이 있음. */
		else if (VM_FILE == type)
		{
			struct lazy_load_info* aux = (struct lazy_load_info*)malloc(sizeof(struct lazy_load_info));

			// if (NULL == aux)
			// {
			// 	return false;
			// }

			aux->file = parent_page->file.file;
			aux->ofs = parent_page->file.ofs;
			aux->read_bytes = parent_page->file.read_bytes;
			aux->zero_bytes = parent_page->file.zero_bytes;
		
			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, aux))
			{
				//free(aux);
				return false;
			}
			
			struct page* file_page = spt_find_page(dst, upage);

			file_backed_initializer(file_page, type, NULL);

			pml4_set_page(thread_current()->pml4, file_page->va, parent_page->frame->kva, parent_page->writable);
			continue;
		}
		
		/* 즉시 페이지를 할당하고 내용을 복사해야함 */
		/* 자식 프로세스를 위해 새로운 uninit 페이지를 먼저 만듬. 이 페이지의 */
		/* 초기화 함수는 부모의 페이지 내용을 복사하는 역할을 해야함. */
		if (!vm_alloc_page(type, upage, writable))
		{
			return false;
		}

		/* vm_alloc_page로 자식 페이지 할당 후 vm_claim_page로 즉시 물리 메모리에 올림 */
		if (!vm_claim_page(upage))
		{
			return false;
		}

		/* 그 후, memcpy로 부모 프레임(parent_page->frame->kva)의 내용을 자식 프레임으로 복사해야함 */
		struct page* child_page = spt_find_page(dst, upage);
		memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
	}

	return true;
}

/* spt의 모든 페이지를 파괴하는데 사용되는 헬퍼 함수 */
void spt_destroy_func(struct hash_elem* e, void* aux UNUSED)
{
	struct page* p = hash_entry(e, struct page, hash_elem);
	
	vm_dealloc_page(p);
}

/* Free the resource hold by the supplemental page table */
/* spt가 가진 모든 자원을 해제하는 함수. 프로세스가 종료될 때 호출됨 */
/* 페이지 엔트리를 순회하며 각 페이지에 대해 destroy(page)를 호출해야함. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/* hash_clear를 사용해 해시 테이블의 모든 요소 정리 */
	/* hash_destroy는 내부적으로 버킷을 해제하는데 process_exit에서 */
	/* supplemental_page_table_kill 함수를 호출해 버킷을 해제한 다음 */
	/* 호출되는 process_cleanup에서도 똑같은 함수를 호출하기 때문에 이중 해제가 */
	/* 될 수 있음. 따라서 hash_destroy를 쓰기보다 hash_clear를 사용하는 것 */
	hash_clear(&spt->hash_table, spt_destroy_func);
}