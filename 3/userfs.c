#define NEED_OPEN_FLAGS
#define NEED_RESIZE

#include "userfs.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 1024,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_errcode = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

static struct block* new_block()
{
	struct block* block = malloc(sizeof(struct block));
	block->memory = malloc(BLOCK_SIZE * sizeof(char));
	block->occupied = 0;
	block->next = NULL;
	block->prev = NULL;
	return block;
}

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	bool deleted;
	int num_blocks;
	/* PUT HERE OTHER MEMBERS */
};

static struct file* new_file(const char* filename)
{
	struct file* file = malloc(sizeof(struct file));
	file->refs = 0;
	file->name = malloc(sizeof(char)*strlen(filename) + 1);
	memcpy(file->name, filename, strlen(filename)+1);
	file->next = NULL;
	file->prev = NULL;
	file->block_list = NULL;
	file->last_block = NULL;
	file->deleted = 0;
	file->num_blocks = 0;
	return file;
}

/** List of all files. */
static struct file *file_list = NULL;

static void unlink_file(struct file* file)
{
	if(file->prev){
		file->prev->next = file->next;
	}
	if(file->next){
		file->next->prev = file->prev;
	}
	if(file_list == file){
		file_list = file->next;
	}
}

static void delete_file(struct file* file)
{
	while(file->block_list){
		struct block* tmp = file->block_list;
		file->block_list = file->block_list->next;
		free(tmp->memory);
		free(tmp);
	}
	free(file->name);
	free(file);
}

static void insert_file(struct file* file)
{
	if(file_list == NULL){
		file_list = file;
		return;
	}
	file_list->prev = file;
	file->next = file_list;
	file_list = file;
}

static struct block* increase_file(struct file* file)
{
	struct block* block = new_block();
	if(file->block_list == NULL){
		file->block_list = block;
		file->last_block = block;
	}else{
		file->last_block->next = block;
		block->prev = file->last_block;
		file->last_block = block;
	}
	file->num_blocks++;
	return block;
}

struct filedesc {
	struct file *file;
	int block_num;
	int block_pos;
	int rw_flags;
	struct block* curr_block;

	/* PUT HERE OTHER MEMBERS */
};

static struct filedesc* new_fd(struct file* file, int flags)
{
	struct filedesc* fd = malloc(sizeof(struct filedesc));
	fd->file = file;
	fd->block_num = 0;
	fd->block_pos = 0;
	fd->rw_flags = flags;
	fd->curr_block = NULL;
	fd->file->refs++;
	return fd;
}

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

static int insert_fd(struct filedesc* fd)
{
	if(file_descriptor_count == file_descriptor_capacity){
		void* new_mem = realloc(file_descriptors,
			(file_descriptor_capacity+1) * 2 * sizeof(fd));
		if(new_mem){
			file_descriptors = new_mem;
		}
		file_descriptor_capacity = (file_descriptor_capacity+1) * 2;
		for(int i = file_descriptor_count;
			i < file_descriptor_capacity; i++)
		{
			file_descriptors[i] = NULL;
		}
	}
	for(int i = 0; i < file_descriptor_capacity; i++){
		if(file_descriptors[i] == NULL){
			file_descriptors[i] = fd;
			file_descriptor_count++;
			return i;
		}
	}
	return -1;
}

static void delete_fd(int fd)
{
	struct file* file = file_descriptors[fd]->file;
	if(--file->refs == 0 && file->deleted){
		delete_file(file);
	}
	free(file_descriptors[fd]);
	file_descriptors[fd] = NULL;
	file_descriptor_count--;
}

enum ufs_error_code
ufs_errno()
{
	return ufs_errcode;
}

/**
 * Find a file descriptor structure by its 1-based identifier.
 * @retval NULL Not found, errno is set to UFS_ERR_NO_FILE.
 * @retval not NULL Valid descriptor structure.
 */
static inline struct filedesc *
ufs_get_fd(int fd)
{
	fd = fd - 1;
	if(fd >= file_descriptor_capacity || fd < 0 ||
		!file_descriptors[fd])
	{
		return NULL;
	}
	return file_descriptors[fd];
}

