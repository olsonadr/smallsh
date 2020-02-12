/*
 * program  -   smallsh
 * author   -   Nicholas Olson
 */

/*** includes ***/
#include <stdlib.h>     // std library stuff
#include <stdio.h>      // I/O stuff
#include <unistd.h>     // launching processess
#include <string.h>     // for strlen, strcmp, etc
#include <errno.h>      // for errno checking
#include <sys/types.h>  // for opening
#include <fcntl.h>      // for opening
#include <sys/stat.h>   // for opening
#include <signal.h>     // for signal control
#include <sys/wait.h>   // for sigchld
#include <sys/resource.h> // for sigchld


/*** defines ***/
#define CL_BUFF_SIZE 2048
#define CL_ARGS_SIZE 512
#define IN_BUFF_SIZE 2048
#define PWD_BUFF_SIZE 100


/*** the two required global variables ***/
int bg_block_mode_changed = 0;
int signal_received = 0;
int bg_block_mode = 0;
int is_child = 0;


/*** structs ***/
struct CL {
    // overall array of input
    char * buffer;
    
    // array of space-delineated arguments
    char ** args;
    int num_args;

    // current directory
    char * pwd;
    int pwd_size;
    int pwd_len;

    // background processes
    int pid_size;
    int pid_len;
    int * pids;

    // fg process status
    int fg_status;
    int fg_signaled;
    int fg_exited;

    // is child process
    int is_child;

    // path contents
    char ** path;
    int path_len;
};


/*** interface prototypes ***/
int setup_CL(struct CL*);           // setup CL struct (allocate)
int free_CL(struct CL*);            // destroy CL struct (free)
int run_CL(struct CL*, char*);      // parse and execute line of command
int get_input(char*, int);          // get user input and put it in buffer
int clear_CL(struct CL*);           // clear CL struct to neutral state
int pid_check_CL(struct CL*);       // checks the statuses of all bg pids
int main(int, char**);              // main runtime


/*** hidden prototypes ***/
int _parse_input(struct CL*, char*);    // parse a string into a command line struct
int _execute_CL(struct CL*);            // execute command specified by CL_buffer
int _print(char*, FILE*);               // print string to file pointer passed
int _grow_CL_pwd_buff(struct CL*);      // grow the size of the pwd buffer
int _change_CL_pwd(struct CL*, char*);  // change the pwd member of CL to passed str
int _set_curr_pwd(struct CL*);          // change pwd string to cwd
int _get_path(struct CL*);              // fill the path member of the CL
int _push_pid(struct CL*, int);         // add pid to the list of running processes
int _remove_pid(struct CL*, int);       // remove a pid of the given value from arr
void _sigint_handler(int signum);       // act on sigint during shell operation
void _sigtstp_handler(int signum);      // act on sigtstp during shell operation


/*** built-in prototypes ***/
int _CL_exit();                         // exit command
int _CL_cd(int, char**, struct CL*);    // cd command
int _CL_status(struct CL*);             // status command


/*** interface methods ***/
/* allocate memory for CL struct */
int setup_CL(struct CL * cl)
{
    // declarations
    char * tmp;
    char * res;
    int i;

    // initializations
    cl->pwd_size = PWD_BUFF_SIZE;
    cl->pwd_len = 0;
    cl->num_args = 0;
    cl->pid_len = 0;
    cl->pid_size = 5;
    cl->fg_status = 0;
    cl->is_child = 0;
    cl->fg_signaled = 0;
    cl->fg_exited = 1;
    cl->path_len = 0;

    // mallocs
    cl->buffer = malloc(CL_BUFF_SIZE * sizeof(char));
    cl->args = malloc(CL_ARGS_SIZE * sizeof(char*));
    cl->pwd = malloc(cl->pwd_size * sizeof(char));
    cl->pids = malloc(cl->pid_size * sizeof(int));

    // read path
    _get_path(cl);

    // set initial pwd
    _set_curr_pwd(cl);
}

/* free memory for CL struct
 * pre-condition:   cl has been setup using setup_CL */
int free_CL(struct CL * cl)
{
    // declarations
    int i;

    // only done for malloc'd args
    for (i = 0; i < cl->num_args; i++)
    {
        free(cl->args[i]);
    }

    // only done for malloc'd paths
    for (i = 0; i < cl->path_len; i++)
    {
        free(cl->path[i]);
    }

    // frees
    free(cl->buffer);
    free(cl->args);
    free(cl->pids);
    free(cl->pwd);
    free(cl->path);
}

