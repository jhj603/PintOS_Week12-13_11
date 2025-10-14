/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

#include "kernel/bitmap.h"
#include "threads/mmu.h"

struct bitmap* swap_table;
struct lock bitmap_lock;

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/* 익명 페이지 서브시스템의 초기화 함수. */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);

	if (NULL == swap_disk)
	{
		PANIC("No swap dist found");
	}

	swap_table = bitmap_create(disk_size(swap_disk) / 8);

	if (NULL == swap_table)
	{
		PANIC("Failed to create swap bitmap");
	}

	lock_init(&bitmap_lock);
}

/* Initialize the file mapping */
/* 1. 페이지의 operations를 anon_ops로 설정 : 페이지의 상태를 VM_UNINIT에서 VM_ANON으로 공식적으로 전환 */
/* 2. anon_page 구조체 초기화 : 스왑 슬롯 인덱스 등 필요한 정보가 있다면 여기서 초기화 */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_idx = BITMAP_ERROR;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	size_t swap_idx = anon_page->swap_idx;
	if (!bitmap_test(swap_table, swap_idx))
	{
		return false;
	}

	if (BITMAP_ERROR == swap_idx)
	{
		PANIC("swap_in idx is crazy");
		return false;
	}

	for (int i = 0; i < 8; ++i)
	{
		disk_read(swap_disk, ((swap_idx * 8) + i), (kva + (i * DISK_SECTOR_SIZE)));
	}

	page->frame->kva = kva;

	lock_acquire(&bitmap_lock);
	bitmap_set(swap_table, swap_idx, false);
	lock_release(&bitmap_lock);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	lock_acquire(&bitmap_lock);
	size_t swap_idx = bitmap_scan_and_flip(swap_table, 0, 1, false);
	lock_release(&bitmap_lock);
	if (BITMAP_ERROR == swap_idx)
	{
		return false;
	}

	anon_page->swap_idx = swap_idx;

	for (int i = 0; i < 8; ++i)
	{
		disk_write(swap_disk, ((swap_idx * 8) + i), (page->frame->kva + (DISK_SECTOR_SIZE * i)));
	}

	page->frame->page = NULL;
	page->frame = NULL;

	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* 스왑 테이블에서 스왑 인덱스 해제 */
	if (BITMAP_ERROR != anon_page->swap_idx)
	{
		lock_acquire(&bitmap_lock);
		bitmap_reset(swap_table, anon_page->swap_idx);
		lock_release(&bitmap_lock);
	}

	/* 프레임이 존재하면 프레임을 리스트에서 제거하고 해제 */
	if (page->frame)
	{
		list_remove(&page->frame->elem);
		page->frame->page = NULL;
		palloc_free_page(page->frame->kva);
		free(page->frame);
		page->frame = NULL;
	}

	pml4_clear_page(thread_current()->pml4, page->va);
}
