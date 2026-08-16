#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>

// --- Ρυθμίσεις ---
#define MAX_INPUT 1024
#define MAX_ARGS  64
#define MAX_CMDS  64
#define MAX_JOBS  16  
#define FILE_PERMISSIONS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

// --- Καταστάσεις Job ---
#define UNDEF 0
#define FG 1
#define BG 2
#define STOPPED 3

static pid_t shell_pgid;
static int   shell_terminal;
static int   shell_is_interactive;
static struct termios shell_tmodes;

// Πίνακας για να θυμόμαστε τα PIDs του τρέχοντος pipeline
static pid_t current_pipeline_pids[MAX_CMDS]; 

struct job_t {
    pid_t pgid;      
    int jid;         
    int state;       
    char cmdline[MAX_INPUT]; 
};

struct job_t jobs[MAX_JOBS]; 
int next_jid = 1;


void clearjob(struct job_t *job) {
    job->pgid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

void initjobs(struct job_t *jobs) {
    for (int i = 0; i < MAX_JOBS; i++)
        clearjob(&jobs[i]);
}

int addjob(struct job_t *jobs, pid_t pgid, int state, char *cmdline) {
    if (pgid < 1) return 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pgid == 0) {
            jobs[i].pgid = pgid;
            jobs[i].state = state;
            jobs[i].jid = next_jid++;
            strcpy(jobs[i].cmdline, cmdline);
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

int deletejob(struct job_t *jobs, pid_t pgid) {
    if (pgid < 1) return 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pgid == pgid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

struct job_t *getjobpid(struct job_t *jobs, pid_t pgid) {
    if (pgid < 1) return NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].pgid == pgid) return &jobs[i];
    return NULL;
}

struct job_t *getjobjid(struct job_t *jobs, int jid) {
    if (jid < 1) return NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].jid == jid) return &jobs[i];
    return NULL;
}

// --- Parsing & String Utilities ---

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
    for (int j = 0; prog_args[j] && ai < MAX_ARGS-1; j++)
        cmd->argv[ai++] = prog_args[j];
    for (int j = 0; redir_args[j] && ai < MAX_ARGS-1; j++)
        cmd->argv[ai++] = redir_args[j];
    cmd->argv[ai] = NULL;
}

int parse_pipeline(char *input, struct simple_cmd cmds[], int *num_cmds, int *bg) {
    int cmd_i = 0;
    int arg_i = 0;
    int in_single = 0, in_double = 0;
    char *p = input;
    char token_buf[1024];
    int tlen = 0;

    *bg = 0;

    for (int i = 0; i < MAX_CMDS; i++) {
        for (int j = 0; j < MAX_ARGS; j++) cmds[i].argv[j] = NULL;
    }

    while (*p) {
        char c = *p;

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            p++; continue;
        } 
        else if (c == '"' && !in_single) {
            in_double = !in_double;
            p++; continue;
        }

        if (!in_single && !in_double && (c == ' ' || c == '\t')) {
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }
            p++;
        }
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
            if (c == '>' && tlen == 1 && token_buf[0] == '2') {
                token_buf[tlen++] = c;
                p++;
                if (*p == '&' && *(p+1) == '1') {
                    token_buf[tlen++] = *p;
                    token_buf[tlen++] = *(p+1);
                    p += 2;
                }
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
                continue; 
            }
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }
            if (c == '>' && *(p+1) == '>') {
                cmds[cmd_i].argv[arg_i++] = my_strdup(">>");
                p += 2;
            } else {
                char tmp[2] = {c, '\0'}; 
                cmds[cmd_i].argv[arg_i++] = my_strdup(tmp);
                p++;
            }
        }
        else if (c == '\n' || c == '\r') {
            p++;
        }
        else if (!in_single && !in_double && c == '&') {
            if (tlen > 0) {
                token_buf[tlen] = '\0';
                cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
                tlen = 0;
            }
            cmds[cmd_i].argv[arg_i++] = my_strdup("&");
            p++;
        }
        else {
            if (tlen < (int)sizeof(token_buf) - 1) {
                token_buf[tlen++] = c;
            }
            p++;
        }
    }

    if (tlen > 0) {
        token_buf[tlen] = '\0';
        cmds[cmd_i].argv[arg_i++] = my_strdup(token_buf);
    }
    cmds[cmd_i].argv[arg_i] = NULL;
    *num_cmds = cmd_i + 1;

    if (cmds[cmd_i].argv[0] != NULL) {
        int last = 0;
        while (cmds[cmd_i].argv[last] != NULL) last++;
        if (last > 0 && strcmp(cmds[cmd_i].argv[last-1], "&") == 0) {
            free(cmds[cmd_i].argv[last-1]);
            cmds[cmd_i].argv[last-1] = NULL;
            *bg = 1;
        }
    }

    if (cmds[0].argv[0] == NULL) return 0;
    fix_leading_redirs(&cmds[0]);
    return 1;
}