/* parse and execute command in "input" using CL struct "cl"
 * pre-condition:   CL struct has been setup
 * post-condition:  returned 1 if parsing error,
 *                  returned 2 if execution error,
 *                  returned -1 if successful but exiting
 *                  returned 0 if successful */
int run_CL(struct CL * cl, char * input)
{
    // parse into CL
    if (_parse_input(cl, input) != 0) { return 1; }
    
    // execute command
    int result = _execute_CL(cl);
    if (result > 0) { return result; }
    else if (result == -1) { return -1; }

    // return
    return 0;
}

/* get user input and put it in provided buffer */
int get_input(char * buffer, int buffer_size)
{
   /* 
    // flush input
    fflush(stdout);
    fflush(stdin);
    */
    
    // printf PS1 string
    fflush(stdout);
    printf(": ");
    fflush(stdout);

    // get input
    if (fgets(buffer, buffer_size, stdin) == NULL) { return 1; };

    // trim the newline
    strtok(buffer, "\n");

    // flush input
    fflush(stdin);

    // that's it, it's pretty simple
    return 0;
}

/* clear the command line struct back to default state */
int clear_CL(struct CL * cl)
{
    int i;

    // free arguments
    for (i = 0; i < cl->num_args; i++)
    {
        free(cl->args[i]);
    }

    // reset vars
    cl->num_args = 0;
    cl->buffer[0] = '\0';
}

/* checks if the background processes have completed */
int pid_check_CL(struct CL * cl)
{
    int result;
    int i;

    // check for bg blocking mode change
    if (bg_block_mode_changed == 1)
    {
        if (bg_block_mode == 1)
        {
            fputs("\nEntering foreground-only mode (& is now ignored)\n", stdout);
        }
        else if (bg_block_mode == 0)
        {
            fputs("\nExiting foreground-only mode\n", stdout);
        }
        bg_block_mode_changed = 0;
    }

    for(i = 0; i < cl->pid_len; i++)
    {
        // get whether bg process has exited
        pid_t cpid = 0;
        result = 0;
        cpid = waitpid(cl->pids[i], &result, WNOHANG);
        //cpid = waitpid(cl->pids[i], &result, WNOHANG);
        /*if (errno == ECHILD) printf("does not exist\n");
        if (errno == EINVAL) printf("bad options\n");
        if (errno == ENOSYS) printf("bad pid\n");*/

        /*
        printf("Stored pid = \"%d\"\n", cl->pids[i]);
        printf("Waitpid result =  \"%d\"\n", cpid);
        printf("Status = %d\n", result);
        printf("WIFEXITED = %d\n", WIFEXITED(result));*/
        if (cpid != 0 && WIFEXITED(result))
        {
            fflush(stdout);
            printf("background pid %d is done: exit value %d\n",
                        cl->pids[i], WEXITSTATUS(result));
            fflush(stdout);
            _remove_pid(cl, cl->pids[i]);
        }
        else if (cpid != 0 && WIFSIGNALED(result))
        {
            fflush(stdout);
            printf("\nbackground pid %d is done: terminated by signal %d\n",
                        cl->pids[i], WTERMSIG(result));
            fflush(stdout);
            _remove_pid(cl, cl->pids[i]);
        }
    }
}


/*** hidden methods ***/
/* parses the "input" string into a CL struct
 * pre-condition:   cl has been setup */
