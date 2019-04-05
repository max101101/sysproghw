#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

enum spec_value{
	SPEC_DEFAULT,
	SPEC_FIRST,
	SPEC_SECOND
};

enum logic_value{
	LOGIC_LAST,
	LOGIC_AND,
	LOGIC_OR,
	LOGIC_EMPTY
};

enum pipe_value{
	PIPE_EMPTY,
	PIPE_LAST,
	PIPE_DEFAULT
};

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

struct head_list{
	struct list* first;
	struct list* last;
};

struct list{
	char* word;
	int spec;
	struct list* next;
};

void insert_list(struct head_list* list, struct buffer* b, int spec)
{
	int i;
	struct list* p;
	if(b->pos == 0){
		return;
	}
	p = (struct list*)malloc(sizeof(struct list));
	p->word = (char*)malloc((b->pos+1) * sizeof(char));
	memcpy(p->word, b->array, b->pos);
	p->word[b->pos] = 0;
	p->spec = spec;
	p->next = NULL;
	b->pos = 0;
	if(list->first){
		list->last->next = p;
		list->last = p;
	}else{
		list->first = p;
		list->last = p;
	}
}

void free_list(struct list* p)
{
	while(p){
		struct list* tmp = p;
		p = p->next;
		free(tmp->word);
		free(tmp);
	}
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
	int exit_code;
	if(WIFEXITED(s)){
		exit_code = WEXITSTATUS(s);
		printf("Process %d finished with %d\n", pid, exit_code);
	}else{
		exit_code = 1;
		printf("Process %d killed by %d\n", pid, WTERMSIG(s));
	}
	return exit_code;
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

void strip_comment(struct head_list* list)
{
	struct list* tmp = list->first;
	if(tmp == NULL){
		return;
	}
	if(tmp->word[0] == '#'){
		free_list(list->first);
		list->first = NULL;
		list->last = NULL;
		return;
	}
	while(tmp->next){
		if(tmp->next->word[0] == '#'){
			free_list(tmp->next);
			tmp->next = NULL;
			list->last = tmp;
			return;
		}
		tmp = tmp->next;
	}
}

bool check_block(struct head_list* list)
{
	if((list->last == NULL) || (list->first == list->last)){
		return 1;
	}
	if((strcmp(list->last->word, "&") == 0) &&
		(list->last->spec == SPEC_FIRST)){
		struct list* tmp = list->first;
		while(tmp->next != list->last){
			tmp = tmp->next;
		}
		free_list(list->last);
		tmp->next = NULL;
		return 0;
	}
	return 1;
}

char check_or(struct list* p)
{
	if((strcmp(p->word, "|")) == 0 && (p->spec == SPEC_FIRST) &&
		(p->next) && (strcmp(p->next->word, "|") == 0) &&
		(p->next->spec == SPEC_SECOND)){
		return 1;
	}
	return 0;
}

char check_and(struct list* p)
{
	if((strcmp(p->word, "&")) == 0 && (p->spec == SPEC_FIRST) &&
		(p->next) && (strcmp(p->next->word, "&") == 0) &&
		(p->next->spec == SPEC_SECOND)){
		return 1;
	}
	return 0;
}

int split_logic(struct list* head, struct list** tail)
{
	if(head == NULL){
		return LOGIC_EMPTY;
	}
	while(head->next){
		if(check_or(head->next)){
			*tail = head->next->next->next;
			head->next = NULL;
			return LOGIC_OR;
		}else if(check_and(head->next)){
			*tail = head->next->next->next;
			head->next = NULL;
			return LOGIC_AND;
		}
		head = head->next;
	}
	return LOGIC_LAST;
}

char check_pipe(struct list* p)
{
	if((strcmp(p->word, "|")) == 0 && (p->spec == SPEC_FIRST)){
		return 1;
	}
	return 0;
}

int split_pipeline(struct list* head, struct list** tail)
{
	if(head == NULL){
		return PIPE_EMPTY;
	}
	while(head->next){
		if(check_pipe(head->next)){
			*tail = head->next->next;
			head->next = NULL;
			return PIPE_DEFAULT;
		}
		head = head->next;
	}
	return PIPE_LAST;
}

void dup_pipe(int fd[])
{
	if(fd[0] != -1){
		dup2(fd[0], 0);
		close(fd[0]);
	}
	if(fd[3] != -1){
		dup2(fd[3], 1);
		close(fd[3]);
	}
}

void close_pipe_fork(int fd[])
{
	if(fd[1] != -1){
		close(fd[1]);
	}
	if(fd[2] != -1){
		close(fd[2]);
	}
}

void prepare_fd(int fd[])
{
	if(fd[0] != -1){
		close(fd[0]);
	}
	if(fd[1] != -1){
		close(fd[1]);
	}
	fd[0] = fd[2];
	fd[1] = fd[3];
	fd[2] = -1;
	fd[3] = -1;
}

char check_append(struct list* p)
{
	if(p == NULL){
		return 0;
	}
	if((strcmp(p->word, ">")) == 0 && (p->spec == SPEC_FIRST) &&
		(p->next) && (strcmp(p->next->word, ">") == 0) &&
		(p->next->spec == SPEC_SECOND) &&
		(p->next->next) && (p->next->next->spec == SPEC_DEFAULT)){
		return 1;
	}
	return 0;
}

char check_write(struct list* p)
{
	if(p == NULL){
		return 0;
	}
	if((strcmp(p->word, ">")) == 0 && (p->spec == SPEC_FIRST) &&
		(p->next) && (p->next->spec == SPEC_DEFAULT)){
		return 1;
	}
	return 0;
}

char check_read(struct list* p)
{
	if(p == NULL){
		return 0;
	}
	if((strcmp(p->word, "<")) == 0 && (p->spec == SPEC_FIRST) &&
		(p->next) && (p->next->spec == SPEC_DEFAULT)){
		return 1;
	}
	return 0;
}

void open_files(struct list* head)
{
	if(head == NULL){
		return;
	}
	while(head->next){
		if(check_append(head->next)){
			struct list* tmp = head->next->next->next;
			int fd = open(tmp->word,
				O_WRONLY|O_CREAT|O_APPEND, S_IRWXU);
			dup2(fd, 1);
			close(fd);
			head->next = tmp->next;
		}
		if(check_write(head->next)){
			struct list* tmp = head->next->next;
			int fd = open(tmp->word,
				O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
			dup2(fd, 1);
			close(fd);
			head->next = tmp->next;
		}
		if(check_read(head->next)){
			struct list* tmp = head->next->next;
			int fd = open(tmp->word, O_RDONLY);
			dup2(fd, 0);
			close(fd);
			head->next = tmp->next;
		}
		head = head->next;
		if(head == NULL){
			return;
		}
	}
}

int execute_pipeline(struct list* p)
{
	int s;
	int i;
	struct list* tail = NULL;
	int last_pid = -1;
	int last_exit_code = 1;
	int fork_counter = 0;
	int fd[4] = {-1,-1,-1,-1};
	int pipe_stat;
	while((pipe_stat = split_pipeline(p, &tail)) != PIPE_EMPTY){
		if(pipe_stat != PIPE_LAST){
			pipe(&(fd[2]));
		}
		if((last_pid = fork()) == 0){
			char** argv;
			dup_pipe(fd);
			close_pipe_fork(fd);
			open_files(p);
			argv = create_argv(p);
			if(strcmp(argv[0], "exit") == 0){
				int exit_code = 0;
				if(argv[1]){
					exit_code = atoi(argv[1]);
				}
				exit(exit_code);
			}
			execvp(argv[0], argv);
			perror("execvp");
			exit(EXIT_FAILURE);
		}
		printf("execute %s\n");
		prepare_fd(fd);
		fork_counter++;
		p = tail;
		tail = NULL;
	}
	for(i = 0; i < fork_counter; i++){
		if(wait(&s) == last_pid){
			last_exit_code = WIFEXITED(s) ? WEXITSTATUS(s) : 1;
		}
	}
	return last_exit_code;
}

int execute_logic(struct list* p)
{
	struct list* tail = NULL;
	int logic_op;
	int exit_code = 0;
	while((logic_op = split_logic(p, &tail)) != LOGIC_EMPTY){
		exit_code = execute_pipeline(p);
		if(logic_op == LOGIC_LAST){
			break;
		}
		if((logic_op == LOGIC_OR) && (exit_code == 0)){
			while(logic_op != LOGIC_EMPTY &&
				logic_op != LOGIC_AND){
				p = tail;
				tail = NULL;
				logic_op = split_logic(p, &tail);
			}
		}else if((logic_op == LOGIC_AND) && (exit_code != 0)){
			while(logic_op != LOGIC_EMPTY &&
				logic_op != LOGIC_OR){
				p = tail;
				tail = NULL;
				logic_op = split_logic(p, &tail);
			}
		}
		p = tail;
		tail = NULL;
	}
	return exit_code;
}

char execute_cd(struct list* p)
{
	if(strcmp(p->word, "cd") == 0){
		if((p->next) && (p->next->spec == SPEC_DEFAULT)){
			chdir(p->next->word);
		}
		return 1;
	}
	return 0;
}


void execute(struct head_list* list)
{
	int pid;
	bool block = check_block(list);
	if(execute_cd(list->first)){
		return;
	}
	if((pid = fork()) == 0){
		int exit_code = execute_logic(list->first);
		exit(exit_code);
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

char need_escape(char c)
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
	struct head_list list;
	list.first = NULL;
	list.last = NULL;
	struct buffer* buf = create_buffer(32);
	int c;
	bool is_quote_d = 0;
	bool is_quote_s = 0;
	bool is_escape = 0;
	bool is_prev_spec = 0;
	while((c = getchar()) != EOF){
		if(is_escape){
			if(!need_escape(c)){
				buf = insert_buffer(buf, '\\');
			}
			if(c != '\n'){
				buf = insert_buffer(buf, c);
			}
			is_escape = 0;
			continue;
		}
		switch(c){
		case '"':
			if(!is_quote_s){
				is_quote_d = !is_quote_d;
			}else{
				buf = insert_buffer(buf, c);
			}
			is_prev_spec = 0;
			break;
		case '\'':
			if(!is_quote_d){
				is_quote_s = !is_quote_s;
			}else{
				buf = insert_buffer(buf, c);
			}
			is_prev_spec = 0;
			break;
		case '\\':
			if(!is_quote_s){
				is_escape = 1;
			}else{
				buf = insert_buffer(buf, c);
			}
			is_prev_spec = 0;
			break;
		case ' ': case '\n': case '>': case '<': case '&': case '|':
			if(is_quote_s || is_quote_d){
				buf = insert_buffer(buf, c);
				continue;
			}
			insert_list(&list, buf, SPEC_DEFAULT);
			if(spec_symbol(c)){
				buf->pos = 1;
				buf->array[0] = c;
				if(is_prev_spec){
					insert_list(&list, buf,
						SPEC_SECOND);
				}else{
					insert_list(&list, buf,
						SPEC_FIRST);
				}
				is_prev_spec = 1;
			}else{
				if(c == '\n'){
					strip_comment(&list);
					if(list.first){
						execute(&list);
					}
					free_list(list.first);
					list.first = NULL;
					list.last = NULL;
				}
				is_prev_spec = 0;
			}
			break;
		default:
			buf = insert_buffer(buf, c);
			is_prev_spec = 0;
		}
	}
	return 0;
}
