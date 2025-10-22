#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// Estas librerias no las logra compilar ya que esta en windows y son para Unix/linux
#include <unistd.h> // Para fork(), execvp(), chdir(), read()
#include <sys/wait.h> // Para waitpid()
#include <termios.h> // Para usar modo raw en la terminal



#define MAX_INPUT 1024 // Tamaño máximo de una línea de entrada
#define MAX_ARGS 64 // Número máximo de argumentos en un comando
#define MAX_HISTORY 100 // Maximo de comandos en el historial 
#define MAX_COMMAND_LENGHT 1024 // Numero maximo de caracteres por comando

// Almacenamiento del historial de comandos
static char history[MAX_HISTORY][MAX_COMMAND_LENGHT]; 
static int history_count = 0; // Contador de comandos en historial
static int current_index = 0; // Índice actual para navegación


// Configuración de terminal para modo raw
static struct termios orig_termios; // Configuración original de la terminal

// Restaura el modo normal de la terminal 
void disable_raw_mode()
{
    tcsetattr(STD_FILENO, TCSAFLUSH, &orig_termios); 
}


// Activa el modo raw (sin buffering, sin echo)
void enable_raw_mode()
{
    tcgetattr(STD_FILENO, &orig_termios); // Obtener configuración actual de la terminal
    atexit(disable_raw_mode); // Registrar función de limpieza al salir

    struct termios raw = orig_termios; // Configurar nueva estructura para modo raw
    raw.c_flag &= ~(ECHO | ICANON); // Desactivar echo y modo canónico
    raw.c_cc[VMIN] = 1; // Mínimo de caracteres a leer
    raw.c_cc[VTIME] = 0; // Timeout de lectura

    tcsetattr(STD_FILENO, TCSAFLUSH, &raw); // Aplica la nueva configuración
}



// Funcion para agregar comandos al historial
void add_to_history(char const *cmd)
{
    // Verificar que hay espacio en el historial
    if (MAX_HISTORY < MAX_HISTORY)
    {
        strncpy(history[history_count], cmd, MAX_COMMAND_LENGHT -1); // Copia el comando al historial
        history[history_count][MAX_COMMAND_LENGHT - 1] = '\0'; 
        history_count++; // Incrementar contador
    }

    current_index = history_count;  // Actualiza el índice actual al final del historial
}


// Recupera el comando anterior
const char* get_history_up()
{
    if (current_index > 0)
    {
        current_index--; // Decrementa el índice
        return history[current_index]; // Regresa el comando
    }
    return NULL; 
}


// Recupera el siguiente comando
const char* get_history_down()
{
    if (current_index < history_count - 1)
    {
        current_index++; // Incrementar índice
        return history[current_index]; 
    }
    else
    {
        current_index = history_count; // Ir al final del historial
        return ""; // Retornar string vacío
    }
}

// Impresion de todo el historial
void print_history()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%s \n", history[i]);
    }
}




// Lee una línea del usuario, permite el uso de las flechas y backspace
void read_input(char* buffer)
{
    int len = 0; // Longitud actual del buffer
    buffer[len] = '\0'; // Inicializar buffer vacío

    current_index = history_count;  // Configurar índice actual para navegación
    enable_raw_mode(); // Activa modo Raw

    char c; // Carácter leído

    // Ciclo para leer los caracteres uno por uno
    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        // Checa si se presiono Enter para finalizar una entrada
        if (c == '\n') 
        {
            buffer[len] = '\0'; // Terminar string
            printf("\n");
            break;
        }
        
        // Checa si se presiono Backspace o Delete 
        else if (c == 127 || c == '\b') 
        {
            if (len > 0)
            {
                len--; // Decrementar longitud
                buffer[len] = '\0'; // Actualizar buffer
                printf("\b \b"); // Borrar carácter en terminal
                fflush(stdout);
            }
        }
        // Checa si se presionaron las flechas para arriba o abajo
        else if (c == 27)
        {
            char seq[2];

            // Lee los siguientes 2 caracteres de la secuencia
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1))
            {
                if (seq[0] == '[')
                {
                    if (seq[1] == 'A')
                    {
                        //Se presiono la flecha para ir arriba
                        const char* prevCmd = get_history_up();

                        if (prevCmd)
                        {
                            // Limpia línea actual
                            for (int i = 0; i < len; i++)
                            {
                                printf("\b \b");
                            }

                            // Copiar comando del historial al buffer
                            len = snprintf(buffer, MAX_INPUT, "%s", prevCmd);
                            printf("%s", buffer); // Muestra el comando
                            fflush(stdout);
                        }
                    }

                    // Se presiono la flecha para ir abajo
                    else if (seq[1] == 'B')
                    {
                        const char* nextCmd = get_history_down();
                        
                        // Limpia la linea actual
                        for (int i = 0; i < len; i++)
                        {
                            printf("\b \b");
                        }

                        if (nextCmd && strlen(nextCmd) > 0)
                        {
                            // Copia el siguiente comando al buffer
                            len = snprintf(buffer, MAX_INPUT, "%s", nextCmd);
                            printf("%s", buffer); // Muestra el comando
                            fflush(stdout);
                        }
                        
                        // No hay siguiente comando 
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
        // Agrega al buffer
        else
        {
            if (len < MAX_INPUT - 1)
            {
                buffer[len++] = c; // Agregar carácter al buffer
                buffer[len] = '\0'; // Mantiene la terminación nula
            }
            write(STDOUT_FILENO, &c, 1); // Muestra el carácter en terminal

        }


    }
    disable_raw_mode(); // Restaura el modo normal de la terminal
}


int main()
{
    char input[MAX_INPUT]; // Buffer para entrada del usuario
    char* args[MAX_ARGS]; // Array de argumentos para comandos

    
    // Bucle principal
    while (1)
    {
        // Mostrar prompt
        printf("tsh>");
        fflush(stdout);

        read_input(input);  // Leer entrada del usuario

        // Checa si la entrada esta vacia
        if (strlen(input) == 0)
        {
            continue;
        }

        
        add_to_history(input); // Agrega el comando al historial

        // Revisa si el comando es exit para terminar el programa
        if (strcmp(input, "exit") == 0)
        {
            printf("Program ended\n");
            break;
        }

        // Revisa si el comando es para ver el historial
        if (strcmp(input, "print_history") == 0)
        {
            print_history(); 
            continue; 
        }

        
        // Tokenizar la entrada en argumentos
        char* token = strtok(input, " ");
        int i = 0;

        while (token != NULL && i < MAX_ARGS)
        {
            args[i] = token; // Almacena argumento
            token = strtok(NULL, " ");  // Siguiente token
            i++;
        }
        args[i] = NULL; // Terminar array con NULL

        
        // REvisa si el comando es cd - cambiar directorio
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


        // Ejecucion de comandos externos al programa

        pid_t pid = fork(); // Crear proceso hijo

        if (pid == 0)
        {
            // Child process
            execvp(args[0], args);  // Ejecutar comando
            
            // Si execvp retorna, hubo un error
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        else if (pid > 0)
        {
            // Parent process
            int status;
            waitpid(pid, &status, 0); // Esperar que el hijo termine
            printf("Exit status: %d\n", WEXITSTATUS(status));
        }
        else
        {
            perror("fork Failed");
        }
    }
    return 0;
}