int _parse_input(struct CL * cl, char * input)
{
    // declarations
    int start = 0;
    int end = 0;
    int i = 0;
    int len;

    // copy input into buffer
    sprintf(cl->buffer, "%s\0", input);
    
    // get length of buffer
    len = strlen(cl->buffer);

    // set first arg to null for now
    cl->args[0] = NULL;

    // check for comment
    if (len > 0 && cl->buffer[0] == '#')
    {
        return 0;
    }

    // parse into args array
    for (end = 0; end < len; end++)
    {
        // beginning and/or end of word
        if ((cl->buffer[end] == ' ') || (end == len - 1))
        {
            // if there has been at least one char since last space
            if ((end - start > 0) || ((end == len - 1) && (cl->buffer[end] != ' ')))
            {
                if (end == len - 1) { end++; }
                cl->args[cl->num_args] = malloc((end - start + 1) * sizeof(char));
                sprintf(cl->args[cl->num_args], "%.*s\0", (end - start), (cl->buffer + start));

                // check for $$
                char * wow = strstr(cl->args[cl->num_args], "$$");
                if (wow != NULL)
                {
                    // get pid
                    pid_t pid = getpid();
                    char pid_buff[100]; // RIP pids longer than 100 digits
                    sprintf(pid_buff, "%d\0", pid);

                    // copy arg into tmp
                    char * tmp = malloc((end - start + 1) * sizeof(char));
                    strcpy(tmp, cl->args[cl->num_args]);

                    // cut-off string before &&
                    wow = tmp + (wow - cl->args[cl->num_args]);
                    wow[0] = '\0';

                    // re-allocate arg buffer
                    free(cl->args[cl->num_args]);
                    cl->args[cl->num_args] = malloc((strlen(pid_buff)+(end-start+1))*sizeof(char));

                    // compile full string 
                    sprintf(cl->args[cl->num_args], "%s%s%s", tmp, pid_buff, (wow+2));

                    // free tmp
                    free(tmp);
                }

                cl->num_args += 1;
            }

            // move start forward
            start = end + 1;
        }
    }

    // add final null to signify end of args
    cl->args[cl->num_args] = (char*) NULL;

    // return
    return 0;
}

/* executes the command contained within the CL struct
 * pre-condition:   cl has been setup */
int _execute_CL(struct CL * cl)
{
    // declarations
    int in_stream = STDIN_FILENO;
    int out_stream = STDOUT_FILENO;
    int saved_in;
    int saved_out;
    int in_redir = 0;
    int out_redir = 0;
    int background = 0;
    int special_count = 0;
    int result = 0;
    int i = 0;
    int j = 0;

    // if command was empty
    if (cl->num_args == 0) { return 0; }

    // if exit (save the time of parsing)
    if (strcmp(cl->args[0], "exit") == 0) { return -1; }

    // check for special args
    if (_process_special_args(cl, &special_count,
                        &in_stream, &out_stream,
                        &in_redir, &out_redir, &background) != 0)
    { return 0; }

    // don't pass special arguments in
    char * tmp = cl->args[cl->num_args - special_count];
    cl->args[cl->num_args - special_count] = NULL;
        
    // execute built-in commands
    if (strcmp(cl->args[0], "cd") == 0)
        { _CL_cd((cl->num_args - special_count), cl->args, cl); }
    else if (strcmp(cl->args[0], "exit") == 0)
        { _CL_exit(); }
    else if (strcmp(cl->args[0], "status") == 0)
        { _CL_status(cl); }

    // execute non built-ins
    else {
        // fork process
        result = 0; 
        signal(SIGINT, _sigint_handler);
        i = fork();

        // if parent process
        if (i > 0)
        {
            // is parent
            cl->is_child = 0;
            is_child = 0;

            // status var
            int status = 0;

            // background process
            if (background)
            {
                signal(SIGINT, SIG_IGN);
                
                fflush(stdout);
                printf("background pid is %d\n", i);
                fflush(stdout);
                _push_pid(cl, i);
            }
            // foreground process
            else
            {
                fflush(stdout);

                j = 0;
                is_child = 1;
                while (j == 0) { j = waitpid(i, &status, WNOHANG); }
                //while (j == 0) { j = waitpid(i, &status, WNOHANG); }
                is_child = 0;

                signal(SIGINT, _sigint_handler);

                /*
                printf("j = %d\n", j);
                printf("status = %d\n", status);
                printf("WIFSIGNALED = %d\n", WIFSIGNALED(status));
                printf("WIFEXITED = %d\n", WIFEXITED(status));
                */

                // if child was not built-in
                if (status != 0)
                {
                    if (WIFSIGNALED(status))
                    {
                        cl->fg_status = WTERMSIG(status);
                        cl->fg_exited = 0;
                        cl->fg_signaled = 1;
                    }
                    else if (WIFEXITED(status))
                    {
                        cl->fg_status = WEXITSTATUS(status);
                        cl->fg_exited = 1;
                        cl->fg_signaled = 0;
                    }
                }
            }
            
            signal(SIGINT, SIG_IGN);
        }
        // if forked child process
        else if (i == 0)
        {
            // is child
            cl->is_child = 1;
            is_child = 1;

            // redirect input and output
            if (in_redir)  { saved_in = dup(STDIN_FILENO); 
                             dup2(in_stream, STDIN_FILENO); }
            if (out_redir) { fflush(stdout);
                             saved_out = dup(STDOUT_FILENO);
                             dup2(out_stream, STDOUT_FILENO); }
            
            // always ignore sigtstp
            signal(SIGTSTP, SIG_IGN);

            // ignore sigint if in background
            if (background) { signal(SIGINT, SIG_IGN); }
            else            { signal(SIGINT, _sigint_handler); }

            // not a built-in command
            for (j = 0; j < cl->path_len; j++)
            {
                char * path_tmp = malloc((strlen(cl->args[0]) + strlen(cl->path[j]) + 2) * sizeof(char));
                sprintf(path_tmp, "%s/%s", cl->path[j], cl->args[0]);
                execv(path_tmp, cl->args);
                //execvp(cl->args[0], cl->args);
                free(path_tmp);
            }

            perror("\0");

            // following is only reached if execvp failed
            cl->fg_status = 1;
            result = -1;
            
            // close streams
            if (in_redir)  { close(in_stream);
                             dup2(STDIN_FILENO, saved_in);
                             close(saved_in); }
            if (out_redir) { close(out_stream); 
                             dup2(STDOUT_FILENO, saved_out);
                             close(saved_out); }

        } // child thing
    }

    // put old special arguments back
    cl->args[cl->num_args - special_count] = tmp;

    // return
    return result;
}

