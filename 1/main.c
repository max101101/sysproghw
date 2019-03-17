#define _XOPEN_SOURCE /* Mac compatibility. */
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <signal.h>
#include <sys/mman.h>

#define STACK_SIZE 1024 * 1024

#define handle_error(msg) \
   do { perror(msg); exit(EXIT_FAILURE); } while (0)

static ucontext_t uctx_main;

static void* allocate_stack()
{
	void *stack = malloc(STACK_SIZE);
	stack_t ss;
	ss.ss_sp = stack;
	ss.ss_size = STACK_SIZE;
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	return stack;
}

struct buffer{
	int len;
	int pos;
	int* array;
};

struct context_data{
	FILE* file;
	struct buffer* buf;
	ucontext_t uctx_my;
	ucontext_t* uctx_next;
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

int part(int* a, int l, int r)
{
	int v = rand()%(r+1-l) + l;
	int i = l;
	int j = r;
	int tmp = a[l];
	a[l] = a[v];
	a[v] = tmp;
	v = a[l];
	while(i < j){
		while(i<=r && a[i]<=v){
			i++;
		}
		while(v < a[j]){
			j--;
		}
		if(i < j){
			tmp = a[i];
			a[i] = a[j];
			a[j] = tmp;
		}
	}
	tmp = a[j];
	a[j] = a[l];
	a[l] = tmp;
	return j;
}

void quick_sort(int* a, int l, int r)
{
	if(l<r){
		int p = part(a, l, r);
		quick_sort(a, l, p-1);
		quick_sort(a, p+1, r);
	}
}

void sort_file(struct context_data* data)
{
	int i;
	int n = 0;
	while(fscanf(data->file, "%d", &n) == 1){
		data->buf = insert_buffer(data->buf, n);
	}
	if (swapcontext(&data->uctx_my, data->uctx_next) == -1)
		handle_error("swapcontext");
	quick_sort(data->buf->array, 0, data->buf->pos - 1);
	if (swapcontext(&data->uctx_my, data->uctx_next) == -1)
		handle_error("swapcontext");
	rewind(data->file);
	if (swapcontext(&data->uctx_my, data->uctx_next) == -1)
		handle_error("swapcontext");
	for(i = 0; i < data->buf->pos; i++){
		fprintf(data->file, "%d ", data->buf->array[i]);
	}
	if (swapcontext(&data->uctx_my, data->uctx_next) == -1)
		handle_error("swapcontext");
	fclose(data->file);
}

void merge_files(struct context_data data[], int n, FILE* file)
{
	for(;;){
		int i;
		int min = INT_MAX;
		int flag = -1;
		for(i = 0; i < n; i++){
			if(data[i].buf->pos == 0){
				continue;
			}
			int v = data[i].buf->array[0];
			if(min >= v){
				min = v;
				flag = i;
			}
		}
		if(flag == -1){
			break;
		}
		fprintf(file, "%d ", min);
		data[flag].buf->array = &data[flag].buf->array[1];
		data[flag].buf->pos--;
	}
}

int main(int argc, char** argv)
{
	if(argc < 2){
		handle_error("no jobs");
	}
	int i;
	srand(clock());
	struct context_data data[argc-1];
	//init uctx
	for(i = 0; i < argc-1; i++){
		FILE* f = fopen(argv[i+1], "r+");
		if(f == NULL){
			handle_error("file not opened");
		}
		data[i].file = f;
		data[i].buf = create_buffer(128);
		char* stack = allocate_stack();
		if (getcontext(&data[i].uctx_my) == -1)
			handle_error("getcontext");
		data[i].uctx_my.uc_stack.ss_sp = stack;
		data[i].uctx_my.uc_stack.ss_size = STACK_SIZE;
	}
	//uctx switch queue
	for(i = 0; i < argc-1; i++){
		if(i == argc - 2){
			data[i].uctx_my.uc_link = &uctx_main;
			data[i].uctx_next = &data[0].uctx_my;
		}else{
			data[i].uctx_my.uc_link = &data[i+1].uctx_my;
			data[i].uctx_next = &data[i+1].uctx_my;
		}
		makecontext(&data[i].uctx_my, sort_file, 1, &data[i]);
	}
	//start uctx
	if (swapcontext(&uctx_main, &data[0].uctx_my) == -1)
		handle_error("swapcontext");
	//merge
	FILE* out = fopen("out.txt", "w");
	if(out == NULL){
		handle_error("file not opened");
	}
	merge_files(data, argc-1, out);
	//end
	fclose(out);
	for(i = 0; i < argc-1; i++){
		free_buffer(data[i].buf);
	}
	return 0;
}
