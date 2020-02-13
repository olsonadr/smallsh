# smallsh

smallsh is a small shell program with basic process management and redirection capabilities.

## Installation

Use the provided makefile or a gcc command to compile the program into an executable.
```bash
make
```
or
```bash
gcc -o ./smallsh ./smallsh.c
```

## Usage

Launch the shell by running the program.
```bash
~ ./smallsh
```
Input any command contained in the current PATH varaible at the prompt (or built in commands "exit", "status", and "cd").
```bash
~ ./smallsh
: ls
smallsh.c    smallsh    README.txt
: exit
~ ```

## Current Features
- Parsing special characters (each must be separated from the rest of the command by spaces)
  - I/O redirection with special characters '>', '>>', and '<'
      - Each of these is followed by a space and then the name of the file to be used
        - Background processes using special character '&'
            - This must be at the end of the command line
            - Arrow key handling
              - Up and Down arrow keys move between history of commands of the session
                - Left and Right arrow keys move through line as if terminal were in canonical mode
                - Background processes
                  - A command ending in the character '&' are placed in the background. The user is given the process id of the child process. Before the first input of the user after the process has completed, the exit status of the child is printed
                    - These background processes are not interrupted by a SIGINT signal
                    - Signal handling
                      - SIGTSTP
                          - If in foreground process, before next input (or if sitting at input, immediately) toggle a "foreground only mode" where the '&' special character is ignored (as if it were not inputted) and so new background processes may not be started
                            - SIGINT
                                - Ignored if sitting at prompt, signals foreground child to terminate if one is currently executing


## TODO
- Tab completion
  - Tab complete program names as well as file structures

## License
[MIT](https://choosealicense.com/licenses/mit/)
