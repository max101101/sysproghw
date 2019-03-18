#include <stdio.h>
#include <stdlib.h>

struct buffer{
	int len;
	int pos;
	int* array;
};

struct buffer* create_buffer(int len)
{
	void* mem = malloc(sizeof(struct buffer) + len*sizeof(int));
	struct buffer* buf = (struct buffer*)mem;
	buf->len = len;
	buf->pos = 0;
	buf->array = (int*)(mem + sizeof(struct buffer));
	return buf;
}

void free_buffer(struct buffer* buf)
{
	free(buf);
}

struct buffer* double_buffer(struct buffer* buf)
{
	struct buffer* new_buf = create_buffer(2 * buf->len);
	int i;
	for(i = 0; i < buf->pos; i++){
		new_buf->array[i] = buf->array[i];
	}
	new_buf->pos = buf->pos;
	free_buffer(buf);
	return new_buf;
}

struct buffer* insert_buffer(struct buffer* buf, int n)
{
	if(buf->pos == buf->len){
		buf = double_buffer(buf);
	}
	buf->array[buf->pos++] = n;
	return buf;
}

int main()
{
	return 0;
}
