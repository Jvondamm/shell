#include "mush.h"

/* runs the 8-P prompt or script, quits on 'q' or ^D */
int main(int argc, char *argv[]) {
    int         total_stages, i, error, running = 1, num = 0;
    char        *line, *stage;
    const char  or[2] = "|";
    char        *saveptr;
    FILE        *script;
    command     *cmd_list[PIPE_LEN];
    struct      sigaction sa;
    sigset_t    set;
    int         old[2], next[2];
    pid_t       child, parent;

    parent = getpid();
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (-1 == sigaction(SIGINT, &sa, NULL)) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    /* mallocs mem for the command */
    line = malloc(sizeof(char) * LINE_LEN);
    if (!line) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    /* opens script file if it exists */
    if (argc == 2 || argc == 1) {
        if (argc == 2) {
            if (!(script = fopen(argv[1], "r"))) {
                perror(argv[1]);
                exit(EXIT_FAILURE);
            }
        }
    } else {
        fprintf(stderr, "usage: ./mush [script]\n");
        exit(EXIT_FAILURE);
    }

    /* will run until ^D or 'q' */
    while (running) {
        
        error = 0;

        /* create initial pipes */
        if (-1 == pipe(old) || -1 == pipe(next)) {
            perror("pipe error");
            exit(EXIT_FAILURE);
        }

        /* intilize cmd_list to nulls */
        for (i = 0; i < PIPE_LEN; i++) {
            cmd_list[i] = NULL;
        }

        if (argc == 1) {
            
            /* mush prompt */
            if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
                printf("8-P ");
            }
    
            if (NULL == fgets(line, LINE_LEN, stdin)) {
                if (feof(stdin)) {
                    break;
                }
                continue;
            }
        } else if (argc == 2) {

            /* script */
            if (NULL == fgets(line, LINE_LEN, script)) {
                exit(EXIT_SUCCESS);
            }
        }
    
        /* remove trailing whitespace */
        trim(line);

        /* quit the program */
        if (strcmp(line, "q") == 0) {
            running = 0;
            break;
        }

        /* count number of stages and make sure they do not exceed the limit */ 
        total_stages = count_stages(line);
        if (total_stages > 10) {
            fprintf(stderr, "pipeline too deep\n");
            error--;
        }

        /* get the first stage */
        stage = strtok_r(line, or, &saveptr);

        /* loop through next stages, ensuring valid */
        for (i = 0; i < total_stages; i++) {
            if (stage == NULL) {
                fprintf(stderr, "invalid null command\n");
                exit(EXIT_FAILURE);
            }
            /* parse each stage, putting stage into cmd struct */
            if (0 > parse_stage(stage, i, total_stages, cmd_list)) {
                error--;
            }

            /* get the next stage */
            stage = strtok_r(NULL, or, &saveptr);
        }
   
        num = 0;
        if (error == 0) {

            /* goes through each stage */
            for (i = 0; i < total_stages; i++) {

                /* checks for cd command, only allows it to be run in
                 * stage 0 */
                if (strcmp(cmd_list[i]->line[0], "cd") == 0) {
                    if (i != 0) {
                        fprintf(stderr, "can only cd in stage 0\n");
                        break;
                    } else {
                        execute_cd(cmd_list[i]);
                        break;
                    }
                } else {
                
                    /* the count of children */
                    num++;

                    /* block SIGINT */
                    blocksigint();

                    /* fork child */
                    child = fork();

                    if (child < 0) {
                        perror("child");
                        exit(EXIT_FAILURE);
                    } else if (child == 0) {

                        /* in child, unblock the SIGINT */
                        unblocksigint();

                        /* 1 stage case */
                        if (total_stages == 1) {
                        
                            /* open file descriptors if they exist, else
                             * we don't have to worry about piping */
                            if(open_fd(cmd_list[i]) == 1) {
                                if (-1 == dup2(cmd_list[i]->input, STDIN_FILENO)) {
                                    perror("dup2 single stage input");
                                    exit(EXIT_FAILURE);
                                }

                                if (-1 == dup2(cmd_list[i]->output, STDOUT_FILENO)) {
                                    perror("dup2 single stage output");
                                    exit(EXIT_FAILURE);
                                }
                            }

                        /* multi-stage case */
                        } else if (total_stages > 1) {
                            
                            /* stage 0 */
                            if (i == 0) {
                                if (-1 == dup2(old[WRITE_END], STDOUT_FILENO)) {
                                    perror("dup2 stage 0 old");
                                    exit(EXIT_FAILURE);
                                }
                            
                            /* stage 1 - total stages - 1 */
                            } else if (i < total_stages - 1) {
                                if (-1 == dup2(old[READ_END], STDIN_FILENO)) {
                                    perror("dup2 middle stage old");
                                    exit(EXIT_FAILURE);
                                }

                                if (-1 == dup2(next[WRITE_END], STDOUT_FILENO )) {
                                    perror("dup2 middle stage next");
                                    exit(EXIT_FAILURE);
                                }
                            
                            /* last stage */
                            } else if (i == total_stages - 1) {
                                
                                /* can change output, so if a FD output is specified,
                                 * open it */
                                if (open_fd(cmd_list[i]) == 1) {
                                    if (-1 == dup2(cmd_list[i]->output, STDOUT_FILENO)) {
                                        perror("dup2 last stage next");
                                        exit(EXIT_FAILURE);
                                    }   
                                } 
                                
                                /* set input piping */
                                if (total_stages == 2) {
                                    if (-1 == dup2(old[READ_END], STDIN_FILENO)) {
                                        perror("dup2 last stage next");
                                        exit(EXIT_FAILURE);
                                    }   
                                } else {
                                    if (-1 == dup2(next[READ_END], STDIN_FILENO)) {
                                        perror("dup2 last stage next");
                                        exit(EXIT_FAILURE);
                                    }   
                                }
                            }
                        }
                
                        /* close all pipes */
                        error = close(old[READ_END]);
                        error = close(old[WRITE_END]);
                        error = close(next[READ_END]);
                        error = close(next[WRITE_END]);
                        
                        /* error check close() */
                        if (error == -1) {
                            perror("close");
                            exit(EXIT_FAILURE);
                        }

                        /* execute commanad and eit child */
                        execute(cmd_list[i]);
                        exit(EXIT_SUCCESS);

                    } else if (getpid() == parent) {
                    
                        /* unblock sigint */
                        unblocksigint();
                    
                        /* close old pipe and get from new pipe
                         * only if we've filled the pipes */
                        if (i > 3) { 
                            error = 0;
                            error = close(old[READ_END]);
                            error = close(old[WRITE_END]);

                            if (error == -1) {
                                perror("close");
                                exit(EXIT_FAILURE);
                            }

                            old[READ_END] = next[READ_END];
                            old[WRITE_END] = next[WRITE_END];
                            if (pipe(next)) {
                                perror("next");
                                exit(EXIT_FAILURE);
                            }
                        }
                        
                    }
                }
            }

            /* clean up leftover pipes */
            error = 0;

            error = close(old[READ_END]);
            error =  close(old[WRITE_END]);
            error = close(next[READ_END]);
            error = close(next[WRITE_END]);
            
            if (error == -1) {
                perror("close");
                exit(EXIT_FAILURE);
            }

            /* wait for all children to exit */
            while (num--) {
                wait(NULL); 
            }

            /* free all cmd structs */
            for (i = 0; i < total_stages; i++) {
                free(cmd_list[i]);
            }

        }

        /* flush stdout */
        fflush(stdout);
    }

    if (argc == 2) {
        fclose(script);
    }

    /* free */
    free(line);

    unblocksigint();
    return 0;
}

