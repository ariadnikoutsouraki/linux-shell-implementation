#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS  64
#define MAX_CMDS  64
#define FILE_PERMISSIONS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

// --- Βοηθητικές Συναρτήσεις ---

static char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p) return NULL;
    memcpy(p, s, len);
    return p;
}

struct simple_cmd {
    char *argv[MAX_ARGS];
};

void free_cmds(struct simple_cmd cmds[], int num_cmds) {
    for (int i = 0; i < num_cmds; i++) {
        for (int j = 0; cmds[i].argv[j] != NULL; j++) {
            free(cmds[i].argv[j]);
            cmds[i].argv[j] = NULL;
        }
    }
}

void shift_args(char **args, int start_index, int count_to_remove) {
    int i = start_index;
    while (args[i + count_to_remove] != NULL) {
        args[i] = args[i + count_to_remove];
        i++;
    }
    args[i] = NULL;
}

void fix_leading_redirs(struct simple_cmd *cmd) {
    char *orig[MAX_ARGS];
    char *prog_args[MAX_ARGS];
    char *redir_args[MAX_ARGS];
    int oi = 0, pi = 0, ri = 0;

    while (cmd->argv[oi] && oi < MAX_ARGS-1) {
        orig[oi] = cmd->argv[oi];
        oi++;
    }
    orig[oi] = NULL;

    oi = 0;
    while (orig[oi] &&
          (strcmp(orig[oi], "<") == 0 ||
           strcmp(orig[oi], ">") == 0 ||
           strcmp(orig[oi], ">>") == 0 ||
           strcmp(orig[oi], "2>") == 0 ||
           strcmp(orig[oi], "2>&1") == 0)) {

        redir_args[ri++] = orig[oi++];
        if (orig[oi]) {
            redir_args[ri++] = orig[oi++];
        }
    }
    redir_args[ri] = NULL;

    while (orig[oi] && pi < MAX_ARGS-1) {
        prog_args[pi++] = orig[oi++];
    }
    prog_args[pi] = NULL;

    if (prog_args[0] == NULL) return;

    int ai = 0;
    for (int j = 0; prog_args[j] && ai < MAX_ARGS-1; j++) cmd->argv[ai++] = prog_args[j];
    for (int j = 0; redir_args[j] && ai < MAX_ARGS-1; j++) cmd->argv[ai++] = redir_args[j];
    cmd->argv[ai] = NULL;
}

// pipes, quotes ΚΑΙ redirections κολλημένα (ls>file)
int parse_pipeline(char *input, struct simple_cmd cmds[], int *num_cmds) {
    int cmd_i = 0;
    int arg_i = 0;
    int in_single = 0, in_double = 0;
    char *p = input;
    char token_buf[1024];
    int tlen = 0;

    // Καθαρισμός αρχικού πίνακα
    for (int i = 0; i < MAX_CMDS; i++) {
        for (int j = 0; j < MAX_ARGS; j++) cmds[i].argv[j] = NULL;
    }

    while (*p) {
        char c = *p;

        // Quotes
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            p++; continue;
        } 
        else if (c == '"' && !in_single) {
            in_double = !in_double;
            p++; continue;
        }

        // Spaces/Tabs 
        if (!in_single && !in_double && (c == ' ' || c == '\t')) {
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }
            p++;
        }
        //  Pipe 
        else if (!in_single && !in_double && c == '|') {
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }
            cmds[cmd_i].argv[arg_i] = NULL;
            cmd_i++;
            arg_i = 0;
            p++;
        }
        
        else if (!in_single && !in_double && (c == '>' || c == '<')) {
            
            // --- ΕΙΔΙΚΗ ΠΕΡΙΠΤΩΣΗ: 2> και 2>&1 ---
            // Αν ο buffer έχει ΜΟΝΟ το '2' και πετύχουμε '>', τότε είναι stderr redirection
            if (c == '>' && tlen == 1 && token_buf[0] == '2') {
                token_buf[tlen++] = c; // Προσθέτουμε το '>' (γίνεται "2>")
                p++; // Προχωράμε

                // Ελέγχουμε αν ακολουθεί &1 (για το 2>&1)
                if (*p == '&' && *(p+1) == '1') {
                    token_buf[tlen++] = *p;     // '&'
                    token_buf[tlen++] = *(p+1); // '1'
                    p += 2; // Προχωράμε 2 θέσεις
                }
                
                // Αποθηκεύουμε το token ("2>" ή "2>&1")
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
                continue; 
            }

            // Αν είχαμε άλλη λέξη πριν (π.χ. "ls"), την κλείνουμε
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }

            // Έλεγχος για >>
            if (c == '>' && *(p+1) == '>') {
                cmds[cmd_i].argv[arg_i++] = my_strdup(">>");
                p += 2;
            } else {
                char tmp[2] = {c, '\0'}; 
                cmds[cmd_i].argv[arg_i++] = my_strdup(tmp);
                p++;
            }
        }
        // Newlines
        else if (c == '\n' || c == '\r') {
            p++;
        }
        //  Κανονικοί χαρακτήρες
        else {
            if (tlen < (int)sizeof(token_buf) - 1) {
                token_buf[tlen++] = c;
            }
            p++;
        }
    }

    // Τελευταία λέξη
    if (tlen > 0) {
        token_buf[tlen] = '\0';
        cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
    }
    cmds[cmd_i].argv[arg_i] = NULL;
    *num_cmds = cmd_i + 1;

    if (cmds[0].argv[0] == NULL) return 0;
    fix_leading_redirs(&cmds[0]);
    return 1;
}

