#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct buffer{
	int len;
	int pos;
	char* array;
};

struct buffer* create_buffer(int len)
{
	void* mem = malloc(sizeof(struct buffer) + len*sizeof(char));
	struct buffer* buf = (struct buffer*)mem;
	buf->len = len;
	buf->pos = 0;
	buf->array = (char*)(mem + sizeof(struct buffer));
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

struct list{
	char* word;
	char spec;
	struct list* next;
};

struct list** insert_list(struct list** last, struct buffer* b, char spec)
{
	int i;
	struct list* p;
	if(b->pos == 0){
		return last;
	}
	*last = (struct list*)malloc(sizeof(struct list));
	p = *last;
	p->word = (char*)malloc((b->pos+1) * sizeof(char));
	for(i = 0; i < b->pos; i++){
		p->word[i] = b->array[i];
	}
	p->word[b->pos] = 0;
	p->spec = spec;
	p->next = NULL;
	b->pos = 0;
	return &(p->next);
}

struct list* free_list(struct list* p)
{
	while(p){
		struct list* tmp = p;
		p = p->next;
		free(tmp->word);
		free(tmp);
	}
	return NULL;
}

int len_list(struct list* p)
{
	int i = 0;
	while(p){
		i++;
		p = p->next;
	}
	return i;
}

int death_analys(int s, int pid)
{
	int code;
	if(WIFEXITED(s)){
		code = WEXITSTATUS(s);
		printf("Process %d finished with %d\n", pid, code);
	}else{
		code = 1;
		printf("Process %d killed by %d\n", pid, WTERMSIG(s));
	}
	return code;
}

void kill_zombie()
{
	int pid;
	int s;
	while((pid = waitpid(-1, &s, WNOHANG)) > 0){
		death_analys(s, pid);
	}
}

char** create_argv(struct list* p)
{
	int i;
	int len = len_list(p);
	char** argv = (char**)malloc((len+1) * sizeof(char*));
	for(i = 0; i < len; i++){
		argv[i] = p->word;
		p = p->next;
	}
	argv[len] = NULL;
	return argv;
}

struct list* strip_comment(struct list* p)
{
	struct list* first = p;
	if(p == NULL){
		return NULL;
	}
	if(p->word[0] == '#'){
		free_list(p);
		return NULL;
	}
	while(p->next){
		if(p->next->word[0] == '#'){
			free_list(p->next);
			p->next = NULL;
			return first;
		}
		p = p->next;
	}
	return first;
}

char check_block(struct list* p)
{
	if(p == NULL || p->next == NULL){
		return 1;
	}
	while(p->next->next){
		p = p->next;
	}
	if((strcmp(p->next->word, "&") == 0) && (p->next->spec == 1)){
		free(p->next->word);
		free(p->next);
		p->next = NULL;
		return 0;
	}
	return 1;
}

void execute(struct list* p)
{
	int pid;
	char block = check_block(p);
	if((pid = fork()) == 0){
		char** argv = create_argv(p);
		execvp(argv[0], argv);
		perror("execvp");
		exit(EXIT_FAILURE);
	}
	if(pid == -1){
		perror("fork");
		return;
	}
	if(block){
		waitpid(pid, NULL, 0);
	}else{
		printf("Process %d started\n", pid);
	}
	kill_zombie();
}

char need_ecr(char c)
{
	char* str = "\n'\"\\ ><|&";
	int i;
	for(i = 0; i < strlen(str); i++){
		if(c == str[i]){
			return 1;
		}
	}
	return 0;
}

char spec_symbol(char c)
{
	char* str = "<>|&";
	int i;
	for(i = 0; i < strlen(str); i++){
		if(c == str[i]){
			return 1;
		}
	}
	return 0;
}

int main()
{
	struct list* first = NULL;
	struct list** last = &first;
	struct buffer* buf = create_buffer(32);
	int c;
	char flag_d = 0;
	char flag_o = 0;
	char flag_e = 0;
	printf(">");
	while((c = getchar()) != EOF){
		if(flag_e){
			if(!need_ecr(c)){
				buf = insert_buffer(buf, '\\');
			}
			buf = insert_buffer(buf, c);
			flag_e = 0;
			continue;
		}
		switch(c){
		case '"':
			if(!flag_o){
				flag_d = !flag_d;
			}else{
				buf = insert_buffer(buf, c);
			}
			break;
		case '\'':
			if(!flag_d){
				flag_o = !flag_o;
			}else{
				buf = insert_buffer(buf, c);
			}
			break;
		case '\\':
			if(!flag_o){
				flag_e = 1;
			}else{
				buf = insert_buffer(buf, c);
			}
			break;
		case ' ': case '\n': case '>': case '<': case '&': case '|':
			if(flag_o || flag_d){
				buf = insert_buffer(buf, c);
				continue;
			}
			last = insert_list(last, buf, 0);
			if(spec_symbol(c)){
				buf->pos = 1;
				buf->array[0] = c;
				last = insert_list(last, buf, 1);
			}
			if(c == '\n'){
				first = strip_comment(first);
				if(first){
					execute(first);
				}
				first = free_list(first);
				last = &first;
				printf(">");
			}
			break;
		default:
			buf = insert_buffer(buf, c);
		}
	}
	printf("\n");
	return 0;
}
