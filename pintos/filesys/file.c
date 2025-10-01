#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* INODE를 넘겨받아 소유권을 가지는 파일 객체를 생성해 반환한다.
 * 메모리 할당에 실패하거나 INODE가 NULL이면 NULL 포인터를 반환한다. */
struct file *
file_open(struct inode *inode)
{
	struct file *file = calloc(1, sizeof *file);
	if (inode != NULL && file != NULL)
	{
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	}
	else
	{
		inode_close(inode);
		free(file);
		return NULL;
	}
}

/* FILE과 동일한 inode를 사용하는 새 파일 객체를 열어 반환한다.
 * 실패하면 NULL 포인터를 반환한다. */
struct file *
file_reopen(struct file *file)
{
	return file_open(inode_reopen(file->inode));
}

/* 파일 객체의 속성을 포함해 복제하고, FILE과 같은 inode를 사용하는 새 파일을 반환한다.
 * 실패하면 NULL 포인터를 반환한다. */
struct file *
file_duplicate(struct file *file)
{
	struct file *nfile = file_open(inode_reopen(file->inode));
	if (nfile)
	{
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write(nfile);
	}
	return nfile;
}

/* FILE을 닫는다. */
void file_close(struct file *file)
{
	if (file != NULL)
	{
		file_allow_write(file);
		inode_close(file->inode);
		free(file);
	}
}

/* FILE이 감싸고 있는 inode를 반환한다. */
struct inode *
file_get_inode(struct file *file)
{
	return file->inode;
}

/* FILE의 현재 위치에서 시작해 BUFFER로 SIZE바이트를 읽는다.
 * 실제로 읽은 바이트 수를 반환하며, 파일 끝이면 SIZE보다 작을 수 있다.
 * 읽은 바이트만큼 FILE의 위치를 이동한다. */
off_t file_read(struct file *file, void *buffer, off_t size)
{
	off_t bytes_read = inode_read_at(file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* FILE의 FILE_OFS 오프셋에서 시작해 BUFFER로 SIZE바이트를 읽는다.
 * 실제로 읽은 바이트 수를 반환하며, 파일 끝이면 SIZE보다 작을 수 있다.
 * FILE의 현재 위치는 변경되지 않는다. */
off_t file_read_at(struct file *file, void *buffer, off_t size, off_t file_ofs)
{
	return inode_read_at(file->inode, buffer, size, file_ofs);
}

/* FILE의 현재 위치에서 시작해 BUFFER의 데이터를 SIZE바이트만큼 FILE에 쓴다.
 * 실제로 기록한 바이트 수를 반환하며, 파일 끝이면 SIZE보다 작을 수 있다.
 * (보통은 파일을 확장하지만 아직 구현되어 있지 않다.)
 * 기록한 바이트만큼 FILE의 위치를 이동한다. */
off_t file_write(struct file *file, const void *buffer, off_t size)
{
	off_t bytes_written = inode_write_at(file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* FILE의 FILE_OFS 오프셋에서 시작해 BUFFER의 데이터를 SIZE바이트 쓰고,
 * 실제로 기록한 바이트 수를 반환한다. 파일 끝이면 SIZE보다 작을 수 있다.
 * (보통은 파일을 확장하지만 아직 구현되어 있지 않다.)
 * FILE의 현재 위치는 변경되지 않는다. */
off_t file_write_at(struct file *file, const void *buffer, off_t size,
					off_t file_ofs)
{
	return inode_write_at(file->inode, buffer, size, file_ofs);
}

/* file_allow_write()가 호출되거나 FILE을 닫을 때까지
 * 해당 inode에 대한 쓰기 작업을 금지한다. */
void file_deny_write(struct file *file)
{
	ASSERT(file != NULL);
	if (!file->deny_write)
	{
		file->deny_write = true;
		inode_deny_write(file->inode);
	}
}

/* FILE이 사용하는 inode에 대한 쓰기 작업을 다시 허용한다.
 * (같은 inode를 연 다른 파일이 있으면 여전히 거부될 수 있다.) */
void file_allow_write(struct file *file)
{
	ASSERT(file != NULL);
	if (file->deny_write)
	{
		file->deny_write = false;
		inode_allow_write(file->inode);
	}
}

/* FILE의 크기를 바이트 단위로 반환한다. */
off_t file_length(struct file *file)
{
	ASSERT(file != NULL);
	return inode_length(file->inode);
}

/* FILE의 현재 위치를 파일 시작점에서 NEW_POS 바이트 떨어진 곳으로 설정한다. */
void file_seek(struct file *file, off_t new_pos)
{
	ASSERT(file != NULL);
	ASSERT(new_pos >= 0);
	file->pos = new_pos;
}

/* FILE의 현재 위치를 파일 시작점 기준 바이트 오프셋으로 반환한다. */
off_t file_tell(struct file *file)
{
	ASSERT(file != NULL);
	return file->pos;
}
