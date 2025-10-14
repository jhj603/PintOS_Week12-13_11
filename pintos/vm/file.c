/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

#include "vm/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"

extern struct lock filesys_lock;

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	/* 페이지 타입을 VM_FILE로 전환 */
	page->operations = &file_ops;

	/* file_page 구조체에 파일 관련 정보 저장 */
	struct file_page *file_page = &page->file;
	/* uninit 페이지였을테니 aux에 file-backed 페이지로 전환될 때 필요한 정보가 저장 */
	/* 1. 임시 저장소(uninit.aux)에서 정보를 꺼냄 */
	struct lazy_load_info* file_info = (struct lazy_load_info*)page->uninit.aux;

	/* 2. 영구 저장소(file_page->file)로 정보를 복사(인계)함 */
	/* reopen 파일 객체 포인터가 file_page->file로 복사됨 */
	file_page->file = file_info->file;
	file_page->ofs = file_info->ofs;
	file_page->read_bytes = file_info->read_bytes;
	file_page->zero_bytes = file_info->zero_bytes;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	file_read_at(file_page->file, kva, file_page->read_bytes, file_page->ofs);

	return true;
}

/* Swap out the page by writeback contents to the file. */
/* 페이지가 메모리에서 스왑 아웃될 때도 삭제와 마찬가지로 내용이 수정됐다면 파일에 다시 써야 함 */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread* cur = thread_current();

	if (pml4_is_dirty(cur->pml4, page->va) && page->writable)
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}

	page->frame->page = NULL;
	page->frame = NULL;

	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/* spt_remove_page()를 수행할 때 사용되는 VM_FILE 타입 페이지의 destroy 함수 */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread* cur = thread_current();

	/* 1. pml4_is_dirty() 함수로 페이지가 수정됐는지(dirty) 확인 */
	if (/*(NULL != page->frame) && */pml4_is_dirty(cur->pml4, page->va) && page->writable)
	{
		/* 2. dirty 상태라면, file_write_at()으로 페이지의 변경된 내용을 디스크 파일에 다시 씀 */
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(cur->pml4, page->va, false);
	}

	hash_delete(&cur->spt.hash_table, &page->hash_elem);

	if (page->frame)
	{
		list_remove(&page->frame->elem);
		page->frame->page = NULL;
		page->frame = NULL;
		free(page->frame);
	}

	pml4_clear_page(cur->pml4, page->va);

	// /* 페이지가 프레임에 있다면 프레임과의 연결을 끊고 페이지 테이블에서 제거 */
	// if (NULL != page->frame)
	// {
	// 	/* 가상 주소(page->va)와 물리 주소 간의 연결을 페이지 테이블에서 제거해야 함. */
	// 	/* 페이지 테이블에 가상 주소 page->va가 이미 해제돼 다른 용도로 사용 중인 물리 주소를 가리키는 유령 매핑이 남게 됨 */
	// 	/* 반드시 페이지 테이블 매핑도 함께 제거해야 함 */
	// 	pml4_clear_page(cur->pml4, page->va);
	// 	vm_free_frame(page->frame);
	// 	page->frame = NULL;
	// }
}

/* Do the mmap */
/* 열린 파일의 offset 바이트부터 len 바이트만큼을 프로세스의 가상 주소 공간내 */
/* addr에 매핑하는 함수. 파일 전체는 addr에서 시작하는 연속적인 가상 페이지들로 매핑됨. */ 
/* 만약 파일의 길이가 PGSIZE의 배수가 아니라면, 마지막으로 매핑된 페이지의 일부 바이트가 */
/* 파일의 끝을 벗어날 수 있음. 페이지 폴트가 발생해 페이지를 메모리에 올릴 때 이 바이트들은 */
/* 0으로 설정, 페이지를 디스크에 다시 쓸 때는 이 바이트들을 폐기해야 함. */ 
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	/* 파일을 새로 열어 파일 디스크립터와 mmap이 독립적으로 관리되게 함 */
	/* 파일 디스크립터의 생명주기와 메모리 매핑의 생명주기를 분리하기 위해 사용 */
	/* 유저 프로그램이 mmap을 호출해 파일을 메모리에 매핑한 후 close(fd)를 통해 파일 디스크립터를 */
	/* 닫을 수 있음. 이런 다음 나중에 addr에 접근해 파일을 읽거나 쓰고 munmap으로 매핑을 해제함 */
	/* 이럴 때, mmap에서 원본 file을 그대로 사용하면 close(fd)가 호출될 때 파일의 open_cnt를 감소시키고 */
	/* 이 값이 0이 되면 파일 시스템이 더 이상 사용되지 않는다고 판단해 디스크에서 삭제해버릴 수 있음 */
	/* 이러면 메모리 매핑이 유효한 파일에 연결되지 않아 심각한 오류가 발생할 수 있음 */
	/* 이를 방지하기 위해 동일한 파일을 가리키는 새로운 파일 구조체 객체를 만들어 가상 메모리 시스템이 */
	/* 소유하게 해야 함. 이를 struct page의 struct uninit이 가지고 있는 struct lazy_load_info의 */
	/* file 포인터로 구현하고 있음. */
	struct thread* cur = thread_current();
	lock_acquire(&filesys_lock);
	struct file* reopen_file = file_reopen(file);
	lock_release(&filesys_lock);
	if (NULL == reopen_file)
	{
		return NULL;
	}

	int total_page_count = length / PGSIZE;

	if (length % PGSIZE)
	{
		total_page_count += 1;
	}

	/* 2. 페이지 할당 및 초기화 */
	void* user_page = addr;

	/* 여는 파일이 length보다 작으면 file_length 사용 */
	/* 만약 페이지에 남는 바이트가 있다면 0으로 채워야 한다. */
	size_t read_bytes;
	size_t zero_bytes;
	
	if (length > file_length(reopen_file))
	{
		read_bytes = file_length(reopen_file);
	}
	else
	{
		read_bytes = length;
	}

	zero_bytes = PGSIZE - (read_bytes % PGSIZE);

	/* 3. 파일 길이 검사 및 전체 매핑 길이 조정 */
	/* 파일 길이를 구하고 파일 끝까지 매핑이 가능한지 확인 */
	/* length가 파일 길이보다 크면 남는 부분은 0으로 채우도록 기록 */
	/* 전체 매핑 크기가 페이지 크기의 배수임을 보장 */
	ASSERT(0 == ((read_bytes + zero_bytes) % PGSIZE));
	/* 페이지 오프셋이 0, 즉 페이지 정렬된 주소임을 보장 */
	ASSERT(0 == pg_ofs(addr));
	/* 파일 내의 오프셋 offset도 페이지 크기의 배수임을 보장 */
	ASSERT(0 == (offset % PGSIZE));
 
	/* 4. 페이지 단위로 loop를 돌며 각 가상 페이지를 uninit으로 등록 */
	/* 5. 모든 페이지가 성공적으로 매핑됐으면 addr 반환 */
	/* 실패 시 중간에 등록한 페이지들을 모두 해제하고 NULL 반환 */
	while ((0 < read_bytes) || (0 < zero_bytes))
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_info* aux = (struct lazy_load_info*)malloc(sizeof(struct lazy_load_info));

		// if (NULL == aux)
		// {
		// 	return NULL;
		// }

		aux->file = reopen_file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		/* 'FILE' 타입의 페이지(파일 기반 페이지)가 최종 타입임을 예약하고 지연 로딩에 필요한 정보를 넘겨줌 */
		if (!vm_alloc_page_with_initializer (
			VM_FILE,			/* 페이지 타입 */
			addr,				/* 페이지를 할당할 가상 주소 */
			writable,			/* 쓰기 가능 여부 */
			lazy_load_segment,	/* 페이지 폴트 발생 시 호출될 함수 (초기화 함수) */
			aux					/* 초기화 함수에 전달할 인자(파일, 오프셋, 읽을 크기 등). */
		))
		{
			//free(aux);
			return NULL;
		}

		struct page* p = spt_find_page(&cur->spt, user_page);
		p->mapped_page_count = total_page_count;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return user_page;
}