/* check the argument list for special arguments (redirection / bg)
 * pre-condition:   cl setup and parsed
 * post-condition:  flags and streams passed are updated */
_process_special_args(struct CL * cl, int * special_count,
                      int * in_stream, int * out_stream,
                      int * in_redir, int * out_redir,
                      int * background)
{
    // declarations
    int i;

    // initial values
    *in_stream = STDIN_FILENO;
    *out_stream = STDOUT_FILENO;
    *special_count = 0;
    *background = 0;
    *out_redir = 0;
    *in_redir = 0;

    // check for background first
    if (strcmp(cl->args[cl->num_args - 1], "&") == 0)
    {
        // open in background
        *special_count += 1;

        // background ( redir can be overwritten )
        if (bg_block_mode == 0)
        {
            *in_stream = open("/dev/null", O_RDONLY);
            *out_stream = open("/dev/null", O_WRONLY);
            *in_redir = 1;
            *out_redir = 1;
            *background = 1;
        }
    }

    // check for redirection special args
    for (i = 0; i < cl->num_args; i++)
    {
        if (strcmp(cl->args[i], "<") == 0)
        {
            // new input file
            *special_count += 2;

            // if background or already redir'd, close the old one
            if (*background || *in_redir) { close(*in_stream); }

            // "<" is not last arg
            *in_stream = open(cl->args[i+1], O_RDONLY);
            *in_redir = 1;

            // check for open failure
            if (*in_stream == -1)
            {
                perror("\0");
                return 1;
            }
        }
        else if (strcmp(cl->args[i], ">") == 0)
        {
            // new output file
            *special_count += 2;
           
            // if background or already redir'd, close the old one
            if (*background || *out_redir) { close(*out_stream); }

            // ">" is not last arg
            *out_stream = open(cl->args[i+1], O_WRONLY | O_TRUNC | O_CREAT, 0600);
            *out_redir = 1;
            
            // check for open failure
            if (*out_stream == -1)
            {
                perror("\0");
                return 1;
            }
        }
        else if (strcmp(cl->args[i], ">>") == 0)
        {
            // new output file (append)
            *special_count += 2;
            
            // if background or already redir'd, close the old one
            if (*background || *out_redir) { close(*out_stream); }
            
            // ">>" is not last arg
            *out_stream = open(cl->args[i+1], O_WRONLY | O_APPEND | O_CREAT, 0600);
            *out_redir = 1;
            
            // check for open failure
            if (*out_stream == -1)
            {
                perror("\0");
                return 1;
            }
        }
    }

    return 0;
}

/* grow the size of the pwd buffer */
int _grow_CL_pwd_buff(struct CL * cl)
{
    // declarations
    char * tmp;

    // allocate temporary array
    tmp = malloc(cl->pwd_size * sizeof(char));

    // fill temporary array
    strcpy(tmp, cl->pwd);

    // free old array
    free(cl->pwd);

    // double size
    cl->pwd_size *= 2;

    // allocate new pwd buffer
    cl->pwd = malloc(cl->pwd_size * sizeof(char));

    // copy tmp into new buffer
    strcpy(cl->pwd, tmp);

    // free tmp buffer
    free(tmp);
}

