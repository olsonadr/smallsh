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
#include <termios.h>    // for terminal attr control
#include <ctype.h>      // for iscntrl


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

    // history of commands
    char ** history;
    int hist_size;
    int hist_len;
    int curr_idx;
};


/*** interface prototypes ***/
int setup_CL(struct CL*);               // setup CL struct (allocate)
int free_CL(struct CL*);                // destroy CL struct (free)
int run_CL(struct CL*, char*);          // parse and execute line of command
int get_input(struct CL*, char*, int);  // get user input and put it in buffer
int clear_CL(struct CL*);               // clear CL struct to neutral state
int pid_check_CL(struct CL*);           // checks the statuses of all bg pids
int main(int, char**);                  // main runtime


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
int _add_to_hist(struct CL*, char*);    // add a command to the command history
int _grow_history(struct CL*);          // grow history dynarr
int _tab_complete(struct CL*, char*);   // update passed buffer w/ tab complete


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
    cl->hist_len = 0;
    cl->hist_size = 10;
    cl->curr_idx = 0;

    // mallocs
    cl->buffer = malloc(CL_BUFF_SIZE * sizeof(char));
    cl->args = malloc(CL_ARGS_SIZE * sizeof(char*));
    cl->pwd = malloc(cl->pwd_size * sizeof(char));
    cl->pids = malloc(cl->pid_size * sizeof(int));
    cl->history = malloc(cl->hist_size * sizeof(char*));

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

    // only done for malloc'd history
    for (i = 0; i < cl->hist_len; i++)
    {
        free(cl->history[i]);
    }

    // frees
    free(cl->buffer);
    free(cl->args);
    free(cl->pids);
    free(cl->pwd);
    free(cl->path);
    free(cl->history);
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
int get_input(struct CL * cl, char * buffer, int buffer_size)
{
    // declaration
    struct termios termInfo, save;
    int curr_len = 0;
    int saved_out;
    int null_out;
    char str[5];
    int i = 0;
    int j = 0;
    int c;
    int t;

    // setup timeout stuff
    fd_set rfds;
    struct timeval tv;
    int retval;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 1; // 1 second
    tv.tv_usec = 0; // 1/10 second

    // move curr_idx to top
    cl->curr_idx = cl->hist_len;

    // get terminal attributes
    t = tcgetattr(0, &termInfo);

    // turn echo off
    termInfo.c_lflag &= ~ECHO; /* turn off ECHO */
    termInfo.c_lflag &= ~ICANON; /* turn on raw mode */

    // set attributes
    tcsetattr(0, TCSANOW, &termInfo);

    // stop buffering
    //setvbuf(stdout, NULL, _IONBF, 0);

    // printf PS1 string
    fflush(stdout);
    fputs(": ", stdout);
    fflush(stdout);
    //fflush(stdin);

    //t = -1;
    // get input
    curr_len = 0;
    //while ((t != -1) ||
    //        (c = getchar()) != EOF && c != '\n' && c != '\0' && (i < buffer_size - 1))
    while ((c = getchar()) != EOF && c != '\n' && c != '\0' && (i < buffer_size - 1))
    //do
    {
        //t = 0;
        //fflush(stdin);
        //c = getchar();
        //scanf("%c", &c);
        //fflush(stdin);

        // do special stuff
        if (c == 127) // backspace
        {
            // if not at beginning
            if (i != 0)
            {
                // move cursor left
                printf("%c%c%c", 27, '[', 'D');

                // shift buffer and print
                for (j = i - 1; j < curr_len - 1; j++)
                {
                    buffer[j] = buffer[j+1];
                    putchar(buffer[j]);
                }
                //buffer[curr_len] = '\0';
                //printf("\n%s\n", buffer);

                // clear last char
                putchar(' ');

                // move cursor back to where it should be
                if (curr_len - i + 1 != 0)
                {
                    printf("%c%c%c%c", 27, '[', '0' + (curr_len - i + 1), 'D');
                }

                curr_len--;
                i--;

                // add null termination
                buffer[curr_len] = '\0';
            }
        }
        else if (c == 27) // ansi escape sequences
        {
            // get rest of escape sequence
            fgets(str, 3, stdin);

            // handle sequence
            if (strcmp(str, "[A") == 0) // up arrow
            {
                // move earlier in history
                if (cl->curr_idx != 0)
                {
                    // move back
                    cl->curr_idx--;

                    // copy the thing there
                    strcpy(buffer, cl->history[cl->curr_idx]);

                    // move cursor back and remove what's there
                    for (j = 0; j < curr_len; j++)
                    {
                        printf("%c%c%c", 27, '[', 'D');
                        putchar(' ');
                        printf("%c%c%c", 27, '[', 'D');
                    }

                    // get len
                    curr_len = strlen(cl->history[cl->curr_idx]);
                    i = curr_len;

                    // print command
                    printf(buffer);
                }
            }
            else if (strcmp(str, "[B") == 0) // down arrow
            {
                // move later in history
                if (cl->curr_idx != cl->hist_len)
                {
                    // move forward
                    cl->curr_idx++;

                    // move cursor back and remove what's there
                    for (j = 0; j < curr_len; j++)
                    {
                        printf("%c%c%c", 27, '[', 'D');
                        putchar(' ');
                        printf("%c%c%c", 27, '[', 'D');
                    }

                    if (cl->curr_idx == cl->hist_len)
                    {
                        buffer[0] = '\0';
                        curr_len = 0;
                        i = curr_len;
                    }
                    else
                    {
                        // copy the thing there
                        strcpy(buffer, cl->history[cl->curr_idx]);
                        curr_len = strlen(cl->history[cl->curr_idx]);
                        i = curr_len;
                    }

                    // print command
                    printf(buffer);

                }
            }
            else if (strcmp(str, "[C") == 0) // right arrow
            {
                // move cursor right
                if (i < curr_len)
                {
                    printf("%c%c%c", 27, '[', 'C');
                    i++;
                }
            }
            else if (strcmp(str, "[D") == 0) // left arrow
            {
                // move cursor left
                if (i != 0)
                {
                    printf("%c%c%c", 27, '[', 'D');
                    i--;
                }
            }
            else
            {
                // Unsupported escape sequence
            }
        }
        else
        {
            // normal character

            // put new char
            putchar(c);

            // shift following chars back
            for (j = curr_len; j > i; j--)
            {
                buffer[j] = buffer[j-1];
            }

            // store char in buff
            buffer[i] = c;

            // put following chars
            for (j = i + 1; j <= curr_len; j++)
            {
                putchar(buffer[j]);
            }

            // if not at end, move cursor back
            if (curr_len - i != 0)
            {
                printf("%c%c%c%c", 27, '[', '0' + (curr_len - i), 'D');
            }

            // add null termination
            buffer[curr_len+1] = '\0';

            // add to length
            curr_len++;
            
            // move cursor forward            
            i++;
        }
        
        // flush
        fflush(stdout);
    }
    
    // get rid of buffered stuff
    fflush(stdin);
    
    // add end of string
    buffer[curr_len+1] = '\0';

    // newline
    putchar('\n');

    // turn ECHO back on
    termInfo.c_lflag |= ECHO; /* turn on ECHO */
    termInfo.c_lflag |= ICANON; /* turn on raw mode */
   
    // set attributes
    tcsetattr(0, TCSANOW, &termInfo);

    // stop buffering
    //setvbuf(stdout, NULL, _IOLBF, 0);

    // trim the newline
    //strtok(buffer, "\n");

    // flush input
    fflush(stdin);

    // add command to history
    if (curr_len != 0) { _add_to_hist(cl, buffer); }
    else { return 2; }

    // that's it, it's pretty simple
    return 0;
}

