#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Estas librerias no las logra compilar ya que esta en windows y son para Unix/linux
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

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