/* change pwd to the passed string
 * pre-condition:   new_dir is null-terminated */
int _change_CL_pwd(struct CL * cl, char * new_dir)
{
    // grow as needed
    while (strlen(new_dir) >= cl->pwd_size) { _grow_CL_pwd_buff(cl); }

    // set new length
    cl->pwd_len = strlen(new_dir);

    // copy new_dir into pwd
    strcpy(cl->pwd, new_dir);
}

/* change the pwd string in cl to the cwd */
int _set_curr_pwd(struct CL * cl)
{
    // declarations
    char * tmp;
    char * res;
    int i;

    // try to allocate cwd buffer
    i = PWD_BUFF_SIZE / 2;
    tmp = malloc(i * sizeof(char));
    res = getcwd(tmp, i);

    // while buff is not big enough for cwd
    while (res == NULL && errno == ERANGE)
    {
        free(tmp);
        i *= 2;
        tmp = malloc(i * sizeof(char));
        res = getcwd(tmp, i);
    }

    // big enough, use the cwd
    _change_CL_pwd(cl, tmp);

    // free temp stuff
    free(tmp);
}

/* parse current PATH var into cl members */
int _get_path(struct CL * cl)
{
    // declarations
    const char * c_tmp;
    int tmp_size = 10;
    int tmp_len = 0;
    char * tmp;
    char * tmp2;
    char * tmp_free;
    int count = 0;
    int i;

    // if path has been alloc'd
    if (cl->path_len != 0)
    {
        for (i = 0; i < cl->path_len; i++) { free(cl->path[i]); }
        free(cl->path);
        cl->path_len = 0;
    }

    // get path var
    c_tmp = getenv("PATH");
    tmp = malloc((strlen(c_tmp) + 1) * sizeof(char));
    tmp_free = tmp;
    strcpy(tmp, c_tmp);

    // get count of ":"
    tmp2 = tmp;
    while((tmp2 = strstr(tmp2, ":")) != NULL)
    {
       count++;
       tmp2++;
    }

    // allocate path arr
    cl->path_len = count + 1;
    cl->path = malloc(cl->path_len * sizeof(char *));

    // if path is empty
    if (strlen(c_tmp) == 0)
    {
        free(tmp_free);
        cl->path[0] = ".";
        return 1;
    }

    // for each ":" between paths (tmp is at beginning)
    count = 0;
    for (i = 0; i < cl->path_len && tmp != NULL; i++)
    {
        // ensure char after curr path is '\0'
        if ((tmp2 = strstr(tmp, ":")) != NULL)
        {
            // set the following ":" to "\0"
            tmp2[0] = '\0';
        }

        // allocate current path string
        cl->path[i] = malloc((strlen(tmp) + 1) * sizeof(char));
    
        // copy path into arr
        strcpy(cl->path[i], tmp);

        // move forward
        if (tmp2 != NULL) { tmp = tmp2 + 1; }
        else              { tmp = NULL; }

        //printf("~%d~%s\n", i, cl->path[i]);
    }

    // cleanup temp
    free(tmp_free);
}

/* add pid to the list of background processes */
int _push_pid(struct CL * cl, int new_pid)
{
    // local
    int i;

    // check if pid list needs to grow
    if (cl->pid_len == cl->pid_size)
    {
        int * tmp = malloc(cl->pid_size * sizeof(int));
        for (i = 0; i < cl->pid_size; i++) { tmp[i] = cl->pids[i]; }
        free(cl->pids);
        cl->pids = malloc(cl->pid_size * 2 * sizeof(int));
        cl->pid_size *= 2;
        free(tmp);
    }

    // add new pid
    cl->pids[cl->pid_len] = new_pid;
    cl->pid_len++;

    // return
    return 0;
}

int _remove_pid(struct CL * cl, int target_pid)
{
    // local
    int found = 0;
    int i;

    // check pid list
    for (i = 0; i < cl->pid_len; i++)
    {
        // shift elements after target_pid up
        if (cl->pids[i] == target_pid || found == 1)
        {
            found = 1;
            if (i == cl->pid_len - 1) { cl->pid_len--; }
            else { cl->pids[i] = cl->pids[i+1]; }
        }
    }

    // return
    return 0;
}

