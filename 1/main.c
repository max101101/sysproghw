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

#define micro_secs(a) (1000000*a/CLOCKS_PER_SEC)

static ucontext_t uctx_main;
static unsigned long timeslice;

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
	char finished;
	int swap_count;
	unsigned long time_work;
	unsigned long timestamp;
	ucontext_t uctx_my;
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

//set ucontext to swich after end of coroutine
//track time
void terminate(struct context_data data[], int n, int size)
{
	int i;
	data[n].finished = 1;
	data[n].time_work += micro_secs(clock()) - data[n].timestamp;
	for(i = n+1; i < size; i++){
		if(data[i].finished == 1){
			continue;
		}
		data[n].uctx_my.uc_link = &data[i].uctx_my;
		return;
	}
	for(i = 0; i < n; i++){
		if(data[i].finished == 1){
			continue;
		}
		data[n].uctx_my.uc_link = &data[i].uctx_my;
		return;
	}
	data[n].uctx_my.uc_link = &uctx_main;
}

//swap to first alive coroutine
//track time
void swap(struct context_data data[], int n, int size)
{
	int i;
	unsigned long time;
	if(micro_secs(clock()) - data[n].timestamp < timeslice){
		return;
	}
	for(i = n+1; i < size; i++){
		if(data[i].finished == 1){
			continue;
		}
		data[n].time_work += micro_secs(clock()) - data[n].timestamp;
		data[n].swap_count++;
		if(swapcontext(&data[n].uctx_my, &data[i].uctx_my) == -1){
			handle_error("swapcontext");
		}
		data[n].timestamp = micro_secs(clock());
		return;
	}
	for(i = 0; i < n; i++){
		if(data[i].finished == 1){
			continue;
		}
		data[n].time_work += micro_secs(clock()) - data[n].timestamp;
		data[n].swap_count++;
		if(swapcontext(&data[n].uctx_my, &data[i].uctx_my) == -1){
			handle_error("swapcontext");
		}
		data[n].timestamp = micro_secs(clock());
		return;
	}
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

void quick_sort(int* a, int l, int r, struct context_data data[], int n, int size)
{
	if(l<r){
		int p = part(a, l, r);
		swap(data, n, size);
		quick_sort(a, l, p-1, data, n, size);
		swap(data, n, size);
		quick_sort(a, p+1, r, data, n, size);
		swap(data, n, size);
	}
}

void sort_file(struct context_data data[], int n, int size)
{
	int i;
	int c = 0;
	data[n].timestamp = micro_secs(clock());
	while(fscanf(data[n].file, "%d", &c) == 1){
		data[n].buf = insert_buffer(data[n].buf, c);
		swap(data, n, size);
	}
	swap(data, n, size);
	quick_sort(data[n].buf->array, 0, data[n].buf->pos - 1, data, n, size);
	swap(data, n, size);
	rewind(data[n].file);
	swap(data, n, size);
	for(i = 0; i < data[n].buf->pos; i++){
		fprintf(data[n].file, "%d ", data[n].buf->array[i]);
		swap(data, n, size);
	}
	swap(data, n, size);
	fclose(data[n].file);
	terminate(data, n , size);
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
	if(argc < 3){
		printf("no jobs to do\n");
		return 0;
	}
	int i;
	int coroutines_num = argc-2;
	timeslice = atoi(argv[1])/coroutines_num;
	unsigned long start = micro_secs(clock());
	srand(start);
	struct context_data data[coroutines_num];
	//init uctx
	for(i = 0; i < coroutines_num; i++){
		FILE* f = fopen(argv[i+2], "r+");
		if(f == NULL){
			handle_error("file not opened");
		}
		data[i].file = f;
		data[i].buf = create_buffer(128);
		data[i].finished = 0;
		data[i].time_work = 0;
		data[i].swap_count = 0;
		char* stack = allocate_stack();
		if (getcontext(&data[i].uctx_my) == -1)
			handle_error("getcontext");
		data[i].uctx_my.uc_stack.ss_sp = stack;
		data[i].uctx_my.uc_stack.ss_size = STACK_SIZE;
	}
	//start uctx
	for(i = 0; i < coroutines_num; i++){
		makecontext(&data[i].uctx_my, sort_file, 3, data, i, coroutines_num);
	}
	if (swapcontext(&uctx_main, &data[0].uctx_my) == -1)
		handle_error("swapcontext");
	//merge after end of uctx
	FILE* out = fopen("out.txt", "w");
	if(out == NULL){
		handle_error("file not opened");
	}
	merge_files(data, coroutines_num, out);
	//end
	fclose(out);
	for(i = 0; i < coroutines_num; i++){
		free_buffer(data[i].buf);
	}
	//stat
	unsigned long result_time = micro_secs(clock())- start;
	printf("Coroutine main time: %ld\n", result_time);
	for(i = 0; i < coroutines_num; i++){
		printf("Coroutine %d swaps: %d times, total time: %lu\n",
			i, data[i].swap_count, data[i].time_work);
		result_time += data[i].time_work;
	}
	printf("Total time: %ld\n", result_time);
	return 0;
}
