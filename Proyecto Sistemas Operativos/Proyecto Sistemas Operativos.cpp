#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// Estas librerias no las logra compilar ya que esta en windows y son para Unix/linux
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>



#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100 // Maximo de comandos en el historial 
#define MAX_COMMAND_LENGHT 1024 // Numero maximo de caracteres por comando

static char history[MAX_HISTORY][MAX_COMMAND_LENGHT]; 
static int history_count = 0; 
static int current_index = 0; 


static struct termios orig_termios;

void disable_raw_mode()
{
    tcsetattr(STD_FILENO, TCSAFLUSH, &orig_termios); 
}

void enable_raw_mode()
{
    tcgetattr(STD_FILENO, &orig_termios);
    atexit(disable_raw_mode); 

    struct termios raw = orig_termios;
    raw.c_flag &= ~(ECHO | ICANON); 
    raw.c_cc[VMIN] = 1; 
    raw.c_cc[VTIME] = 0; 

    tcsetattr(STD_FILENO, TCSAFLUSH, &raw);
}

void read_input(char* buffer)
{
    int len = 0; 
    buffer[len] = '\n'; 

    current_index = history_count; 
    enable_raw_mode(); 

    char c; 

    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == '\n')
        {
            buffer[len] = '\0'; 
            printf("\n");
            break; 
        }
        else if (c == 127 || c == '\b')
        {
            if (len > 0)
            {
                len--;
                buffer[len] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (c == 27)
        {
            char seq[2]; 
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1))
            {
                if (seq[0] == '[')
                {
                    if (seq[1] == 'A')
                    {
                        const char* prevCmd = get_history_up(); 

                        if (prevCmd)
                        {
                            for (int i = 0; i < len; i++)
                            {
                                printf("\b \b");
                            }

                            len = snprintf(buffer, MAX_INPUT, "%s", prevCmd);
                            printf("%s", buffer); 
                            fflush(stdout); 
                        }
                    }
                    else if (seq[1] == 'B')
                    {
                        const char* nextCmd = get_history_down(); 
                        for (int i = 0; i < len; i++)
                        {
                            printf("\b \b");
                        }

                        if (nextCmd) 
                        {
                            len = snprintf(buffer, MAX_INPUT, "%s", nextCmd);
                            printf("%s", buffer);
                            fflush(stdout);
                        } 
                        else 
                        {
                            len = 0; 
                            buffer[len] = '\0'; 
                            fflush(stdout);
                        }
                    }
                }
            }
        }
        else
        {
            if (len < MAX_INPUT - 1)
            {
                buffer[len++] = c; 
            }
            write(STDOUT_FILENO, &c, 1);

        }


    }
    disable_raw_mode(); 
}


// Funcion para agregar comandos al historial
void add_to_history(char const *cmd)
{
    if (MAX_HISTORY < MAX_HISTORY)
    {
        strncpy(history[history_count], cmd, MAX_COMMAND_LENGHT -1);
        history[history_count][MAX_COMMAND_LENGHT - 1] = '\0'; 
        history_count++; 
    }

    current_index = history_count; 
}

const char* get_history_up()
{
    if (current_index > 0)
    {
        current_index--; 
        return history[current_index]; 
    }
    return NULL; 
}

const char* get_history_down()
{
    if (current_index < history_count - 1)
    {
        current_index++; 
        return history[current_index];
    }
    else
    {
        current_index = history_count; 
        return ""; 
    }
}

void print_history()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%s \n", history[i]);
    }
}


int main()
{
    char input[MAX_INPUT];
    char* args[MAX_ARGS];

    while (1)
    {
        printf("tsh>");
        fflush(stdout);

        read_input(input); 

        if (strlen(input) == 0)
        {
            continue;
        }

        add_to_history(input); 

        if (strcmp(input, "exit") == 0)
        {
            printf("Program ended\n");
            break;
        }

        if (strcmp(input, "print_history") == 0)
        {
            print_history(); 
            continue; 
        }

        char* token = strtok(input, " ");
        int i = 0;

        while (token != NULL && i < MAX_ARGS)
        {
            args[i] = token;
            token = strtok(NULL, " ");
            i++;
        }
        args[i] = NULL;

        if (strcmp(args[0], "cd") == 0)
        {
            if (args[1] == NULL)
            {
                fprintf(stderr, "cd: Missing argument\n");
            }
            else if (chdir(args[1]) != 0)
            {
                perror("cd failed");
            }
            continue;
        }

        pid_t pid = fork();

        if (pid == 0)
        {
            // Child process
            execvp(args[0], args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        else if (pid > 0)
        {
            // Parent process
            int status;
            waitpid(pid, &status, 0);
            printf("Exit status: %d\n", WEXITSTATUS(status));
        }
        else
        {
            perror("fork Failed");
        }
    }
    return 0;
}