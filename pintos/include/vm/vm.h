#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

#include "lib/kernel/hash.h"

enum vm_type {
	/* page not initialized */
	/* lazy loading을 지원하기 위함. 모든 페이지는 처음에 VM_UNINIT 타입으로 생성됨. */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	/* 익명 페이지 */
	VM_ANON = 1,
	/* page that realated to the file */
	/* 파일 기반 페이지 */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
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

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {
	const struct page_operations *operations;
	/* Address in terms of user space */ 
	/* 사용자 공간의 주소. 해시 테이블에서 페이지를 찾기 위한 key로 사용 */
	void *va;              
	struct frame *frame;   /* Back reference for frame */ /* 프레임에 대한 역참조 */

	/* Your implementation */
	/* 쓰기 가능 여부 */
	bool writable;
	/* 물리 메모리에 로드됐는지 여부 */
	bool is_loaded;
	/* 해시 테이블의 노드가 되기 위한 필수 멤버 */
	struct hash_elem hash_elem;

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
/* 물리 메모리를 나타냄. 프레임 관리 인터페이스를 구현하며 멤버를 추가할 수 있음. */
struct frame {
	void *kva;	/* 커널 가상 주소 */
	struct page *page;	/* 페이지 구조체 */

	/* 전역 프레임 테이블(리스트)에 포함되기 위한 요소 */
	struct list_elem elem;
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
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

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
/* 하드웨어 페이지 테이블이 제공하지 못하는 추가 정보를 관리하기 위해 만드는 보조 자료구조 */
/* 하드웨어 페이지 테이블은 가상 주소와 물리 주소 매핑 정보, 읽기/쓰기 권한 등 몇 가지 플래그만 저장 */
/* 가상 메모리를 제대로 구현하기 위해 다음과 같은 정보들이 필요 */
/* 1. 페이지의 현재 위치 : 현재 물리 메모리에 있는지, 하드의 스왑 영역에 있는지, 실행 파일 그 자체인지 알아야 함 */
/* 2. 지연 로딩 정보 : "이 가상 주소는 000 파일의 xxx 오프셋에서 yyy 바이트만큼 읽어와야 함"과 같은 예약 정보를 저장해야 함 */
/* 3. 페이지 종류 : 일반적인 데이터가 담기는 익명 페이지인지, 파일과 연결된 파일 기반 페이지인지 구분해야 함 */
struct supplemental_page_table {
	/* 해시 테이블 */
	struct hash hash_table;
	/* 실행 파일 로드 시, 코드와 데이터를 물리 메모리에 바로 올리지 않고 */
	/* "이 가상 주소는 이 파일의 이 부분에 해당한다"는 정보만 SPT에 기록함. 이걸 저장할 멤버 필요 */
	/* SPT에 정보만 있고 물리 메모리에 없는 주소에 접근하면 페이지 폴트 발생 */
	/* 폴트를 처리해서 실제 데이터를 파일에서 읽어와 물리 메모리에 올려주는 핸들러를 만들어야 함. */
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
