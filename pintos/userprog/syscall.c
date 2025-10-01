#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/file.h"

typedef void syscall_handler_func(struct intr_frame* f);

static syscall_handler_func* syscall_handlers[SYS_END];

extern struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void sys_halt(struct intr_frame* f);

void sys_exit(struct intr_frame* f);
void sys_open(struct intr_frame* f);
void sys_close(struct intr_frame* f);
void sys_filesize(struct intr_frame* f);
void sys_wait(struct intr_frame* f);
void sys_exec(struct intr_frame* f);
void sys_tell(struct intr_frame* f);
void sys_remove(struct intr_frame* f);

void sys_create(struct intr_frame* f);
void sys_fork(struct intr_frame* f);
void sys_seek(struct intr_frame* f);
void sys_dup2(struct intr_frame* f);

void sys_write(struct intr_frame* f);
void sys_read(struct intr_frame* f);

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

	syscall_handlers[SYS_HALT] = sys_halt;
	syscall_handlers[SYS_EXIT] = sys_exit;
	syscall_handlers[SYS_FORK] = sys_fork;
	syscall_handlers[SYS_EXEC] = sys_exec;
	syscall_handlers[SYS_WAIT] = sys_wait;
	syscall_handlers[SYS_CREATE] = sys_create;
	syscall_handlers[SYS_REMOVE] = sys_remove;
	syscall_handlers[SYS_OPEN] = sys_open;
	syscall_handlers[SYS_FILESIZE] = sys_filesize;
	syscall_handlers[SYS_READ] = sys_read;
	syscall_handlers[SYS_WRITE] = sys_write;
	syscall_handlers[SYS_SEEK] = sys_seek;
	syscall_handlers[SYS_TELL] = sys_tell;
	syscall_handlers[SYS_CLOSE] = sys_close;

	syscall_handlers[SYS_MMAP] = NULL;
	syscall_handlers[SYS_MUNMAP] = NULL;

	syscall_handlers[SYS_CHDIR] = NULL;
	syscall_handlers[SYS_MKDIR] = NULL;
	syscall_handlers[SYS_READDIR] = NULL;
	syscall_handlers[SYS_ISDIR] = NULL;
	syscall_handlers[SYS_INUMBER] = NULL;
	syscall_handlers[SYS_SYMLINK] = NULL;
	
	syscall_handlers[SYS_DUP2] = sys_dup2;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	uint64_t syscall_num = f->R.rax;

	if ((SYS_HALT <= syscall_num) && (SYS_END > syscall_num))
	{
		syscall_handler_func* handler = syscall_handlers[syscall_num];

		if (NULL != handler)
		{
			handler(f);

			return;
		}
	}

	thread_current()->exit_status = -1;

	thread_exit();
}

void check_address(void* addr)
{
	if ((NULL == addr) || is_kernel_vaddr(addr) || (NULL == pml4_get_page(thread_current()->pml4, addr)))
	{
		thread_current()->exit_status = -1;

		thread_exit();
	}
}

void check_valid_buffer(const void* buffer, unsigned int size)
{
	for (void* p = pg_round_down(buffer); p < (buffer + size); p += PGSIZE)
	{
		check_address(p);
	}
}

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

void sys_halt(struct intr_frame* f)
{
	power_off();
}

void sys_exit(struct intr_frame* f)
{
	thread_current()->exit_status = (int)f->R.rdi;

	thread_exit();
}

void sys_open(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;

	check_valid_string(file);

	if ('\0' == *file)
	{
		f->R.rax = -1;
		return;
	}

	struct thread* cur = thread_current();

	struct file* open_file = filesys_open(file);

	if (NULL == open_file)
	{
		f->R.rax = -1;
		return;
	}

	int fd = 2;
	while ((FDT_COUNT_LIMIT > fd) && (NULL != cur->fd_table[fd]))
	{
		++fd;
	}

	if (FDT_COUNT_LIMIT <= fd)
	{
		file_close(open_file);

		f->R.rax = -1;

		return;
	}

	cur->fd_table[fd] = open_file;

	f->R.rax = fd;
}

void sys_close(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		return;
	}

	if ((STDIN == cur->fd_table[fd]) || (STDOUT == cur->fd_table[fd]))
	{
		cur->fd_table[fd] = NULL;
		return;
	}

	if (0 == cur->fd_table[fd]->dup_count)
	{
		file_close(cur->fd_table[fd]);
		cur->fd_table[fd] = NULL;
	}
	else
	{
		--cur->fd_table[fd]->dup_count;
	}
}

void sys_filesize(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		f->R.rax = -1;
		return;
	}

	struct file* target_file = cur->fd_table[fd];

	if ((STDIN == target_file) || (STDOUT == target_file))
	{
		f->R.rax = -1;

		return;
	}

	int size = file_length(target_file);

	f->R.rax = size;
}

void sys_wait(struct intr_frame* f)
{
	f->R.rax = process_wait((int)f->R.rdi);
}

