/*
 * program -    smallsh
 * author -     Nicholas Olson
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


/*** defines ***/
#define CL_BUFF_SIZE 2048
#define CL_ARGS_SIZE 512
#define IN_BUFF_SIZE 2048
#define PWD_BUFF_SIZE 100


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
int _push_pid(struct CL*, int);              // add pid to the list of running processes
int _remove_pid(struct CL*, int);       // remove a pid of the given value from arr

/*** built-in prototypes ***/
int _CL_exit(FILE*, FILE*, int, char**, pthread_t*);            // exit command
int _CL_cd(FILE*, FILE*, int, char**, pthread_t*, struct CL*);  // cd command
int _CL_status(FILE*, FILE*, int, char**, pthread_t*);          // status command


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

    // mallocs
    cl->buffer = malloc(CL_BUFF_SIZE * sizeof(char));
    cl->args = malloc(CL_ARGS_SIZE * sizeof(char*));
    cl->pwd = malloc(cl->pwd_size * sizeof(char));
    cl->pids = malloc(cl->pid_size * sizeof(int));

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

    // frees
    free(cl->buffer);
    free(cl->args);
    free(cl->pids);
    free(cl->pwd);
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
    // printf PS1 string
    printf(": ");

    // get input
    fgets(buffer, buffer_size, stdin);

    // trim the newline
    strtok(buffer, "\n");

    // that's it, it's pretty simple
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

    for(i = 0; i < cl->pid_len; i++)
    {
        // get whether bg process has exited
        pid_t cpid = waitpid(cl->pids[i], &result, WNOHANG);

        if (WIFEXITED(result))
        {
            printf("Child %d terminated with status %d\n", cl->pids[i], WEXITSTATUS(result));
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

    // check if command was not empty
    if (cl->num_args == 0) { return 0; }

    // if exit
    if (strcmp(cl->args[0], "exit") == 0) { return -1; }

    // special argument vars
    special_count = 0;

    // check for special args
    if (strcmp(cl->args[cl->num_args - 1], "&") == 0)
    {
        // open in background
        special_count += 1;

        // background ( redir can be overwritten )
        in_stream = open("/dev/null", O_RDONLY);
        out_stream = open("/dev/null", O_WRONLY);
        in_redir = 1;
        out_redir = 1;
        background = 1;
    }
    for (i = 0; i < cl->num_args; i++)
    {
        if (strcmp(cl->args[i], "<") == 0)
        {
            // new input file
            special_count += 2;

            // if background or already redir'd, close the old one
            if (background || in_redir) { close(in_stream); }

            // "<" is not last arg
            in_stream = open(cl->args[i+1], O_RDONLY);
            in_redir = 1;
        }
        else if (strcmp(cl->args[i], ">") == 0)
        {
            // new output file
            special_count += 2;
           
            // if background or already redir'd, close the old one
            if (background || out_redir) { close(out_stream); }

            // ">" is not last arg
            out_stream = open(cl->args[i+1], O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU);
            out_redir = 1;
        }
        else if (strcmp(cl->args[i], ">>") == 0)
        {
            // new output file (append)
            special_count += 2;
            
            // if background or already redir'd, close the old one
            if (background || out_redir) { close(out_stream); }
            
            // ">>" is not last arg
            out_stream = open(cl->args[i+1], O_WRONLY | O_APPEND | O_CREAT, S_IRWXU);
            out_redir = 1;
        }
    }

    pthread_t pid_wow;
    pthread_t * pid = &pid_wow;

    // ignore special
    char * tmp = cl->args[cl->num_args - special_count];
    cl->args[cl->num_args - special_count] = NULL;

    // run program
    result = 0; 
    i = fork();

    if (i > 0)
    {
        if (!background) { waitpid(i); } // parent wait
        else
        {
            printf("Child %d started\n", i);
            _push_pid(cl, i);
        } // parent not wait
    }
    else if (i == 0)
    {
        // redirect stuff
        if (in_redir)  { saved_in = dup(STDIN_FILENO); 
                         dup2(in_stream, STDIN_FILENO); }
        if (out_redir) { fflush(stdout);
                         saved_out = dup(STDOUT_FILENO);
                         dup2(out_stream, STDOUT_FILENO); }
        
        if (strcmp(cl->args[0], "cd") == 0)
        {
            result = _CL_cd(in_stream, out_stream,
                            (cl->num_args - special_count),
                            cl->args, pid, cl);
        }
        else if (strcmp(cl->args[0], "exit") == 0)
        {
            result = _CL_exit(in_stream, out_stream,
                                (cl->num_args - special_count),
                                cl->args, pid);
        }
        else if (strcmp(cl->args[0], "status") == 0)
        {
            result = _CL_status(in_stream, out_stream,
                                (cl->num_args - special_count),
                                cl->args, pid);
        }
        else
        {
            // not a built-in
            execvp(cl->args[0], cl->args);
            result = -1; // only reached if execvp failed
        }

        // close streams
        if (in_redir)  { close(in_stream);
                         dup2(STDIN_FILENO, saved_in);
                         close(saved_in); }
        if (out_redir) { close(out_stream); 
                         dup2(STDOUT_FILENO, saved_out);
                         close(saved_out); }
    } // child thing

    // put old special args back
    cl->args[cl->num_args - special_count] = tmp;

    // return
    return result;
}

/* print string to file pointer if provided */
int _print(char * str, FILE * dest)
{
    // background, don't print
    if (dest == NULL)
    {
        return 0;
    }
    else
    {
        // flush
        fflush(dest);

        // stdout, normal print
        if (dest == stdout)
        {
            printf(str);
        }
        // redirected
        else
        {
            fprintf(dest, str);
        }

        // flush
        fflush(dest);
    }
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


/*** built-ins ***/
/* built-in exit command (exits the shell) */
int _CL_exit(FILE * in_stream, FILE * out_stream,
             int argc, char ** argv, pthread_t * pid)
{
    // lock mutex thread

    /* printing
    _print("exiting\n", out_stream); */

    return -1;
}

/* built-in cd command (change directory) */
int _CL_cd(FILE * in_stream, FILE * out_stream,
           int argc, char ** argv, pthread_t * pid,
           struct CL * cl)
{
    // declarations
    int i;

    // lock mutex thread
    // *****************

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
int _CL_status(FILE * in_stream, FILE * out_stream,
               int argc, char ** argv, pthread_t * pid)
{
    // lock mutex thread
    return 0;
}

/*** main ***/
int main(int argc, char** argv)
{
    char * in_buff;
    int keep_going;
    struct CL cl;
    int i;

    // malloc input buffer
    in_buff = malloc(IN_BUFF_SIZE * sizeof(char));

    // setup command line struct
    setup_CL(&cl);

    // command loop
    keep_going = 0;

    while (keep_going == 0)
    {
        // check background
        pid_check_CL(&cl);

        // get commands
        get_input(in_buff, IN_BUFF_SIZE);

        // run commands
        keep_going = run_CL(&cl, in_buff);

        // clear CL
        clear_CL(&cl);
    }

    // destroy command line
    free_CL(&cl);

    // free input buffer
    free(in_buff);
}
