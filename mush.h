#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define LINE_LEN 512
#define PIPE_LEN 10
#define ARG_LEN 10
#define PRINT_LINE 128
#define WRITE_END 1
#define READ_END 0
#define RDWR_PERM 0666

/* struct that holds a single stage
 *
 * line - holds the parsed command line, 
 * essentially the same thing except no < or > chars
 *
 * fd_in - set to NULL unless there is an input 
 * redirect, then set to the name of the file
 *
 * fd_out - same as fd_in except for output
 *
 * input - holds the fd, either STDIN or the opened fd_in
 *
 * output - same as fd_in except for output
 *
 * num - the number of arguments in line
 *
 */
typedef struct command {
    char *line[ARG_LEN], *fd_in, *fd_out;
    int input, output, num;
} command;

/* counts the number of stages in a pipeline */
int count_stages(char *line);

/* parses the entire pipeline, making a list of cmd structs that hold each stage */
int parse_stage(char *stage, int stage_num, int total_stages, command **cmd_line);

/* sets the initial fd_in, fd_out, input, and output of a cmd */ 
void default_pipe(int stage_num, command *cmd);

/* parses a stage if containing ls or < > redirects */
int parse_ls(char **stage_saved, command *cmd, 
                int total_stages, int j, int stage_num, char *stage);

/* removes trailing whitespace */
char *trim(char *line);

/* executes a command from a stage */
void execute(command *cmd);

/* opens the file descriptors if they exist from 
 * fd_in and fd_out and puts them into input/output */
int open_fd(command *cmd);

/* executes the cd command */
void execute_cd(command *cmd);

/* the SIGINT handler */
void handler(int signum);

/* blocks SIGINT */
void blocksigint(void);

/* unblocks SIGINT */
void unblocksigint(void);