/* Do the munmap */
/* munmap이 호출되거나 프로세스가 종료되면 OS는 해당 매핑에 속한 모든 페이지를 검사해야 함 */
/* 페이지의 내용이 메모리에 올라온 후 수정됐다면(dirty 상태), 변경된 내용을 디스크의 원본 파일에 다시 씀(write-back) */
/* 수정되지 않았다면(clean 상태) 그냥 버려짐. 해당 페이지들은 프로세스의 가상 주소 공간에서 제거됨 */
void
do_munmap (void *addr) {
	/* addr를 이용해 매핑된 전체 페이지 범위를 찾아야 함 */
	/* mmap 시 file_reopen으로 생성한 file 객체는 해당 매핑의 모든 페이지가 공유함. */
	/* 이 file 객체가 매핑 전체를 식별하는 "ID 카드" 역할을 함*/
	struct supplemental_page_table* spt = &thread_current()->spt;
	
	/* 1. addr로 spt에서 첫 번째 페이지(page)를 찾음 */
	struct page* page = spt_find_page(spt, addr);

	// /* addr에 매핑된 페이지가 없으면 바로 반환 */
	// if ((NULL == page) || (VM_FILE != page_get_type(page)))
	// {
	// 	return;
	// }

	int count = page->mapped_page_count;

	for (int i = 0; i < count; ++i)
	{
		if (page)
		{
			spt_remove_page(spt, page);
		}

		addr += PGSIZE;
		page = spt_find_page(spt, addr);
	}

	// /* mmap으로 생성된 모든 페이지는 동일한 file 객체를 공유함 */
	// /* 이 file 객체 포인터를 얻어와 기준으로 삼음 */
	// /* 2. page->file.file 포인터를 목표 파일로 지정 */
	// struct file* target_file = page->file.file;
	
	// /* 해시 테이블을 순회하면서 요소를 삭제하는 것은 안전하지 않으므로 */
	// /* 해제할 페이지 목록을 따로 만든 후 별도로 해제하는 것이 안전 */
	// /* lib/kernel/hash.c의 hash_next 함수에 주석으로 반복 중에 해시 테이블을 */
	// /* 수정하면 모든 반복자가 무효화된다고 적혀있음 */
	// struct list pages_to_remove;
	// list_init(&pages_to_remove);

	// /* spt를 순회하며 동일한 파일에 매핑된 모든 페이지를 찾아 해제 */
	// /* 3. spt 전체를 순회하면서 page->file.file이 목표 파일과 동일한 모든 페이지를 찾음 */
	// struct hash_iterator i;
	// hash_first(&i, &spt->hash_table);

	// while (hash_next(&i))
	// {
	// 	struct page* p = hash_entry(hash_cur(&i), struct page, hash_elem);

	// 	if ((VM_FILE == page_get_type(p)) && (target_file == p->file.file))
	// 	{
	// 		list_push_back(&pages_to_remove, &p->hash_elem.list_elem);
	// 	}
	// }

	// /* 4. 찾아낸 각 페이지에 대해 spt_remove_page()를 호출해 spt에서 제거하고 파괴 절차 시작 */
	// struct list_elem* e = list_begin(&pages_to_remove);

	// while (e != list_end(&pages_to_remove))
	// {
	// 	struct page* p = list_entry(e, struct page, hash_elem.list_elem);
	// 	e = list_next(e);
	// 	spt_remove_page(spt, p);
	// }
}