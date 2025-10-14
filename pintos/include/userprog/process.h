#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "include/filesys/off_t.h"

/* lazy_load_segment에 정보를 전달하기 위한 구조체 */
/* uninit 페이지에 저장됐다가 페이지 폴트 발생 시 lazy_load_segment 함수로 전달되는 정보들 */
struct lazy_load_info 
{
	/* 페이지에 로드해야 할 데이터가 담겨 있는 실행 파일 */
	/* 파일 시스템에 접근하고 필요한 데이터를 읽어올 수 있는 데이터의 출처를 알려주는 가장 중요한 정보 */
	struct file* file;
	/* 파일 내에서 이 페이지에 해당하는 데이터가 시작되는 오프셋, 파일 상의 위치를 나타냄. */
	/* file에서 데이터를 읽을 때, 어디서부터 읽기 시작해야 하는지를 알려줌. file_seek이나 file_read_at 함수에 */
	/* 이 값을 전달해 정확한 위치의 데이터를 가져올 수 있음. */
	off_t ofs;
	/* 해당 페이지를 채우기 위해 파일로부터 읽어와야 할 데이터의 크기(바이트 단위)를 나타냄. */
	/* 몇 바이트를 파일에서 읽어야 하는지 알려줌. */
	uint32_t read_bytes;
	/* 파일에서 데이터를 읽은 후, 페이지의 나머지 부분을 0으로 채워야 할 크기(바이트 단위)를 나타냄. */
	/* 페이지의 어느 부분부터 끝까지 0으로 채워야 하는지를 알려줌. */
	uint32_t zero_bytes;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

struct thread* get_child_process(tid_t tid);
int process_add_file(struct file* f);
struct file* process_get_file(int fd);
int process_close_file(int fd);
int process_insert_file(int fd, struct file* f);

#ifdef VM
bool lazy_load_segment (struct page *page, void *aux);
#endif

#endif /* userprog/process.h */
