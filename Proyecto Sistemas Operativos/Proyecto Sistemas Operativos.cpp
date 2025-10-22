#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// Estas librerias no las logra compilar ya que esta en windows y son para Unix/linux
#include <unistd.h>
#include <sys/wait.h>
#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100 // Maximo de comandos en el historial 
#define MAX_COMMAND_LENGHT 1024 // Numero maximo de caracteres por comando

static char history[MAX_HISTORY][MAX_COMMAND_LENGHT]; 
static int history_count = 0; 
static int current_index = 0; 


// Funcion para agregar comandos al historial
void add_to_history(char constr *cmd)
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
        return history[history_count]; 
    }
    else
    {
        current_index = history_count; 
        return ""; 
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

        if (fgets(input, MAX_INPUT, stdin) == NULL)
        {
            perror("fgets failed");
            continue;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
        {
            printf("Program ended\n");
            break;
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