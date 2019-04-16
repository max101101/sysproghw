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

struct block* new_block()
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
	const char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;
	//Soft delete
	bool deleted;

	/* PUT HERE OTHER MEMBERS */
};

struct file* new_file(const char* filename)
{
	struct file* file = malloc(sizeof(struct file));
	file->refs = 0;
	file->name = filename;
	file->next = NULL;
	file->prev = NULL;
	file->block_list = new_block();
	file->last_block = file->block_list;
	file->deleted = 0;
	return file;
}

/** List of all files. */
static struct file *file_list = NULL;

void delete_file(struct file* file)
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
	free(file);
}

void insert_file(struct file* file)
{
	if(file_list == NULL){
		file_list = file;
		return;
	}
	file_list->prev = file;
	file->next = file_list;
	file_list = file;
}

struct filedesc {
	struct file *file;

	/* PUT HERE OTHER MEMBERS */
};

struct filedesc* new_fd(struct file* file)
{
	struct filedesc* fd = malloc(sizeof(struct filedesc));
	fd->file = file;
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

int insert_fd(struct filedesc* fd)
{
	if(file_descriptor_count == file_descriptor_capacity){
		void* new_mem = realloc(file_descriptors,
			file_descriptor_capacity * sizeof(struct filedesc*));
		if(new_mem != NULL){
			file_descriptors = new_mem;
		}
		int last = file_descriptor_capacity *= 2;
		for(int i = file_descriptor_count; i < last; i++){
			file_descriptors[i] = NULL;
		}
	}
	for(int i = 0; i < file_descriptor_capacity; i++){
		if(!file_descriptors[i]){
			file_descriptors[i] = fd;
			file_descriptor_count++;
			return i;
		}
	}
	return -1;
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
	if(file_descriptor_count == 0){
		if(is_create){
			file_descriptors = malloc(4 * sizeof(struct filedesc*));
			struct file* file = new_file(filename);
			insert_file(file);
			file_descriptors[0] = new_fd(file);
			for(int i = 1; i < 4; i++){
				file_descriptors[i] = NULL;
			}
			file_descriptor_count = 1;
			file_descriptor_capacity = 4;
			ufs_errcode = UFS_ERR_NO_ERR;
			return 1;
		}else{
			ufs_errcode = UFS_ERR_NO_FILE;
			return -1;
		}
	}
	//Search File
	struct file* file = file_list;
	while(file){
		if(!strcmp(file->name, filename)){
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
	/* IMPLEMENT THIS FUNCTION */
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
}

void remove_fd(struct file* file)
{
	if(--file->refs == 0 && file->deleted){
		delete_file(file);
	}
}

int
ufs_close(int fd)
{
	fd = fd - 1;
	if(fd >= file_descriptor_capacity){
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
	if(file_descriptors[fd]){
		remove_fd(file_descriptors[fd]->file);
		file_descriptors[fd] = NULL;
		ufs_errcode = UFS_ERR_NO_ERR;
		return 0;
	}else{
		ufs_errcode = UFS_ERR_NO_FILE;
		return -1;
	}
}
