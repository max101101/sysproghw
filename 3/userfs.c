#include "userfs.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

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
	//Soft delete
	bool deleted;

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
	return file;
}

/** List of all files. */
static struct file *file_list = NULL;

static void delete_file(struct file* file)
{
	while(file->block_list){
		struct block* tmp = file->block_list;
		file->block_list = file->block_list->next;
		free(tmp->memory);
		free(tmp);
	}
	if(file->prev){
		file->prev->next = file->next;
	}
	if(file->next){
		file->next->prev = file->prev;
	}
	if(file_list == file){
		file_list = file->next;
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

static void increase_file(struct file* file)
{
	struct block* block = new_block();
	if(file->block_list == NULL){
		file->block_list = block;
		file->last_block = block;
		return;
	}
	file->last_block->next = block;
	block->prev = file->last_block;
	file->last_block = block;
}

struct filedesc {
	struct file *file;
	int block_num;
	int block_pos;

	/* PUT HERE OTHER MEMBERS */
};

static struct filedesc* new_fd(struct file* file)
{
	struct filedesc* fd = malloc(sizeof(struct filedesc));
	fd->file = file;
	fd->block_num = 0;
	fd->block_pos = 0;
	fd->file->refs++;
	return fd;
}

static struct block* get_curr_block(struct filedesc* fd)
{
	struct block* ret = fd->file->block_list;
	for(int i = 0; i < fd->block_num; i++){
		ret = ret->next;
	}
	return ret;
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
			file_descriptor_capacity * sizeof(fd));
		if(new_mem){
			file_descriptors = new_mem;
		}
		file_descriptor_capacity *= 2;
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

int
ufs_open(const char *filename, int flags)
{
	int is_create = flags & UFS_CREATE;
	//Init FS State
	if(file_descriptor_capacity == 0 && is_create){
		file_descriptors = malloc(4 * sizeof(struct filedesc*));
		for(int i = 0; i < 4; i++){
			file_descriptors[i] = NULL;
		}
		file_descriptor_count = 0;
		file_descriptor_capacity = 4;
	}
	//Search File
	struct file* file = file_list;
	while(file){
		if(!strcmp(file->name, filename) && !file->deleted){
			ufs_errcode = UFS_ERR_NO_ERR;
			return insert_fd(new_fd(file)) + 1;
		}
		file = file->next;
	}
	//No File
	if(is_create){
		struct file* file = new_file(filename);
		insert_file(file);
		struct filedesc* fd = new_fd(file);
		ufs_errcode = UFS_ERR_NO_ERR;
		return insert_fd(fd) + 1;
	}
	ufs_errcode = UFS_ERR_NO_FILE;
	return -1;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	fd = fd - 1;
	if(fd >= file_descriptor_capacity || fd < 0 ||
		!file_descriptors[fd])
	{
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	struct filedesc* filedesc = file_descriptors[fd];
	if(filedesc->block_num * BLOCK_SIZE +
		filedesc->block_pos + size > MAX_FILE_SIZE)
	{
		ufs_errcode = UFS_ERR_NO_MEM;
		return -1;
	}
	struct block* curr_block = get_curr_block(filedesc);
	if(curr_block == NULL){
		increase_file(filedesc->file);
		curr_block = get_curr_block(filedesc);
	}
	size_t i;
	for(i = 0; i < size; i++){
		curr_block->memory[filedesc->block_pos] = buf[i];
		if(filedesc->block_pos == curr_block->occupied){
			curr_block->occupied++;
		}
		filedesc->block_pos++;
		if(filedesc->block_pos == BLOCK_SIZE){
			filedesc->block_num++;
			filedesc->block_pos = 0;
			if(curr_block->next == NULL){
				increase_file(filedesc->file);
			}
			curr_block = curr_block->next;
		}
	}
	ufs_errcode = UFS_ERR_NO_ERR;
	return i;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	fd = fd - 1;
	if(fd >= file_descriptor_capacity || fd < 0 ||
		!file_descriptors[fd])
	{
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	struct filedesc* filedesc = file_descriptors[fd];
	struct block* curr_block = get_curr_block(filedesc);
	if(curr_block == NULL ||
		curr_block->occupied == filedesc->block_pos)
	{
		ufs_errcode = UFS_ERR_NO_ERR;
		return 0;
	}
	size_t i;
	for(i = 0; i < size; i++){
		if(filedesc->block_pos < curr_block->occupied){
			buf[i] = curr_block->memory[filedesc->block_pos];
			filedesc->block_pos++;
		}else if(filedesc->block_pos == BLOCK_SIZE){
			curr_block = curr_block->next;
			filedesc->block_num++;
			filedesc->block_pos = 0;
			if(curr_block == NULL){
				break;
			}
		}else{
			break;
		}
	}
	ufs_errcode = UFS_ERR_NO_ERR;
	return i;
}

int
ufs_close(int fd)
{
	fd = fd - 1;
	if(fd >= file_descriptor_capacity || fd < 0 ||
		!file_descriptors[fd])
	{
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	delete_fd(fd);
	ufs_errcode = UFS_ERR_NO_ERR;
	return 0;
}

int
ufs_delete(const char *filename)
{
	//Search File
	struct file* file = file_list;
	while(file){
		if(!strcmp(file->name, filename) && !file->deleted){
			if(file->refs == 0){
				delete_file(file);
			}else{
				file->deleted = 1;
			}
			ufs_errcode = UFS_ERR_NO_ERR;
			return 0;
		}
		file = file->next;
	}
	ufs_errcode = UFS_ERR_NO_FILE;
	return -1;
}
