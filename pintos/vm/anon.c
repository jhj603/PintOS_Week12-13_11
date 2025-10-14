/* anon.c: 디스크 이미지가 아닌 페이지(즉, 익명 페이지)의 구현. */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>			// 비트맵 자료구조
#include <stdint.h>         // uint8_t 사용
#include <string.h>         // memset 지원
#include "threads/vaddr.h"	// PGSIZE 지원
#include "threads/mmu.h"	// pml4 관련 함수

#define SECTOR_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)	// 한 페이지당 디스크 섹터 수

static struct bitmap *swap_table;	// swap 슬롯의 사용 여부를 추적하는 bitmap
static struct lock swap_lock;		// swap_table 접근 동기화를 위한 전역 Lock

/* 아래 줄은 수정하지 말 것. */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* 이 구조체는 수정하지 말 것. */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* 익명 페이지를 위한 데이터를 초기화한다. */
void
vm_anon_init (void) {
	// swap_disk을 스왑 전용 디스크 파티션으로 설정
	swap_disk = disk_get(1, 1);
	// 디스크 전체 섹터 수를 페이지 단위로 환산해 swap 슬롯 수만큼 비트맵을 만든다.
	swap_table = bitmap_create(disk_size(swap_disk) / SECTOR_PER_PAGE);
	// swap_table에 대한 동시 접근을 제어할 락을 초기화
	lock_init(&swap_lock);
}

/* 파일 매핑을 초기화한다. */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	// uninit 페이지 구조체에 접근해 초기화 루틴이 호출되었음을 명시
	struct uninit_page *uninit = &page->uninit;
	// 해당 페이지가 익명 페이지 연산 테이블을 사용하도록 등록
	page->operations = &anon_ops;
	// 익명 페이지 메타데이터에 접근
	struct anon_page *anon_page = &page->anon;
	// 아직 어떤 swap 슬롯과도 매핑되지 않았음을 표시
	anon_page->page_no = BITMAP_ERROR;
	// 초기화가 성공했음을 호출자에게 알림
	return true;
}

/* 스왑 디스크에서 내용을 읽어 페이지를 swap in한다. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// 익명 페이지 메타데이터에 접근해 swap 정보를 가져옴
	struct anon_page *anon_page = &page->anon;

	/* BITMAP_ERROR면 아직 스왑아웃된 적이 없는 새 페이지이므로,
	   물리 페이지를 0으로 채운 뒤 바로 성공 처리한다. */
	if (anon_page->page_no == BITMAP_ERROR) {
		memset(kva, 0, PGSIZE);
		return true;
	}

	// 공유 자료구조를 보호하기 위해 swap 락을 획득
	lock_acquire(&swap_lock);
	// 비트맵에서 해당 슬롯이 실제로 사용 중인지 검증
	if (!bitmap_test(swap_table, anon_page->page_no)) {
		lock_release(&swap_lock);
		return false;
	}
	// 페이지를 구성하는 모든 섹터를 순차적으로 읽어 물리 페이지에 채움
	for (size_t i = 0; i < SECTOR_PER_PAGE; i++) {
		disk_read(swap_disk, (anon_page->page_no * SECTOR_PER_PAGE) + i,
		          (uint8_t *)kva + (i * DISK_SECTOR_SIZE));
	}
	// 데이터를 복구했으므로 해당 swap 슬롯을 비어 있다고 갱신
	bitmap_set(swap_table, anon_page->page_no, false);
	// 공유 자원 사용이 끝났으므로 락을 해제
	lock_release(&swap_lock);
	// 페이지가 더 이상 swap에 없음을 표시
	anon_page->page_no = BITMAP_ERROR;
	// swap-in이 성공했음을 호출자에게 반환
	return true;
}

/* 내용을 스왑 디스크에 기록하여 페이지를 스왑 아웃한다. */
static bool
anon_swap_out (struct page *page) {
	// 대상 페이지의 익명 메타데이터와 프레임 정보를 확보
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = page->frame;

	// 프레임이 없으면 기록할 물리가 없으므로 바로 실패 처리
	if (frame == NULL) {
		return false;
	}
	// swap_table을 보호하기 위해 락을 획득
	lock_acquire(&swap_lock);
	// 비어 있는 swap 슬롯을 찾고 바로 사용 중으로 표시
	size_t page_no = bitmap_scan_and_flip(swap_table, 0, 1, false);
	// 빈 슬롯을 찾지 못하면 swap 공간이 부족하므로 실패
	if (page_no == BITMAP_ERROR) {
		lock_release(&swap_lock);
		return false;
	}
	// 물리 페이지 내용을 섹터 단위로 순차 기록해 swap 영역에 저장
	for (size_t i = 0; i < SECTOR_PER_PAGE; i++) {
		disk_write(swap_disk,
		           (page_no * SECTOR_PER_PAGE) + i,
		           frame->kva + (i * DISK_SECTOR_SIZE));
	}
	// 나중에 다시 가져올 수 있도록 사용한 swap 슬롯 번호를 기록
	anon_page->page_no = page_no;
	// 프레임과 페이지의 연결을 끊어 물리 메모리 해제를 준비
	frame->page = NULL;
	// 페이지가 더 이상 프레임을 참조하지 않도록 포인터를 제거
	page->frame = NULL;
	// 현재 프로세스의 페이지 테이블에서 해당 가상 주소 매핑을 제거
	pml4_clear_page(thread_current()->pml4, page->va);
	// 공유 자원 사용이 끝났으므로 락을 해제
	lock_release(&swap_lock);

	return true;
}

/* 익명 페이지를 파괴한다. PAGE는 호출자가 해제한다. */
static void
anon_destroy (struct page *page) {
	// 파괴 대상 페이지의 익명 메타데이터를 확인
	struct anon_page *anon_page = &page->anon;
	// swap 슬롯이 할당돼 있었다면 비트맵에서 사용 표시를 제거
	if (anon_page->page_no != BITMAP_ERROR) {
		bitmap_reset(swap_table, anon_page->page_no);
	}
	// 페이지가 여전히 프레임을 보유하고 있으면 정리 절차를 진행
	if (page->frame) {
		// 프레임 리스트 조작 중 경합을 막기 위해 락을 획득
		lock_acquire(&swap_lock);
		// 글로벌 프레임 리스트에서 이 프레임 노드를 제거
		list_remove(&page->frame->frame_elem);
		// 프레임 리스트 조작이 끝났으므로 락을 해제
		lock_release(&swap_lock);
		// 프레임과 페이지 사이의 상호 참조를 끊음
		page->frame->page = NULL;
		// 프레임 구조체 메모리를 반환
		free(page->frame);
		// 페이지가 더 이상 프레임을 가리키지 않도록 NULL로 초기화
		page->frame = NULL;
	}
}