/* add string to command history
 * pre-condition:   command has no newline */
int _add_to_hist(struct CL * cl, char * command)
{
    // if command is empty
    if (command[0] == '\0') { return 1; }

    // if history needs to grow
    if (cl->hist_len == cl->hist_size - 1) { _grow_history(cl); }

    // malloc new element
    cl->history[cl->hist_len] = malloc((strlen(command)+1) * sizeof(char));

    // add new element
    strcpy(cl->history[cl->hist_len], command);

    // increment length
    cl->hist_len++;

    // move curr_idx
    cl->curr_idx = cl->hist_len;
    
    // return
    return 0;
}

/* double size of history array */
int _grow_history(struct CL * cl)
{
    // declarations
    char ** tmp;
    int i;

    // copy old elements
    tmp = malloc(cl->hist_len * sizeof(char*));
    for (i = 0; i < cl->hist_len; i++)
    {
        tmp[i] = cl->history[i];
    }
    
    // free old buff
    free(cl->history);
    
    // double size
    cl->hist_size *= 2;
    
    // malloc new buff
    cl->history = malloc(cl->hist_size * sizeof(char*));
    
    // copy elements in
    for (i = 0; i < cl->hist_len; i++)
    {
        cl->history[i] = tmp[i];
    }
    
    // free tmp
    free(tmp);

    // return
    return 0;
}

