#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

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

struct buffer* sort_file(FILE* f)
{
	int i;
	int n = 0;
	struct buffer* buf = create_buffer(128);
	while(fscanf(f, "%d", &n) == 1){
		buf = insert_buffer(buf, n);
	}
	quick_sort(buf->array, 0, buf->pos - 1);
	rewind(f);
	for(i = 0; i < buf->pos; i++){
		fprintf(f, "%d ", buf->array[i]);
	}
	return buf;
}

void merge_files(struct buffer* bufs[], int n, FILE* file)
{
	for(;;){
		int i;
		int min = INT_MAX;
		int flag = -1;
		for(i = 0; i < n; i++){
			if(bufs[i]->pos == 0){
				continue;
			}
			int v = bufs[i]->array[0];
			if(min >= v){
				min = v;
				flag = i;
			}
		}
		if(flag == -1){
			break;
		}
		fprintf(file, "%d ", min);
		bufs[flag]->array = &bufs[flag]->array[1];
		bufs[flag]->pos--;
	}
}

int main(int argc, char** argv)
{
	int i;
	srand(clock());
	struct buffer* bufs[argc-1];
	for(i = 1; i < argc; i++){
		FILE* f = fopen(argv[i], "r+");
		if(f == NULL){
			continue;
		}
		bufs[i-1] = sort_file(f);
		fclose(f);
	}
	FILE* out = fopen("out.txt", "w");
	if(out == NULL){
		return 1;
	}
	merge_files(bufs, argc-1, out);
	fclose(out);
	for(i = 0; i < argc-1; i++){
		free_buffer(bufs[i]);
	}
	return 0;
}
