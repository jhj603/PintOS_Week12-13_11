#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/palloc.h"

#include "threads/init.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/file.h"

typedef void syscall_handler_func(struct intr_frame* f);

static syscall_handler_func* syscall_handlers[SYS_END];

extern struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void sys_halt(void);

void sys_exit(int status);
int sys_open(const char* file);
void sys_close(int fd);
int sys_filesize(int fd);
int sys_wait(tid_t tid);
int sys_exec(const char* file);
int sys_tell(int fd);
bool sys_remove(const char* file);

bool sys_create(const char* file, unsigned int initial_size);
tid_t sys_fork(const char* thread_name, struct intr_frame* f);
void sys_seek(int fd, unsigned int pos);
int sys_dup2(int oldfd, int newfd);

int sys_write(int fd, const void* buffer, unsigned int size);
int sys_read(int fd, void* buffer, unsigned int size);

uint64_t sys_mmap(void* addr, size_t len, int writable, int fd, off_t ofs);
void sys_munmap(void* addr);

void check_address(void* addr);
void check_valid_buffer(const void* buffer, unsigned int size);
void check_valid_string(const char* str);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	// syscall_handlers[SYS_HALT] = sys_halt;
	// syscall_handlers[SYS_EXIT] = sys_exit;
	// syscall_handlers[SYS_FORK] = sys_fork;
	// syscall_handlers[SYS_EXEC] = sys_exec;
	// syscall_handlers[SYS_WAIT] = sys_wait;
	// syscall_handlers[SYS_CREATE] = sys_create;
	// syscall_handlers[SYS_REMOVE] = sys_remove;
	// syscall_handlers[SYS_OPEN] = sys_open;
	// syscall_handlers[SYS_FILESIZE] = sys_filesize;
	// syscall_handlers[SYS_READ] = sys_read;
	// syscall_handlers[SYS_WRITE] = sys_write;
	// syscall_handlers[SYS_SEEK] = sys_seek;
	// syscall_handlers[SYS_TELL] = sys_tell;
	// syscall_handlers[SYS_CLOSE] = sys_close;

	// /* 메모리 맵 파일을 구현하기 위한 시스템 콜 */
	// syscall_handlers[SYS_MMAP] = NULL;//sys_mmap;
	// syscall_handlers[SYS_MUNMAP] = NULL;//sys_munmap;

	// syscall_handlers[SYS_CHDIR] = NULL;
	// syscall_handlers[SYS_MKDIR] = NULL;
	// syscall_handlers[SYS_READDIR] = NULL;
	// syscall_handlers[SYS_ISDIR] = NULL;
	// syscall_handlers[SYS_INUMBER] = NULL;
	// syscall_handlers[SYS_SYMLINK] = NULL;
	
	// syscall_handlers[SYS_DUP2] = sys_dup2;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	/* 시스템 콜이 호출될 때, 유저 rsp를 저장 */
#ifdef VM
	thread_current()->user_rsp = f->rsp;
#endif

	uint64_t syscall_num = f->R.rax;

	switch (syscall_num)
	{
		case SYS_HALT:
			sys_halt();
			break;
		case SYS_EXIT:
			sys_exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = sys_fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = sys_exec(f->R.rdi);
			break;
        case SYS_WAIT:
            f->R.rax = process_wait(f->R.rdi);
            break;
        case SYS_CREATE:
            f->R.rax = sys_create(f->R.rdi, f->R.rsi);
            break;
        case SYS_REMOVE:
            f->R.rax = sys_remove(f->R.rdi);
            break;
        case SYS_OPEN:
            f->R.rax = sys_open(f->R.rdi);
            break;
        case SYS_FILESIZE:
            f->R.rax = sys_filesize(f->R.rdi);
            break;
        case SYS_READ:
            f->R.rax = sys_read(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_WRITE:
            f->R.rax = sys_write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_SEEK:
            sys_seek(f->R.rdi, f->R.rsi);
            break;
        case SYS_TELL:
            f->R.rax = sys_tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            sys_close(f->R.rdi);
            break;
		case SYS_DUP2:
			f->R.rax = sys_dup2(f->R.rdi, f->R.rsi);
			break;
#ifdef VM
		case SYS_MMAP:
			f->R.rax = sys_mmap((void*)f->R.rdi, (size_t)f->R.rsi, (int)f->R.rdx, (int)f->R.r10, (off_t)f->R.r8);
			break;
		case SYS_MUNMAP:
			sys_munmap((void*)f->R.rdi);
			break;
#endif
		default:
            sys_exit(-1);
	}

	// if ((SYS_HALT <= syscall_num) && (SYS_END > syscall_num))
	// {
	// 	syscall_handler_func* handler = syscall_handlers[syscall_num];

	// 	if (NULL != handler)
	// 	{
	// 		handler(f);

	// 		return;
	// 	}
	// }

	// thread_current()->exit_status = -1;

	// thread_exit();
}