/* update passed buffer w/ tab complete
 * pre-conditions:  cl is setup, buffer is null terminated */
int _tab_complete(struct CL * cl, char * buffer)
{
    char * ptr;

    if ((ptr = strstr(buffer, " ")) == NULL)
    {
        /* tab-complete the command */
    }
    else
    {
        /* tab-complete the last arg */
        int size = 10;
        int len = 0;
        char ** matches = malloc(size * sizeof(char*));
        int longest = 0;


        // find last arg
        while(strstr(buffer, " ") != NULL)
            { ptr = strstr(buffer, " ") + 1; }

        // at start of last arg
        //for(
    }
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
            fputs("Entering foreground-only mode (& is now ignored)\n", stdout);
        }
        else if (bg_block_mode == 0)
        {
            fputs("Exiting foreground-only mode\n", stdout);
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
    if (cl->args[0] == NULL || strcmp(cl->args[0], "\n") == 0) { return 0; }

    // if exit (save the time of parsing)
    if (strcmp(cl->args[0], "exit") == 0) { return -1; }

    // check for special args
    if (_process_special_args(cl, &special_count,
                        &in_stream, &out_stream,
                        &in_redir, &out_redir, &background) != 0)
    {
        /* redirection error */
        if (!background)
        {
            // foreground, set status
            cl->fg_status = 1;
            cl->fg_exited = 1;
            cl->fg_signaled = 0;
        }
        
        return 0;
    }

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
                status = 0;
                j = waitpid(i, &status, 0);
                //while (j == 0) { j = waitpid(i, &status, WNOHANG); }
                is_child = 0;

                signal(SIGINT, _sigint_handler);

                /*
                printf("j = %d\n", j);
                printf("status = %d\n", status);
                printf("WIFSIGNALED = %d\n", WIFSIGNALED(status));
                printf("WIFEXITED = %d\n", WIFEXITED(status));
                */

                // if no waitpid error occurred
                if (j != -1)//status != 0)
                {
                    //printf("~~WAITPID %d\n", j);
                    if (WIFSIGNALED(status))
                    {
                        //printf("~~SIGNAL %d\n", WTERMSIG(status));
                        cl->fg_status = WTERMSIG(status);
                        cl->fg_exited = 0;
                        cl->fg_signaled = 1;
                    }
                    else if (WIFEXITED(status))
                    {
                        //printf("~~EXIT %d\n", WEXITSTATUS(status));
                        cl->fg_status = WEXITSTATUS(status);
                        cl->fg_exited = 1;
                        cl->fg_signaled = 0;
                    }
                    else
                    {
                        //printf("~~NOT SIGNALED OR EXITED\n");
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

            // following is only reached if execvp failed
            cl->fg_status = 1;
            cl->is_child = 1;
            result = -1;

            // print system error
            char perr[CL_BUFF_SIZE] = "smallsh: ";
            sprintf(perr, "%s%s", perr, cl->args[0]);
            perror(perr);
            
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
                char perr[CL_BUFF_SIZE] = "smallsh: ";
                sprintf(perr, "%s%s", perr, cl->args[i+1]);
                perror(perr);
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
                char perr[CL_BUFF_SIZE] = "smallsh: ";
                sprintf(perr, "%s%s", perr, cl->args[i+1]);
                perror(perr);
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
                char perr[CL_BUFF_SIZE] = "smallsh: ";
                sprintf(perr, "%s%s", perr, cl->args[i+1]);
                perror(perr);
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

    // set signal handlers
    sigint_action.sa_handler  = _sigint_handler;
    sigtstp_action.sa_handler = _sigtstp_handler;

    // assign signal actions with sigaction
    //sigaction(SIGINT,  &sigint_action,  NULL);
    signal(SIGINT, SIG_IGN);
    sigaction(SIGTSTP, &sigtstp_action, NULL);

    // command loop
    keep_going = 0;
    while (keep_going == 0)
    {
        // clear CL
        clear_CL(&cl);

        // check background
        pid_check_CL(&cl);

        // get commands
        if (get_input(&cl, in_buff, IN_BUFF_SIZE) != 0) { continue; };

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