/* returns the number of stages */
int count_stages(char *line) {
    int i;
    int count = 0;

    /* increases count if '|' is found */
    for (i = 0; i < strlen(line); i++) {
        if (line[i] == '|') {
            count++;
        }
    }
    return count + 1;
}

/* removes trailing whitespace */
char *trim(char *line) {
    char *ptr;

    /* get end of line index */
    ptr = line + strlen(line) - 1;

    /* decrements the pointer if space found */
    while (ptr > line && isspace((unsigned char)*ptr)) {
        ptr--;
    }
    ptr[1] = '\0';
    
    return line;
}

/* exeute the command from a single stage */
void execute(command *cmd) {
     
    char *executable[PRINT_LINE];
    int i;
   
    /* sets all to nulls initially */
    for (i = 0; i < PRINT_LINE; i++) {
        executable[i] = NULL;
    }

    /* for each word in the cmd, copy tro executable */
    for (i = 0; i <= cmd->num; i++) {
        executable[i] = cmd->line[i];
        if (strcmp(executable[i], "<") == 0 ||
            strcmp(executable[i], ">") == 0) {
            /* to skip the < and the following input/output name */
            i+=2;
        }
    }
    
    /* run using execvp */
    if (execvp(cmd->line[0], executable) != 0) {
        perror(cmd->line[0]);
        exit(EXIT_FAILURE);
    }
}

/* opens input/output file descriptors for a single stage if they exist */
int open_fd(command *cmd) {
    int opened = 0;
    
    /* opens output FD if it exists */
    if (cmd->fd_out != NULL) {
        opened = 1;
        cmd->output = open(cmd->fd_out, O_RDWR | O_CREAT | O_TRUNC, RDWR_PERM);
        if (cmd->output < 0) {
            perror("fd_out open error");
            exit(EXIT_FAILURE);
        }

    }
    
    /* opens input FD if it exists */
    if (cmd->fd_in != NULL) {
        opened = 1;
        cmd->input = open(cmd->fd_in, O_RDONLY);
        if (cmd->input < 0) {
            perror("fd_in open error");
            exit(EXIT_FAILURE);
        }
    }
    return opened;
}

/* runs the cd command */
void execute_cd(command *cmd) {
    /* if missing directory to change to */
    if (cmd->num != 1) {
        fprintf(stderr, "no directory specified\n");
    } else {
        /* else change to that directory */
        if (chdir(cmd->line[1]) != 0) {
            perror(cmd->line[1]);
        }
    }
}


