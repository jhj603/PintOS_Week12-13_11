#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include <hash.h>	// hash 함수 모듈

enum vm_type {
	/* 초기화되지 않은 페이지 */
	VM_UNINIT = 0,
	/* 파일과 관련 없는, 이른바 익명 페이지 */
	VM_ANON = 1,
	/* 파일과 연관된 페이지 */
	VM_FILE = 2,
	/* 페이지 캐시를 보관하는 페이지(Project 4용) */
	VM_PAGE_CACHE = 3,

	/* 상태를 저장하기 위한 비트 플래그 */

	/* 부가 정보를 저장하기 위한 보조 비트 플래그 마커.
	 * 값이 int 범위를 벗어나지 않는 한 원하는 만큼 추가할 수 있다. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* 이 값을 넘기지 말 것. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* "페이지"를 표현하는 구조체.
 * 일종의 "부모 클래스"이며, uninit_page, file_page, anon_page,
 * 그리고 페이지 캐시(Project 4)라는 네 가지 "자식 클래스"를 거느린다.
 * 미리 정의된 멤버는 삭제하거나 수정하지 말 것. */
struct page {
	const struct page_operations *operations;
	void *va;              /* 사용자 공간의 주소 */
	struct frame *frame;   /* 프레임에 대한 역참조 */

	/* 구현이 들어갈 자리 */
	struct hash_elem hash_elem;
	bool writable; // 이 page가 User 입장에서 쓰기가 가능한가?를 기억하는 플래그
	int mapped_page_count;

	/* 타입별 데이터는 유니온 안에 묶여 있으며,
	 * 각 함수는 현재 활성화된 유니온을 자동으로 판별한다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* "프레임"을 표현하는 구조체 */
struct frame {
	void *kva;
	struct page *page;
	struct list_elem frame_elem;
};

/* 페이지 연산을 위한 함수 테이블.
 * C에서 "인터페이스"를 흉내 내는 한 가지 방법이다.
 * 구조체 멤버에 "메서드" 테이블을 넣어 두고 필요할 때마다 호출한다. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 현재 프로세스의 메모리 공간을 나타내는 구조체.
 * 이 구조체에 대해 특정한 설계를 강요하지 않는다.
 * 어떤 형태로 구현할지는 전적으로 여러분에게 달려 있다. */
struct supplemental_page_table {
	struct hash spt_hash;	// spt 해시 구조체
};

struct lazy_load_arg {
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

#endif  /* VM_VM_H */