void sys_exec(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;

	check_valid_string(file);

	char* fn_copy = palloc_get_page(0);
	
	if (NULL == fn_copy)
	{
		f->R.rax = -1;
		return;
	}

	strlcpy(fn_copy, file, PGSIZE);

	if (-1 == process_exec(fn_copy))
	{
		thread_current()->exit_status = -1;

		thread_exit();
	}
}

void sys_tell(struct intr_frame* f)
{
	int fd = f->R.rdi;

	struct thread* cur = thread_current();

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		f->R.rax = -1;
		return;
	}

	struct file* target_file = cur->fd_table[fd];

	if ((STDIN == target_file) || (STDOUT == target_file))
	{
		f->R.rax = -1;
		return;
	}

	unsigned int position = file_tell(target_file);

	f->R.rax = position;
}

void sys_remove(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;

	check_valid_string(file);

	bool success = filesys_remove(file);

	f->R.rax = success;
}

void sys_create(struct intr_frame* f)
{
	const char* file = (const char*)f->R.rdi;
	unsigned int initial_size = f->R.rsi;

	check_valid_string(file);


	if ('\0' == *file)
	{
		f->R.rax = 0;
		return;
	}

	bool success = filesys_create(file, initial_size);

	f->R.rax = success;
}

void sys_fork(struct intr_frame* f)
{
	const char* thread_name = (const char*)f->R.rdi;

	check_valid_string(thread_name);

	f->R.rax = process_fork(thread_name, f);
}

void sys_seek(struct intr_frame* f)
{
	int fd = f->R.rdi;
	unsigned int pos = f->R.rsi;

	struct thread* cur = thread_current();

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd) || (NULL == cur->fd_table[fd]))
	{
		return;
	}

	struct file* target_file = cur->fd_table[fd];

	if ((STDIN == target_file) || (STDOUT == target_file))
	{
		return;
	}

	file_seek(target_file, pos);
}

void sys_dup2(struct intr_frame* f)
{
	int oldfd = f->R.rdi;
	int newfd = f->R.rsi;

	if ((0 > newfd) || (FDT_COUNT_LIMIT <= newfd) || (0 > oldfd) || (FDT_COUNT_LIMIT <= oldfd))
	{
		f->R.rax = -1;
		return;
	}

	if (oldfd == newfd)
	{
		f->R.rax = newfd;
		return;
	}

	struct thread* cur = thread_current();

	if (NULL == cur->fd_table[oldfd])
	{
		f->R.rax = -1;
		return;
	}

	if (NULL != cur->fd_table[newfd])
	{
		struct intr_frame temp_f;
		temp_f.R.rdi = newfd;
		sys_close(&temp_f);
	}

	if ((STDIN != cur->fd_table[oldfd]) && (STDOUT != cur->fd_table[oldfd]))
	{
		++(cur->fd_table[oldfd]->dup_count);
	}

	cur->fd_table[newfd] = cur->fd_table[oldfd];

	f->R.rax = newfd;
}

void sys_write(struct intr_frame* f)
{
	int fd = f->R.rdi;
	const void* buffer = (const void*)f->R.rsi;
	unsigned int size = f->R.rdx;

	if (0 == size)
	{
		f->R.rax = 0;
		return;
	}

	check_valid_buffer(buffer, size);

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd))
	{
		f->R.rax = -1;
		return;
	}

	struct thread* cur = thread_current();
	int bytes_write = -1;
	struct file* target_file = cur->fd_table[fd];

	if (NULL == target_file)
	{
		f->R.rax = -1;
		return;
	}

	if (STDOUT == target_file)
	{
		putbuf(buffer, size);

		bytes_write = size;
	}
	else if (STDIN == target_file)
	{
		bytes_write = -1;
	}
	else
	{
		lock_acquire(&filesys_lock);
		bytes_write = file_write(target_file, buffer, size);
		lock_release(&filesys_lock);
	}

	f->R.rax = bytes_write;
}

void sys_read(struct intr_frame* f)
{
	int fd = f->R.rdi;
	void* buffer = (void*)f->R.rsi;
	unsigned int size = f->R.rdx;

	if (0 == size)
	{
		f->R.rax = 0;
		return;
	}

	check_valid_buffer(buffer, size);

	if ((0 > fd) || (FDT_COUNT_LIMIT <= fd))
	{
		f->R.rax = -1;
		return;
	}

	struct thread* cur = thread_current();
	int bytes_read = -1;

	struct file* target_file = cur->fd_table[fd];
	if (NULL == target_file)
	{
		f->R.rax = -1;
		return;
	}

	if (STDIN == target_file)
	{
		char* local_buf = (char*)buffer;

		for (unsigned int i = 0; i < size; ++i)
		{
			local_buf[i] = input_getc();
		}
	}
	else if (STDOUT == target_file)
	{
		bytes_read = -1;
	}
	else
	{
		lock_acquire(&filesys_lock);
		bytes_read = file_read(target_file, buffer, size);
		lock_release(&filesys_lock);
	}

	f->R.rax = bytes_read;
}