#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef int (*builtin_func)(char **args, int arg_count);

#define BUILTIN_COMMAND_SUCCESS 0
#define BUILTIN_COMMAND_ERROR 1

enum built_in_command_type {
    BUILTIN_COMMAND_TYPE_CD = 1,
    BUILTIN_COMMAND_TYPE_EXIT,
    BUILTIN_COMMAND_TYPE_PWD,
    BUILTIN_COMMAND_TYPE_TRUE,
    BUILTIN_COMMAND_TYPE_FALSE,
    BUILTIN_COMMAND_TYPE_ECHO,
    BUILTIN_COMMAND_TYPE_COUNT,
    /*MUST BE THE LAST*/
    BUILTIN_COMMAND_TYPE_NONE = 0,
};

static int
builtin_cd(char **args, int arg_count)
{
    if (arg_count > 1) {
        fprintf(stderr, "cd: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    } 
    if (arg_count < 1) {
        fprintf(stderr, "cd: not enough arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }
    if (chdir(args[1]) != 0) {
        perror("cd");
        return BUILTIN_COMMAND_ERROR;
    }

    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_exit(char **args, int arg_count)
{
    if (arg_count > 1) {
        fprintf(stderr, "exit: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }
    int exit_code = 0;
    if (arg_count == 0) {
        exit_code = BUILTIN_COMMAND_SUCCESS;
    } else {
        exit_code = atoi(args[1]);
    }

    return exit_code;
}

static int
builtin_pwd(char **args, int arg_count)
{
    (void)args;
    if (arg_count > 0) {
        fprintf(stderr, "pwd: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
        return BUILTIN_COMMAND_ERROR;
    }
    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_true(char **args, int arg_count)
{
    (void)args;
    (void)arg_count;
    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_false(char **args, int arg_count)
{
    (void)args;
    (void)arg_count;
    return BUILTIN_COMMAND_ERROR;
}

static int
builtin_echo(char **args, int arg_count)
{
    if (arg_count == 0) {
        printf("\n");
        fflush(stdout);
        return BUILTIN_COMMAND_SUCCESS;
    }
    else {
        for (int i = 1; i <= arg_count; i++)
        {
            if (i > 1) {
                printf(" ");
            }
            printf("%s", args[i]);
        }
        printf("\n");
        fflush(stdout);
        return BUILTIN_COMMAND_SUCCESS;
    }
    fflush(stdout);
    return BUILTIN_COMMAND_ERROR;
}

/*builtin_table[0] = NULL; because enum start from 1*/
builtin_func builtin_table[] = {
    NULL,           
    builtin_cd,     
    builtin_exit,   
    builtin_pwd,    
    builtin_true,   
    builtin_false,  
    builtin_echo    
};

static enum built_in_command_type
is_builtin_command(const char *exe)
{
    if (strcmp(exe, "cd") == 0) {
        return BUILTIN_COMMAND_TYPE_CD;
    } 
    if (strcmp(exe, "exit") == 0) {
        return BUILTIN_COMMAND_TYPE_EXIT;
    } 
    if (strcmp(exe, "pwd") == 0) {
        return BUILTIN_COMMAND_TYPE_PWD;
    } 
    if (strcmp(exe, "true") == 0) {
        return BUILTIN_COMMAND_TYPE_TRUE;
    } 
    if (strcmp(exe, "false") == 0) {
        return BUILTIN_COMMAND_TYPE_FALSE;
    } 
    if (strcmp(exe, "echo") == 0) {
        return BUILTIN_COMMAND_TYPE_ECHO;
    }

    return BUILTIN_COMMAND_TYPE_NONE;
}

struct pq_node {
	pid_t pid;
	struct pq_node *next;
};

struct pqueue {
	struct pq_node *head;
	struct pq_node *tail;
};

static struct pq_node *new_node(pid_t pid) {
	struct pq_node *res = malloc(sizeof(struct pq_node));
	res->pid = pid;
	res->next = NULL;
	return res;
}

static struct pqueue *new_pqueue() {
	struct pqueue *res = malloc(sizeof(struct pqueue));
	res->head = NULL;
	res->tail = NULL;
	return res;
}

static void push_pqueue(struct pqueue *pq, pid_t pid) {
	struct pq_node *node = new_node(pid);
	if (!pq->head) {
		pq->head = node;
		pq->tail = node;
	} else {
		pq->tail->next = node;
		pq->tail = pq->tail->next;
	}
}

static int pop_queue(struct pqueue *pq) {
	int status = -1;
	if (pq->head) {
		struct pq_node *tmp = pq->head;
		pq->head = pq->head->next;
		waitpid(tmp->pid, &status, 0);
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
		}
		free(tmp);
	}
	return status;
}

static void free_pqueue(struct pqueue *pq) {
	while (pq->head) {
		struct pq_node *tmp = pq->head;
		pq->head = pq->head->next;
		free(tmp);
	}
}

static int wait_pqueue(struct pqueue *pq) {
	int exitcode = 0;
	while(pq->head) {
		exitcode = pop_queue(pq);
	}
	free_pqueue(pq);
	return exitcode;
}

void cleanup_zombies(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static int 
execute_command_line(struct command_line *line, struct parser *p)
{
    int out_file = -1;
    
	assert(line != NULL);
	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		//do nothing
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		out_file = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_file == -1) perror("open");
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		out_file = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out_file == -1) perror("open");
	} else {
		assert(false);
	}
	const struct expr *e = line->head;
    enum built_in_command_type type = BUILTIN_COMMAND_TYPE_NONE;

    int has_prev = 0;
    int will_pipe = 0;

    int prev_pipe[2] = {0};
    
    struct pqueue *pq = new_pqueue();

    int save_out = dup(STDOUT_FILENO);
    int save_in  = dup(STDIN_FILENO);
    while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {

            int next_pipe[2] = {0};
            will_pipe = 0;
            if(e->next != NULL){
                if (e->next->type == EXPR_TYPE_PIPE) {
                    will_pipe = 1;
                }
            }
            if (will_pipe) {
                pipe(next_pipe);
            }
            
            if (will_pipe == 0 && out_file != -1) {
                dup2(out_file, STDOUT_FILENO);
            }
            type = is_builtin_command(e->cmd.exe);
            if (type != BUILTIN_COMMAND_TYPE_NONE) {
                if (will_pipe || has_prev) {
                    // if (will_pipe == 0 && type == BUILTIN_COMMAND_TYPE_EXIT) {
                        // int res = builtin_table[type](e->cmd.args, e->cmd.arg_count);
                        // parser_delete(p);
                        // free_pqueue(pq);
                        // free(pq);
                        // command_line_delete(line);
                        // exit(res);
                    // }
                    pid_t pid = fork();
                    if (pid == 0) {
                        if (has_prev)  dup2(prev_pipe[0], STDIN_FILENO);
                        if (will_pipe) dup2(next_pipe[1], STDOUT_FILENO);

                        if (has_prev)  {
                            close(prev_pipe[1]);
                            close(prev_pipe[0]);
                        }

                        if (will_pipe) {
                            close(next_pipe[1]);
                            close(next_pipe[0]);
                        }
                        int res = builtin_table[type](e->cmd.args, e->cmd.arg_count);
                        if (type == BUILTIN_COMMAND_TYPE_EXIT) {
                            parser_delete(p);
                            free_pqueue(pq);
                            free(pq);
                            command_line_delete(line);
                        }
                        exit(res);
                    } else {
                        push_pqueue(pq, pid);
                    }
                } else {
                    int res = builtin_table[type](e->cmd.args, e->cmd.arg_count);
                    if (type == BUILTIN_COMMAND_TYPE_EXIT) {
                        command_line_delete(line);
                        parser_delete(p);
                        exit(res);
                    }
                }
                
            } 
            else {
                pid_t pid = fork();
                if (pid == 0) {
                    if (has_prev)  {
                        dup2(prev_pipe[0], STDIN_FILENO);
                    }

                    if (will_pipe) {
                        dup2(next_pipe[1], STDOUT_FILENO);
                    }

                    if (has_prev)  {
                        close(prev_pipe[1]);
                        close(prev_pipe[0]);
                    }

                    if (will_pipe) {
                        close(next_pipe[1]);
                        close(next_pipe[0]);
                    }
                    
                    if (execvp(e->cmd.exe, e->cmd.args) == -1)
                    {
                        perror("execvp");
                        fprintf(stderr, "e->type: %d\n", e->type);
                        fprintf(stderr, "\ne->cmd.exe: %s\n", e->cmd.exe);
                        fprintf(stderr, "e->cmd.arg_count: %d\n", e->cmd.arg_count);
                        fprintf(stderr, "e->cmd.args:\n");
                        u_int32_t i = 0;
                        
                        while (i <= e->cmd.arg_count && e->cmd.args[i] != NULL) {
                            fprintf(stderr, "%d: %s \n", i, e->cmd.args[i]);
                            i++;
                        }
                        fprintf(stderr, "\n\n");
                    }
                    assert(false);
                } else {
                    push_pqueue(pq, pid);
                }
            }
        if (has_prev) {
            close(prev_pipe[0]);
            close(prev_pipe[1]);
        }

        if (will_pipe) {                     
            prev_pipe[0] = next_pipe[0];
            prev_pipe[1] = next_pipe[1];
            has_prev = 1;
		}                               
            
        } else if (e->type == EXPR_TYPE_PIPE) {
			//printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
            wait_pqueue(pq);
		} else if (e->type == EXPR_TYPE_OR) {
            wait_pqueue(pq);
		} else {
			assert(false);
		}
		e = e->next;
	}
    int res = wait_pqueue(pq);
    free(pq);
    dup2(save_out, STDOUT_FILENO);
    dup2(save_in, STDIN_FILENO);

    if (out_file != -1) {
        close(out_file);
    }
    return res;
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
    int res = 0;

	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			res = execute_command_line(line, p);
			command_line_delete(line);
		}
	}
	parser_delete(p);
    signal(SIGCHLD, cleanup_zombies);
	return res;
}
