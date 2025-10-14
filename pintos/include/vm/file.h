#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	/* aux에서 받아온 reopen 파일 객체를 저장할 포인터 */
	struct file* file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset);
void do_munmap (void *va);
#endif