int handle_redirection(char** args) {
    int i = 0;
    int fd;
    char* filename = NULL;
    int redirect_target = -1; 

    // Χειρισμός 2>&1
    for (int j = 0; args[j]; j++) {
        if (strcmp(args[j], "2>&1") == 0) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
                perror("tiny_shell: dup2 2>&1 failed");
                return -1;
            }
            shift_args(args, j, 1);
            j--; 
        }
    }

    while (args[i] != NULL) {
        redirect_target = -1;
        
        if (strcmp(args[i], ">>") == 0)      redirect_target = 2; 
        else if (strcmp(args[i], "2>") == 0) redirect_target = 3; 
        else if (strcmp(args[i], "<") == 0)  redirect_target = 0; 
        else if (strcmp(args[i], ">") == 0)  redirect_target = 1; 

        if (redirect_target != -1) {
            if (args[i+1] == NULL) {
                fprintf(stderr, "tiny_shell: syntax error: missing file for redirection\n");
                return -1;
            }
            filename = args[i+1];
            
            // Σβήνουμε τον τελεστή και το αρχείο από τα ορίσματα
            shift_args(args, i, 2);
            
            if (redirect_target == 0) { // <
                fd = open(filename, O_RDONLY);
                if (fd == -1) { perror("open input"); return -1; }
                if (dup2(fd, STDIN_FILENO) == -1) { perror("dup2 input"); close(fd); return -1; }
                close(fd);
            } else { // >, >>, 2>
                int flags = (redirect_target == 2) ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                int target_fd = (redirect_target == 3) ? STDERR_FILENO : STDOUT_FILENO;
                
                fd = open(filename, flags, FILE_PERMISSIONS);
                if (fd == -1) { perror("open output"); return -1; }
                if (dup2(fd, target_fd) == -1) { perror("dup2 output"); close(fd); return -1; }
                close(fd);
            }
            continue; 
        }
        i++;
    }
    return 0;
}

int main() {
    while (1) {
        char input[MAX_INPUT];

        printf("tiny_shell> ");
        fflush(stdout);

        if (fgets(input, MAX_INPUT , stdin) == NULL) {
            if (feof(stdin)) printf("\nExiting shell on EOF.\n");
            else perror("fgets failed");
            break;
        }

        input[strcspn(input,"\n")] = '\0';
        if (input[0] == '\0') continue;

        if (strcmp(input, "exit") == 0) {
            printf("exiting tinyshell\n");
            break;
        }

        struct simple_cmd cmds[MAX_CMDS];
        int num_cmds = 0;
        
        // Parsing
        if (!parse_pipeline(input, cmds, &num_cmds)) {
            continue;
        }

        // CD Command
        if (strcmp(cmds[0].argv[0], "cd") == 0) {
            if (cmds[0].argv[1] == NULL) {
                if (chdir(getenv("HOME")) != 0) perror("cd failed");
            } else {
                if (chdir(cmds[0].argv[1]) != 0) perror("cd failed");
            }
            free_cmds(cmds, num_cmds);
            continue;
        }

        int pipefd[2];
        int prev_pipe_read_fd = -1;
        pid_t pids[MAX_CMDS];
        int i;

        for (i = 0; i < num_cmds; i++) {
            if (i < num_cmds - 1) {
                if (pipe(pipefd) == -1) {
                    perror("pipe failed");
                    break;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                break;
            }

            pids[i] = pid;

            if (pid == 0) { // Child
                if (prev_pipe_read_fd != -1) {
                    dup2(prev_pipe_read_fd, STDIN_FILENO);
                    close(prev_pipe_read_fd);
                }
                if (i < num_cmds - 1) {
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                    close(pipefd[0]);
                }

                char **args = cmds[i].argv;
                if (handle_redirection(args) != 0) exit(EXIT_FAILURE);

                
            {
                execvp(args[0],args);
            
            
                if (errno == EACCES) {
                // Αν λέει Permission denied αλλά το αρχείο δεν υπάρχει, άλλαξε το λάθος
                if (access(args[0], F_OK) != 0) {
                    errno = ENOENT; 
                }
            }
    
            perror("execvp failed");
            exit(EXIT_FAILURE);
            }
            } 
            else { // Parent
                if (prev_pipe_read_fd != -1) close(prev_pipe_read_fd);
                if (i < num_cmds - 1) {
                    close(pipefd[1]);
                    prev_pipe_read_fd = pipefd[0];
                }
            }
        }

        // Wait for children
        int status;
        for (int j = 0; j < i; j++) {
            waitpid(pids[j], &status, 0);
            
            if (WIFEXITED(status)) {
                 printf("Command %d exited with status %d\n", j+1, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                 printf("Command %d terminated by signal %d\n", j+1, WTERMSIG(status));
            }
        }

        free_cmds(cmds, num_cmds);
    }

    return 0;
}