int
ufs_open(const char *filename, int flags)
{
	int is_create = flags & UFS_CREATE;
	int fd_flags = flags & (UFS_READ_ONLY | UFS_WRITE_ONLY | UFS_READ_WRITE);
	if(fd_flags == 0){
		fd_flags = UFS_READ_WRITE;
	}
	//Search File
	struct file* file = file_list;
	while(file){
		if(!strcmp(file->name, filename)){
			ufs_errcode = UFS_ERR_NO_ERR;
			return insert_fd(new_fd(file, fd_flags)) + 1;
		}
		file = file->next;
	}
	//No File
	if(is_create){
		struct file* file = new_file(filename);
		insert_file(file);
		struct filedesc* fd = new_fd(file, fd_flags);
		ufs_errcode = UFS_ERR_NO_ERR;
		return insert_fd(fd) + 1;
	}
	ufs_errcode = UFS_ERR_NO_FILE;
	return -1;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	struct filedesc* filedesc = ufs_get_fd(fd);
	if(filedesc == NULL){
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	if((filedesc->rw_flags & (UFS_WRITE_ONLY | UFS_READ_WRITE)) == 0)
	{
		ufs_errcode = UFS_ERR_NO_PERMISSION; 
		return -1;
	}
	int curr_size;
	if(filedesc->block_num == 0){
		curr_size = 0;
	}else{
		curr_size = (filedesc->block_num-1)*BLOCK_SIZE
			+ filedesc->block_pos;
	}
	if(curr_size + size > MAX_FILE_SIZE)
	{
		ufs_errcode = UFS_ERR_NO_MEM;
		return -1;
	}
	if(filedesc->curr_block == NULL){
		if(filedesc->file->block_list){
			filedesc->curr_block = filedesc->file->block_list;
		}else{
			filedesc->curr_block = increase_file(filedesc->file);
		}
		filedesc->block_num = 1;
	}
	int b_written = 0;
	while(size){
		if(filedesc->block_pos == BLOCK_SIZE){
			filedesc->block_pos = 0;
			filedesc->block_num++;
			if(filedesc->curr_block->next == NULL){
				increase_file(filedesc->file);
			}
			filedesc->curr_block = filedesc->curr_block->next;
		}
		int free_b = BLOCK_SIZE - filedesc->curr_block->occupied;
		int b_to_write = size > free_b ? free_b : size;
		memcpy(&(filedesc->curr_block->memory[filedesc->block_pos]),
			&(buf[b_written]), b_to_write);
		filedesc->block_pos += b_to_write;
		b_written += b_to_write;
		size -= b_to_write;
		if(filedesc->block_pos > filedesc->curr_block->occupied){
			filedesc->curr_block->occupied = filedesc->block_pos;
		}
	}
	ufs_errcode = UFS_ERR_NO_ERR;
	return b_written;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	struct filedesc* filedesc = ufs_get_fd(fd);
	if(filedesc == NULL){
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	if((filedesc->rw_flags & (UFS_READ_ONLY | UFS_READ_WRITE)) == 0)
	{
		ufs_errcode = UFS_ERR_NO_PERMISSION; 
		return -1;
	}
	if(filedesc->curr_block == NULL)
	{
		if(filedesc->file->block_list == NULL){
			ufs_errcode = UFS_ERR_NO_ERR;
			return 0;
		}
		filedesc->curr_block = filedesc->file->block_list;
		filedesc->block_num = 1;
	}
	int b_read = 0;
	while(size){
		if(filedesc->block_pos == BLOCK_SIZE){
			if(filedesc->curr_block->next == NULL){
				ufs_errcode = UFS_ERR_NO_ERR;
				return b_read;
			}
			filedesc->block_pos = 0;
			filedesc->block_num++;
			filedesc->curr_block = filedesc->curr_block->next;
		}
		if(filedesc->block_pos == filedesc->curr_block->occupied){
			return b_read;
		}
		int free_b = filedesc->curr_block->occupied - filedesc->block_pos;
		int b_to_read = size > free_b ? free_b : size;
		memcpy(&(buf[b_read]),
			&(filedesc->curr_block->memory[filedesc->block_pos]), b_to_read);
		filedesc->block_pos += b_to_read;
		b_read += b_to_read;
		size -= b_to_read;
	}
	ufs_errcode = UFS_ERR_NO_ERR;
	return b_read;
}

int
ufs_close(int fd)
{
	struct filedesc* filedesc = ufs_get_fd(fd);
	if(filedesc == NULL){
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	delete_fd(fd-1);
	ufs_errcode = UFS_ERR_NO_ERR;
	return 0;
}

int
ufs_delete(const char *filename)
{
	//Search File
	struct file* file = file_list;
	while(file){
		if(!strcmp(file->name, filename)){
			unlink_file(file);
			file->deleted = 1;
			if(file->refs == 0){
				delete_file(file);
			}
			ufs_errcode = UFS_ERR_NO_ERR;
			return 0;
		}
		file = file->next;
	}
	ufs_errcode = UFS_ERR_NO_FILE;
	return -1;
}

int
ufs_resize(int fd, size_t new_size)
{
	struct filedesc* filedesc = ufs_get_fd(fd);
	if(filedesc == NULL){
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	if(new_size > MAX_FILE_SIZE){
		ufs_errcode = UFS_ERR_NO_MEM;
		return -1;
	}
	struct file* file = filedesc->file;
	size_t old_size;
	if(file->num_blocks == 0){
		old_size = 0;
	}else{
		old_size = (file->num_blocks-1) * BLOCK_SIZE
			+ file->last_block->occupied;
	}
	int new_num_blocks = new_size/BLOCK_SIZE + 1;
	int new_block_pos = new_size%BLOCK_SIZE;
	if(new_block_pos == 0){
		new_block_pos = 512;
		new_num_blocks--;
	}
	//Increase file size
	if(new_size >= old_size){
		while(file->num_blocks < new_num_blocks){
			increase_file(file);
		}
		ufs_errcode = UFS_ERR_NO_ERR;
		return 0;
	}
	//Find new last block
	struct block* new_last_block = NULL;
	if(new_num_blocks > 0){
		new_last_block = file->block_list;
	}
	for(int i = 1; i < new_num_blocks; ++i){
		new_last_block = new_last_block->next;
	}
	//delete rest file
	struct block* tmp = new_last_block->next;
	while(tmp){
		struct block* tmp2 = tmp;
		tmp = tmp->next;
		free(tmp2->memory);
		free(tmp2);
	}
	//set new last block
	file->last_block = new_last_block;
	file->num_blocks = new_num_blocks;
	if(new_last_block == NULL){
		file->block_list = NULL;
	}else{
		file->last_block->occupied = new_block_pos;
		new_last_block->next = NULL;
	}
	//find fds with the same file and move position if needed
	for(int i = 0; i < file_descriptor_capacity; ++i){
		struct filedesc* filedesc = file_descriptors[i];
		if(filedesc && (filedesc->file == file)){
			if(filedesc->block_num >= file->num_blocks){
				filedesc->block_num = file->num_blocks;
				filedesc->curr_block = file->last_block;
				if(filedesc->curr_block == NULL){
					filedesc->block_pos = 0;
				}else{
					filedesc->block_pos = file->last_block->occupied;
				}
			}
		}
	}
	ufs_errcode = UFS_ERR_NO_ERR;
	return 0;
}