int handle_redirection(char** args) {
    int i = 0;
    int fd;
    char* filename = NULL;
    int redirect_target = -1; 

    for (int j = 0; args[j]; j++) {
        if (strcmp(args[j], "2>&1") == 0) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
                perror("tiny_shell: redirection 2>&1 failed");
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
            shift_args(args, i, 2);
            
            if (redirect_target == 0) { 
                fd = open(filename, O_RDONLY);
                if (fd == -1) { perror("tiny_shell: open input failed"); return -1; }
                if (dup2(fd, STDIN_FILENO) == -1) { perror("tiny_shell: dup2 input failed"); close(fd); return -1; }
                close(fd);
            } else { 
                int flags = (redirect_target == 2) ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                int target_fd = (redirect_target == 3) ? STDERR_FILENO : STDOUT_FILENO;
                fd = open(filename, flags, FILE_PERMISSIONS);
                if (fd == -1) { perror("tiny_shell: open output failed"); return -1; }
                if (dup2(fd, target_fd) == -1) { perror("tiny_shell: dup2 output failed"); close(fd); return -1; }
                close(fd);
            }
            continue; 
        }
        i++;
    }
    return 0;
}


static void sigint_handler(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

static void sigtstp_handler(int sig) {
    (void)sig; 
}

static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    int saved_errno = errno;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {

        struct job_t *job = getjobpid(jobs, pid);

        if (WIFSTOPPED(status)) {
            if (job) {
                job->state = STOPPED;
                printf("\n[%d]+ Stopped    %s\n", job->jid, job->cmdline);
                fflush(stdout);
            }
        }
        else if (WIFCONTINUED(status)) {
            if (job && job->state == STOPPED)
                job->state = BG;
        }
        else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            
            // Background Job
            if (job && (job->state == BG || job->state == STOPPED)) {
                const char *reason = WIFSIGNALED(status) ? "Terminated" : "Done";
                printf("\n[%d]+ %s    %s\n", job->jid, reason, job->cmdline);
            }
            else {
                // Foreground Job (Είτε νέα εντολή, είτε από fg)
                
                // 1. ΓΕΝΙΚΟΣ ΕΛΕΓΧΟΣ ΓΙΑ CTRL+C (SIGINT)
                // Αυτό τώρα θα τρέχει ΓΙΑ ΟΛΕΣ τις foreground εντολές
                if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) {
                    write(STDOUT_FILENO, "\n", 1);
                }

                // 2. ΕΛΕΓΧΟΣ ΓΙΑ ΤΟ PIPELINE (Command 1, 2...)
                int cmd_num = -1;
                for(int k=0; k<MAX_CMDS; k++) {
                    if (current_pipeline_pids[k] == pid) {
                        cmd_num = k + 1; 
                        break;
                    }
                }

                if (cmd_num != -1) {
                    if (WIFEXITED(status)) {
                        printf("Command %d exited with status %d\n", cmd_num, WEXITSTATUS(status));
                    } 
                    else if (WIFSIGNALED(status)) {
                        // Τυπώνουμε μήνυμα ΜΟΝΟ αν ΔΕΝ είναι SIGINT (αφού κάναμε αλλαγή γραμμής πάνω)
                        if (WTERMSIG(status) != SIGINT) {
                            printf("Command %d terminated by signal %d\n", cmd_num, WTERMSIG(status));
                        }
                    }
                } 
            }
            fflush(stdout);

            if (job) deletejob(jobs, job->pgid);
        }
    }

    errno = saved_errno;
}
static void install_handlers(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = sigtstp_handler;
    sigaction(SIGTSTP, &sa, NULL);

    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

// Περιμένει μέχρι:
// 1. Να τερματίσουν ΟΛΕΣ οι εντολές του pipe (active == 0)
// 2. Ή η δουλειά να σταματήσει με Ctrl+Z (state == STOPPED)
void wait_for_pipeline(int num_cmds, pid_t pgid) {
    while (1) {
        // 1. Έλεγχος για Ctrl+Z (STOPPED)
        struct job_t *job = getjobpid(jobs, pgid);
        if (job && job->state == STOPPED) {
            break; // Σταμάτησε, άρα επιστρέφουμε
        }

        // 2. Έλεγχος αν τρέχουν ακόμα διεργασίες
        int active = 0;
        for (int i = 0; i < num_cmds; i++) {
            pid_t pid = current_pipeline_pids[i];
            if (pid == 0) continue; 

            // Το kill(pid, 0) ελέγχει αν η διεργασία υπάρχει
            if (kill(pid, 0) == 0) {
                active++; 
            } else if (errno != ESRCH) {
                 active++; 
            }
        }
        
        if (active == 0) break; 
        usleep(10000); 
    }
}

void waitfg(pid_t pid) {
    struct job_t *job;
    while (1) {
        job = getjobpid(jobs, pid);
        if (!job) break;            
        if (job->state != FG) break; 
        usleep(10000);
    }
}


int builtin_cmd(char **argv) {
    if (strcmp(argv[0], "jobs") == 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].pgid != 0) {
                const char *state_str = (jobs[i].state == BG) ? "Running" : "Stopped";
                printf("[%d] %d %s %s\n", jobs[i].jid, jobs[i].pgid, state_str, jobs[i].cmdline);
            }
        }
        return 1;
    }
    
    if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0) {
        if (argv[1] == NULL) {
            printf("%s command requires PID or %%jobid argument\n", argv[0]);
            return 1;
        }

        struct job_t *job = NULL;
        if (argv[1][0] == '%') { 
            int jid = atoi(&argv[1][1]);
            job = getjobjid(jobs, jid);
        } else { 
            job = getjobpid(jobs, atoi(argv[1]));
        }

        if (job == NULL) {
            printf("%s: No such job\n", argv[1]);
            return 1;
        }

        if (strcmp(argv[0], "bg") == 0) {
            printf("[%d]+ %s &\n", job->jid, job->cmdline);
            job->state = BG;
            kill(-job->pgid, SIGCONT);
        } else {
            // --- Εντολή FG ---
            
            // ΠΡΟΣΘΗΚΗ: Τυπώνουμε την εντολή που επαναφέρουμε
            printf("%s\n", job->cmdline); 

            job->state = FG;
            kill(-job->pgid, SIGCONT);
            tcsetpgrp(shell_terminal, job->pgid);
            waitfg(job->pgid);
            tcsetpgrp(shell_terminal, shell_pgid);
        }
        return 1;
    }
    return 0; 
}


