/*
 * program -    smallsh
 * author -     Nicholas Olson
 */

/*** includes ***/
#include <stdlib.h> // std library stuff
#include <stdio.h>  // I/O stuff
#include <unistd.h> // launching processess
#include <string.h> // for strlen, strcmp, etc


/*** defines ***/
#define CL_BUFF_SIZE 2048
#define CL_ARGS_SIZE 512
#define IN_BUFF_SIZE 2048


/*** structs ***/
struct CL {
    // overall array of input
    char * buffer;
    
    // array of space-delineated arguments
    char ** args;
    int num_args;

    // mutex lock
};


/*** interface prototypes ***/
int setup_CL(struct CL*);           // setup CL struct (allocate)
int free_CL(struct CL*);            // destroy CL struct (free)
int run_CL(struct CL*, char*);      // parse and execute line of command
int get_input(char*, int);          // get user input and put it in buffer
int clear_CL(struct CL*);           // clear CL struct to neutral state
int main(int, char**);              // main runtime


/*** hidden prototypes ***/
int _parse_input(struct CL*, char*); // parse a string into a command line struct
int _execute_CL(struct CL*);         // execute command specified by CL_buffer
int _print(char*, FILE*);            // print string to file pointer passed


/*** built-in prototypes ***/
int _CL_exit(FILE*, FILE*, int, char**, pthread_t*);   // exit command
int _CL_cd(FILE*, FILE*, int, char**, pthread_t*);     // cd command
int _CL_status(FILE*, FILE*, int, char**, pthread_t*); // status command


/*** interface methods ***/
/* allocate memory for CL struct */
int setup_CL(struct CL * cl)
{
    // mallocs
    cl->buffer = malloc(CL_BUFF_SIZE * sizeof(char));
    cl->args = malloc(CL_BUFF_SIZE * sizeof(char*));
    cl->num_args = 0;
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

    // parse into args array
    for (end = 0; end < len; end++)
    {
        // beginning and/or end of word
        if ((cl->buffer[end] == ' ') || (end == len - 1))
        {
            // if there has been at least one char since last space
            if ((end - start > 0) || ((end == len - 1) && (end - start == 1)))
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

    // return
    return 0;
}

/* executes the command contained within the CL struct
 * pre-condition:   cl has been setup */
int _execute_CL(struct CL * cl)
{
    // declarations
    FILE * in_stream = stdin;
    FILE * out_stream = stdout;
    int special_count;
    int result;
    int i;

    // check if command was not empty
    if (cl->num_args == 0) { return 0; }

    // special argument vars
    special_count = 0;

    // check for special args
    for (i = 0; i < cl->num_args; i++)
    {
        if (strcmp(cl->args[i], "<") == 0)
        {
            // new input file
            special_count += 2;

            // "<" is not last arg
            in_stream = fopen(cl->args[i+1], "r");
        }
        else if (strcmp(cl->args[i], ">") == 0)
        {
            // new output file
            special_count += 2;
           
            // ">" is not last arg
            out_stream = fopen(cl->args[i+1], "w");
        }
        else if (strcmp(cl->args[i], "&") == 0)
        {
            // open in background
            special_count += 1;

            // background
            in_stream = NULL;
            out_stream = NULL;
        }
    }

    // create mutex thread
    // *******************

    pthread_t pid_wow;
    pthread_t * pid = &pid_wow;

    // run program
    result = 0;
    if (strcmp(cl->args[0], "cd") == 0)
    { 
        result = _CL_cd(in_stream, out_stream,
                        (cl->num_args - special_count - 1),
                        cl->args + 1, pid);
    }
    else if (strcmp(cl->args[0], "exit") == 0)
    {
        result = _CL_exit(in_stream, out_stream,
                            (cl->num_args - special_count - 1),
                            cl->args + 1, pid);
    }
    else if (strcmp(cl->args[0], "status") == 0)
    {
        result = _CL_status(in_stream, out_stream,
                            (cl->num_args - special_count - 1),
                            cl->args + 1, pid);
    }
    else
    {
    }

    // destroy thread
    // *******************
    
    // close streams
    if (in_stream != NULL && in_stream != stdin) { fclose(in_stream); }
    else if (out_stream != NULL && out_stream != stdout) { fclose(out_stream); }

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


/*** built-ins ***/
/* built-in exit command (exits the shell) */
int _CL_exit(FILE * in_stream, FILE * out_stream,
             int argc, char ** argv, pthread_t * pid)
{
    // lock mutex thread
    _print("exiting\n", out_stream);
    return -1;
}

/* built-in cd command (change directory) */
int _CL_cd(FILE * in_stream, FILE * out_stream,
           int argc, char ** argv, pthread_t * pid)
{
    // lock mutex thread
}

/* built-in status command (shows shell status) */
int _CL_status(FILE * in_stream, FILE * out_stream,
               int argc, char ** argv, pthread_t * pid)
{
    // lock mutex thread
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