#ifdef VM
/* 기존 pml4_get_page로 페이지 테이블에 해당 주소의 매핑이 있는지 확인했음 */
/* 스택 확장이나 지연 로딩으로 인해 아직 물리 메모리에 연결되지 않은 유효한 가상 주소가 존재함 */
/* 이 주소에 접근하면 pml4_get_page는 NULL을 반환해 실패로 처리됨 */
/* 페이지 테이블 대신 spt를 확인하도록 변경해야 함. 페이지의 존재 여부와 상태 등을 모두 관리하기 때문 */
/* 검사를 통과하면 해당 주소는 잠재적으로 유효한 주소로 간주할 수 있는 것 */
void check_address(void* addr)
{
	if ((NULL == addr) || is_kernel_vaddr(addr)/* || (NULL == spt_find_page(&thread_current()->spt, pg_round_down(addr)))*/)
	{
		sys_exit(-1);
	}
}

void check_valid_buffer(const void* buffer, unsigned int size)
{
	check_address((void*)buffer);

	if (0 < size)
	{
		check_address((void*)(buffer + size - 1));
	}
}
#else
void check_address(void* addr)
{
	if ((NULL == addr) || is_kernel_vaddr(addr) || (NULL == pml4_get_page(thread_current()->pml4, addr)))
	{
		sys_exit(-1);
	}
}

void check_valid_buffer(const void* buffer, unsigned int size)
{
	for (void* p = pg_round_down(buffer); p < (buffer + size); p += PGSIZE)
	{
		check_address(p);
	}
}
#endif

void check_valid_string(const char* str)
{
	check_address(str);

	while ('\0' != *str)
	{
		if (pg_ofs(str) == (PGSIZE - 1))
		{
			check_address(str + 1);
		}

		++str;
	}
}

void sys_halt(void)
{
	power_off();
}

void sys_exit(int status)
{
	struct thread* cur = thread_current();

	cur->exit_status = status;

	printf("%s: exit(%d)\n", cur->name, cur->exit_status);

	thread_exit();
}

int sys_open(const char* file)
{
	check_valid_string(file);

	struct file* open_file = filesys_open(file);

	if (NULL == open_file)
	{
		return -1;
	}

	int fd = process_add_file(open_file);

	if (-1 == fd)
	{
		file_close(open_file);
	}

	return fd;
}

void sys_close(int fd)
{
	struct thread* cur = thread_current();
	struct file* file = process_get_file(fd);

	if (NULL == file)
	{
		return;
	}

	process_close_file(fd);

	if ((STDIN == file) || (STDOUT == file))
	{
		file = 0;
		return;
	}

	if (0 == file->dup_count)
	{
		file_close(file);
	}
	else
	{
		--file->dup_count;
	}
}

int sys_filesize(int fd)
{
	struct file* file = process_get_file(fd);

	if (NULL == file)
	{
		return -1;
	}

	return file_length(file);
}

int sys_wait(tid_t tid)
{
	return process_wait(tid);
}

int sys_exec(const char* file)
{
	check_valid_string(file);

	char* fn_copy = palloc_get_page(PAL_ZERO);
	
	if (NULL == fn_copy)
	{ 
		return -1;
	}

	strlcpy(fn_copy, file, strlen(file) + 1);

	if (-1 == process_exec(fn_copy))
	{
		return -1;
	}

	return 0;
}

int sys_tell(int fd)
{
	struct file* target_file = process_get_file(fd);

	if ((NULL == target_file) || ((STDIN <= target_file) && (STDOUT >= target_file)))
	{
		return -1;
	}

	return file_tell(target_file);
}

bool sys_remove(const char* file)
{
	check_valid_string(file);

	return filesys_remove(file);
}