int main() {
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("setpgid");
            exit(1);
        }
        tcsetpgrp(shell_terminal, shell_pgid);
        tcgetattr(shell_terminal, &shell_tmodes);
    }

    initjobs(jobs);
    install_handlers();

    while (1) {
        // Καθαρισμός του πίνακα PIDs στην αρχή κάθε νέας εντολής
        for(int k=0; k<MAX_CMDS; k++) current_pipeline_pids[k] = 0;

        char input[MAX_INPUT];

        printf("tiny_shell> "); 
        fflush(stdout);

        if (fgets(input, MAX_INPUT , stdin) == NULL) {
            if (feof(stdin)) printf("\nExiting shell on EOF.\n");
            break;
        }

        input[strcspn(input,"\n")] = '\0';
        if (input[0] == '\0') continue;

        char raw_cmd[MAX_INPUT];
        strcpy(raw_cmd, input);

        struct simple_cmd cmds[MAX_CMDS];
        int num_cmds = 0;
        int bg = 0;

        if (!parse_pipeline(input, cmds, &num_cmds, &bg)) {
            continue;
        }

        if (strcmp(cmds[0].argv[0], "exit") == 0) {
             printf("exiting tinyshell\n");
             free_cmds(cmds, num_cmds);
             break;
        }

        if (strcmp(cmds[0].argv[0], "cd") == 0) {
            if (cmds[0].argv[1] == NULL) {
                if (chdir(getenv("HOME")) != 0) perror("cd failed");
            } else {
                if (chdir(cmds[0].argv[1]) != 0) perror("cd failed");
            }
            free_cmds(cmds, num_cmds);
            continue;
        }

        if (builtin_cmd(cmds[0].argv)) {
            free_cmds(cmds, num_cmds);
            continue;
        }

        int pipefd[2];
        int prev_pipe_read_fd = -1;
        pid_t pgid = 0;
        int fork_failed = 0;

        sigset_t mask_all, mask_one, prev_one;
        sigfillset(&mask_all);
        sigemptyset(&mask_one);
        sigaddset(&mask_one, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask_one, &prev_one);

        for (int i = 0; i < num_cmds; i++) {
            if (i < num_cmds - 1) {
                if (pipe(pipefd) == -1) {
                    perror("pipe failed");
                    fork_failed = 1; break;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                fork_failed = 1; break;
            }

            // *** ΑΠΟΘΗΚΕΥΣΗ PID ΓΙΑ ΑΝΤΙΣΤΟΙΧΙΣΗ ΣΤΟ HANDLER ***
            if (pid > 0) {
                current_pipeline_pids[i] = pid;
            }

            if (pid == 0) { // Child
                sigprocmask(SIG_SETMASK, &prev_one, NULL); 

                if (i == 0) setpgid(0, 0);
                else setpgid(0, pgid);

                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);
    
                if (!bg && i == 0 && shell_is_interactive) {
                    tcsetpgrp(shell_terminal, getpgrp());
                }

                if (prev_pipe_read_fd != -1) {
                    dup2(prev_pipe_read_fd, STDIN_FILENO);
                    close(prev_pipe_read_fd);
                }
                if (i < num_cmds - 1) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                }

                char **args = cmds[i].argv;
                if (handle_redirection(args) != 0) exit(EXIT_FAILURE);

                execvp(args[0], args);

                // --- ΔΙΟΡΘΩΣΗ ΓΙΑ ΤΟ PERMISSION DENIED (WSL/LINUX) ---
                if (errno == EACCES) {
                    if (strchr(args[0], '/') == NULL && access(args[0], F_OK) != 0) {
                        errno = ENOENT; 
                    }
                }
                // -------------------------------------------------------

                perror("execvp failed");
                exit(EXIT_FAILURE);
            } 
            else { // Parent
                if (i == 0) {
                    pgid = pid;
                    setpgid(pid, pgid);
                } else {
                    setpgid(pid, pgid);
                }

                if (prev_pipe_read_fd != -1) close(prev_pipe_read_fd);
                if (i < num_cmds - 1) {
                    close(pipefd[1]);
                    prev_pipe_read_fd = pipefd[0];
                }
            }
        }

        if (!fork_failed) {
            int state = bg ? BG : FG;
            addjob(jobs, pgid, state, raw_cmd);
            struct job_t *job = getjobpid(jobs, pgid);

            sigprocmask(SIG_SETMASK, &prev_one, NULL); 

            if (!bg) {
                if (job) job->state = FG;          

                if (shell_is_interactive) {
                    tcsetpgrp(shell_terminal, pgid);
                    // ΑΛΛΑΓΗ: Περιμένουμε ΟΛΕΣ τις εντολές του pipe να τελειώσουν (ή να γίνουν Stopped)
                    wait_for_pipeline(num_cmds, pgid);
                    tcsetpgrp(shell_terminal, shell_pgid);
                }
            } else {
                if (job) printf("[%d] %d\n", job->jid, job->pgid);
            }
        } else {
             sigprocmask(SIG_SETMASK, &prev_one, NULL);
        }

        if (prev_pipe_read_fd != -1) close(prev_pipe_read_fd);
        free_cmds(cmds, num_cmds);
    }
    return 0;
}