/* act assigned to sigint invocation */
void _sigint_handler(int signum)
{
    // exit process if child
    //if (is_child) { printf("SIGINT\n"); }
    //else { printf("\n"); }
   
    // flush out
    fflush(stdout);

    //printf("is_child = %d\n", is_child);
    //if (is_child) printf("terminated by signal %d\n", signum);
    
    //printf("terminated by signal %d\n", signum);
    fputs("terminated by signal 2\n", stdout);

    //else { printf("\n"); }
    //signal_received = 1;
    //if (is_child) signal_received = 1;

    // flush stdin
    fflush(stdin);
    fflush(stdout);
}

/* act assigned to sigchld invocation */
void _sigchld_handler(int signum)
{
}

/* act assigned to sigtstp invocation */
void _sigtstp_handler(int signum)
{
    // toggle background blocking mode
    if (bg_block_mode == 0)
    {
        bg_block_mode = 1;
        bg_block_mode_changed = 1;
    }
    else if (bg_block_mode == 1)
    {
        bg_block_mode = 0;
        bg_block_mode_changed = 1;
    }

    /*
    // declaration
    int i;

    // flush
    fflush(stdout);

    // wait for children
    wait(&i);

    // toggle background blocking mode
    if (bg_block_mode == 0)
    {
        bg_block_mode = 1;
        //printf("\nEntering foreground-only mode (& is now ignored)\n");
        fputs("\nEntering foreground-only mode (& is now ignored)\n", stdout);
    }
    else if (bg_block_mode == 1)
    {
        bg_block_mode = 0;
        //printf("\nExiting foreground-only mode\n");
        fputs("\nExiting foreground-only mode\n", stdout);
    }
    
    // flush stdin and stdout
    fflush(stdin);
    fflush(stdout);
    */
}


/*** built-ins ***/
/* built-in exit command (exits the shell) */
int _CL_exit()
{
    return -1;
}

/* built-in cd command (change directory) */
int _CL_cd(int argc, char ** argv, struct CL * cl)
{
    // declarations
    int i;

    // move home
    if (argc == 1)
    {
        chdir(getenv("HOME"));
    }

    // change directory
    for(i = 0; i < argc; i++)
    {
        chdir(argv[i]);
    }

    // update pwd member
    _set_curr_pwd(cl);

    /* printing pwd
    _print(cl->pwd, out_stream);
    printf("\n"); */

    // return
    return 0;
}

/* built-in status command (shows shell status) */
int _CL_status(struct CL * cl)
{
    if      (cl->fg_exited)
    {
        fflush(stdout);
        printf("exit value %d\n", cl->fg_status);
        fflush(stdout);
    }
    else if (cl->fg_signaled || signal_received)
    {
        fflush(stdout);
        printf("terminated by signal %d\n", cl->fg_status);
        fflush(stdout);
        signal_received = 0;
    }
    return 0;
}


/*** main ***/
int main(int argc, char** argv)
{
    // declarations
    char * in_buff;
    int keep_going;
    struct CL cl;
    int result;
    int i;

    // malloc input buffer
    in_buff = malloc(IN_BUFF_SIZE * sizeof(char));

    // setup command line struct
    setup_CL(&cl);

    // declare sigaction structs
    struct sigaction sigint_action  = {0};
    struct sigaction sigtstp_action = {0};
    struct sigaction sigchld_action = {0};

    // set signal handlers
    sigint_action.sa_handler  = _sigint_handler;
    sigtstp_action.sa_handler = _sigtstp_handler;
    sigchld_action.sa_handler = _sigchld_handler;

    // assign signal actions with sigaction
    //sigaction(SIGINT,  &sigint_action,  NULL);
    signal(SIGINT, SIG_IGN);
    sigaction(SIGTSTP, &sigtstp_action, NULL);
    signal(SIGCHLD, SIG_IGN);
    //sigaction(SIGCHLD, &sigchld_action, NULL);

    // command loop
    keep_going = 0;
    while (keep_going == 0)
    {
        // clear CL
        clear_CL(&cl);

        // check background
        pid_check_CL(&cl);

        // get commands
        if (get_input(in_buff, IN_BUFF_SIZE) != 0) { continue; };

        // run commands
        keep_going = run_CL(&cl, in_buff);
    }

    // destroy command line
    free_CL(&cl);

    // free input buffer
    free(in_buff);

    // return (last exit status if child)
    return (cl.is_child) ? (cl.fg_status) : (0);
}