bool sys_create(const char* file, unsigned int initial_size)
{
	check_valid_string(file);

	return filesys_create(file, initial_size);
}

tid_t sys_fork(const char* thread_name, struct intr_frame* f)
{
	check_valid_string(thread_name);

	return process_fork(thread_name, f);
}

void sys_seek(int fd, unsigned int pos)
{
	struct file* target_file = process_get_file(fd);

	if ((NULL == target_file) || ((STDIN <= target_file) && (STDOUT >= target_file)))
	{
		return;
	}

	file_seek(target_file, pos);
}

int sys_dup2(int oldfd, int newfd)
{
	if ((0 > oldfd) || (0 > newfd))
	{
		return -1;
	}

	if (oldfd == newfd)
	{
		return newfd;
	}

	struct file* oldfile = process_get_file(oldfd);

	if (NULL == oldfile)
	{
		return -1;
	}

	struct file* newfile = process_get_file(newfd);

	if (oldfile == newfile)
	{
		return newfd;
	}

	sys_close(newfd);

	newfd = process_insert_file(newfd, oldfile);

	return newfd;
}

int sys_write(int fd, const void* buffer, unsigned int size)
{
	check_valid_buffer(buffer, size);

	struct thread* cur = thread_current();
	int bytes_write = -1;

	struct file* target_file = process_get_file(fd);

	if ((STDIN == target_file) || (NULL == target_file))
	{ 
		return -1;
	}

	if (STDOUT == target_file)
	{
		putbuf(buffer, size);
 
		return size;
	}

	lock_acquire(&filesys_lock);
	bytes_write = file_write(target_file, buffer, size);
	lock_release(&filesys_lock);

	return bytes_write;
}

int sys_read(int fd, void* buffer, unsigned int size)
{
	check_valid_buffer(buffer, size);

	struct file* file = process_get_file(fd);

	if (STDIN == file)
	{
		int i = 0;
		char c;
		unsigned char* buf = buffer;

		for (; i < size; ++i)
		{
			c = input_getc();
			*buf++ = c;

			if ('\0' == c)
			{
				break;
			}
		}

		return i;
	}
	if ((NULL == file) || (STDOUT == file))
	{
		return -1;
	}

	int bytes_read = -1;
	
	lock_acquire(&filesys_lock);
	bytes_read = file_read(file, buffer, size);
	lock_release(&filesys_lock);

	return bytes_read;
}

#ifdef VM
/* 파일의 내용을 메모리의 특정 영역에 직접 연결(매핑)하는 시스템 콜 */
/* 성공 시, 이 함수는 파일이 매핑된 가상 주소를 반환 */
/* 실패 시에는 파일을 매핑하기에 유효하지 않은 주소인 NULL을 반드시 반환 */
uint64_t sys_mmap(void* addr, size_t len, int writable, int fd, off_t ofs)
{
	/* 1. 매개변수 유효성 검사 */
	/* addr이 NULL인지, 페이지가 정렬되지 않았는지, 커널 주소인지 검사 */
	if ((NULL == addr) || (0 != pg_ofs(addr)) || is_kernel_vaddr(addr) || is_kernel_vaddr(addr + len))
	{
		return NULL;
	}

	// /* len이 0 이하인지, offset이 페이지 정렬되지 않은지 검사 */
	if ((0 >= (int)len) || (0 != (ofs % PGSIZE)))
	{
		return NULL;
	}

	if (addr != pg_round_down(addr))
	{
		return NULL;
	}

	/* fd가 유효하고 표준 입출력인지 검사 */
	if (3 > fd)
	{
		return NULL;
	}

	struct file* file = process_get_file(fd);
	/* file이 NULL인지, 파일 길이가 0인지 검사 */
	if ((NULL == file) || (0 == file_length(file)))
	{
		return NULL;
	}

	return (uint64_t)do_mmap(addr, len, writable, file, ofs);
}

/* mmap으로 생성된 파일과 메모리 간의 연결(매핑)을 해제하는 시스템 콜 */
/* addr은 반드시 이전에 동일한 프로세스가 mmap을 호출해 */
/* 반환받았고 아직 매핑 해제되지 않은 가상 주소여야 함 */
void sys_munmap(void* addr)
{
	do_munmap(addr);
}
#endif