/* ensures a stage is valid, then adds the command and its pipes to the struct cmd */
int parse_stage(char *stage, int stage_num, int total_stages, command **cmd_line) {
    char            *saveptr, *temp;
    const char      space[2] = " ";
    int             i = 0, j = 0;
    char            *stage_saved[ARG_LEN];
    command         *cmd = (command*)malloc(sizeof(command));

    /* break up command on space */
    cmd->line[i] = strtok_r(stage, space, &saveptr);

    /* save original command to stage_saved */
    stage_saved[i] = cmd->line[i];

    while(cmd->line[i] != NULL) {
        
        
        /* counter that increments every time for stage_saved */
        j++;

        /* get the next word separated by space */
        temp = strtok_r(NULL, space, &saveptr);

        stage_saved[j] = temp;
        
        /* break upon NULL return */
        if (temp == NULL) {
            cmd->line[i + 1] = temp;
            break;
        }

        /* do not copy item to cmd->line if it is a < or > char
         * but always copy to stage_saved for reference */
        if (temp != NULL && 
            (strcmp(temp, "<") == 0 || strcmp(temp, ">") == 0)) {
            j++;
            temp = strtok_r(NULL, space, &saveptr);
            stage_saved[j] = temp;
        } else {
            i++;
            cmd->line[i] = temp;
        }
        
    }
    cmd->num = i;

    /* ensures that a stage has no more than 10 args, 
     * (index of num starts at 0) */
    if (cmd->num > ARG_LEN - 1) {
        fprintf(stderr, "%s: too many arguments\n", stage);
        return -1;
        
    }

    /* set default input/output pipes for the cmd */
    default_pipe(stage_num, cmd);

    /* parses ls and ensures it is syntactically correct */
    if (0 > parse_ls(stage_saved, cmd, total_stages, j, stage_num, stage)) {
        return -1;
    }
    
    /* set list index to point to struct cmd */
    cmd_line[stage_num] = cmd;
    return 0;
}

/* sets a stage to default stdin/stdout piping for a single stage */
void default_pipe(int stage_num, command *cmd) {
    cmd->output = STDOUT_FILENO;
    cmd->fd_out = NULL;
    cmd->input = STDIN_FILENO;
    cmd->fd_in = NULL;
}

/* parses a command if it has ls or < >, raises errors if any are found */
int parse_ls(char **stage_saved, 
                command *cmd, 
                int total_stages, 
                int j, 
                int stage_num, 
                char *stage) 
{
    
    int i, in = 0, out = 0;

    /* loops through single stage */
    for (i = 0; i < j; i++) {
        
        /* case 1, input: "<" */
        if (strcmp(stage_saved[i],"<") == 0) {

            /* can't have ls in second or later stage */
            if (stage_num != 0) {
                fprintf(stderr, "%s: ambiguous input\n", stage);
                return -1;
            }

            /* keep track of how many "<" chars */
            in++;

            /* check to see if there is a file after the "<",
             * if not, error, if there is, make it the input */
            if (i + 1 > j) {
                fprintf(stderr, "%s: no input redirect found\n", stage);
                return -1;
            } else if(strcmp(stage_saved[i + 1], "<") == 0 || 
                    strcmp(stage_saved[i + 1], ">") == 0) {
                fprintf(stderr, "%s: bad input redirection\n", stage);
                return -1;
            } else {
                cmd->fd_in = stage_saved[i + 1];
                i++;
            }
            

        /* case 2, output: ">" */
        /* copy of input just with output */
        } else if (strcmp(stage_saved[i],">") == 0) {
            if ( ((stage_num < total_stages - 1) && (stage_num > 0)) || 
                    (stage_num == 0 && total_stages > 1) ) {
                fprintf(stderr, "%s: ambiguous output\n", stage);
                return -1;
            }
            
            out++;

            if (i + 1 > j) {
                fprintf(stderr, "%s: no output redirect found\n", stage);
                return -1;
            } else if(strcmp(stage_saved[i + 1], "<") == 0 ||
                    strcmp(stage_saved[i + 1], ">") == 0) {
                fprintf(stderr, "%s: bad output redirection\n", stage);
                return -1;
            } else {
                cmd->fd_out = stage_saved[i + 1];
                i++;
            }
            
        }
    }

    /* if more than 1 ">" or "<" is found */
    if (in > 1) {
        fprintf(stderr, "%s: bad input redirection\n", stage);
        return -1;
    } else if (out > 1) {
        fprintf(stderr, "%s: bad output redirection\n", stage);
        return -1;
    }
    return 0;
}       

/* handles the SIGINT */
void handler(int signum) {
    wait(NULL);
    printf("\n");
}

/* blocks sigint */
void blocksigint(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    if ( -1 == sigprocmask(SIG_BLOCK, &set, NULL)) {
            perror("sigprocmask");
            exit(EXIT_FAILURE);
    }
}

/* unblocks sigint */
void unblocksigint(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    if ( -1 == sigprocmask(SIG_UNBLOCK, &set, NULL)) {
            perror("sigprocmask");
            exit(EXIT_FAILURE);
    
    }
}
