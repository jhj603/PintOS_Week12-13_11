/* file.c: 메모리로 지원되는 파일 객체(메모리 매핑 객체)의 구현. */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include <string.h>
#include <stdlib.h>

extern struct lock filesys_lock;

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* 이 구조체는 수정하지 말 것. */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* 파일 VM의 초기화 함수 */
void
vm_file_init (void) {
}

/* 파일 기반(page) 페이지를 초기화한다. */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 파일 기반 페이지가 이후 swap_in/out 및 destroy 단계에서 사용할 handler 테이블을 연결한다. */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	struct lazy_load_arg *aux = (struct lazy_load_arg *) page->uninit.aux;

	/* lazy_load_segment가 전달한 파일 매핑 정보를 내부 구조체에 복사해 둔다.
	   이후 페이지 폴트가 발생해 swap_in이 실행되면 어떤 파일/오프셋을 읽을지 참조한다. */
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;

	return true;
}


/* 파일에서 내용을 읽어 페이지를 스왑 인한다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	// 파일에서 데이터를 읽어와 물리 메모리(kva)에 적재한다.
	lock_acquire(&filesys_lock);
	int read = file_read_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
	lock_release(&filesys_lock);

	// 남은 영역은 0으로 초기화
	memset(page->frame->kva + read, 0, PGSIZE - read);

	return true;
}

/* 내용을 파일에 다시 기록하여 페이지를 스왑 아웃한다. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct frame *frame = page->frame;

	// 페이지가 dirty 상태면 파일에 변경 내용 기록
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		lock_acquire(&filesys_lock);
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
		lock_release(&filesys_lock);

		pml4_set_dirty(thread_current()->pml4, page->va, false); // dirty 비트 초기화
	}

	// frame 연결 해제
	page->frame->page = NULL;
	page->frame = NULL;

	// 페이지 테이블에서 매핑 제거
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* 파일 기반 페이지를 파괴한다. PAGE는 호출자가 해제한다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;

	/* 페이지가 dirty 상태라면 파일에 다시 써서 매핑된 내용이 유실되지 않도록 한다.
	   file_write_at()은 물리 메모리 주소를 요구하므로 frame->kva를 사용한다. */
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		lock_acquire(&filesys_lock);
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
		lock_release(&filesys_lock);

		// dirty 비트 초기화
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}

	// frame이 존재하면 frame 테이블에서 제거 및 정리
	if (page->frame) {
		list_remove(&page->frame->frame_elem);
		page->frame->page = NULL;
		page->frame = NULL;
		free(page->frame);
	}

	/* 페이지 테이블에서 매핑을 제거해, 이후 접근 시 page fault가 발생하도록 한다. */
	pml4_clear_page(thread_current()->pml4, page->va);
}


/* mmap을 수행한다. */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	
	lock_acquire(&filesys_lock);	// 파일 동시 접근 보호
	/* 매핑 요청 기초 검증 및 준비
	   - file_reopen()을 사용해 독립적인 파일 객체를 확보한다.
	   - 매핑 시작 주소(start_addr)는 반복 과정에서 addr가 증가하더라도 반환 시 필요하므로 저장해 둔다. */
	struct file *f = file_reopen(file);
	void *original_addr = addr;	// 매핑 성공 시 파일이 매핑된 가상 주소를 반환하는 데 사용

	// 이 매핑을 위해 사용한 총 페이지 수
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1 : length / PGSIZE;

	/* 파일에서 읽을 총량 결정
	   - 요청 길이보다 실제 파일 크기가 작으면 file_length()만큼만 매핑한다.
	   - 한 페이지에 읽은 뒤 남는 공간은 0으로 채우기 때문에 zero_bytes를 미리 계산한다. */
	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	/* 선행 조건 확인
	   - 읽을 영역과 0으로 채울 영역을 합치면 페이지 크기의 정수배가 되어야 한다.
	   - addr과 offset은 모두 페이지 정렬 상태여야 한다. */
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0);	// upage가 페이지 정렬되어 있는지 확인
	ASSERT(offset % PGSIZE == 0);	// ofs가 페이지 정렬되어 있는지 확인

	/* lazy_load_segment가 사용할 보조 정보(lazy_load_arg)를 준비한다.
		- 읽어야 할 파일 객체, 오프셋, 읽기 바이트 수, 0을 채울 바이트 수를 저장한다. */
	struct lazy_load_arg *aux;
	/* 페이지 단위로 lazy-load 엔트리 생성
	   - 실제 디스크 읽기는 페이지 폴트 시점에 수행되므로 각 페이지에 lazy_load_segment를 등록한다.
	   - 모든 데이터를 소진할 때까지 한 페이지씩 반복한다. */
	while (read_bytes > 0 || zero_bytes > 0) {
		aux = (struct lazy_load_arg *) malloc(sizeof(struct lazy_load_arg));
		if (!aux) {
			goto err;
		}

		/* 이 페이지를 채우는 방법을 계산한다. 파일에서 page_read_bytes를 읽고 최종 page_zero_bytes 바이트를 0으로 채운다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		aux->file = f;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		// vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성한다.
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) {
			// 페이지 확보에 실패하면 지금까지 생성한 매핑을 정리하지 못한 채 NULL을 반환한다.
			return NULL;
		}

		/* 방금 등록한 페이지를 찾아 mmap된 전체 페이지 수를 기록한다.
		   이후 munmap 시 mapped_page_count를 활용해 연속된 페이지를 한 번에 해제한다. */
		struct page *page = spt_find_page(&thread_current()->spt, addr);
		page->mapped_page_count = total_page_count;

		// 읽은 바이트와 0으로 채운 바이트를 추적하고 가상 주소를 증가시킨다. (다음 페이지 처리 준비)
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	lock_release(&filesys_lock);
	return original_addr;	// 성공 시 매핑 시작 주소 반환

err:
	free(aux);	// 실패 시 자원 해제
	lock_release(&filesys_lock);
	return NULL;
}

/* munmap을 수행한다. */
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = spt_find_page(spt, addr);

	if (page == NULL) {
		return;
	}

	int count = page->mapped_page_count;

	/* mmap으로 확보된 각 페이지에 대해 destroy()를 호출하면, file_backed_destroy()가
	   dirty 페이지를 파일에 기록하고 페이지 테이블 매핑을 제거한다. */
	for (int i = 0; i < count; i++) {
		if (page != NULL) {
			destroy(page);
		}

		addr += PGSIZE;
		page = spt_find_page(spt, addr);
	}
}
