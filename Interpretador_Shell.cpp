#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
// Estas librerias no las logra compilar ya que esta en windows y son para Unix/linux
#include <unistd.h> // Para fork(), execvp(), chdir(), read()
#include <sys/wait.h> // Para waitpid()
#include <termios.h> // Para usar modo raw en la terminal

#define MAX_INPUT 1024 // Tamaño máximo de una línea de entrada
#define MAX_ARGS 64 // Número máximo de argumentos en un comando
#define MAX_HISTORY 100 // Maximo de comandos en el historial 
#define MAX_COMMAND_LENGHT 1024 // Numero maximo de caracteres por comando

// Almacenamiento del historial de comandos
static std::string history[MAX_HISTORY];
static int history_count = 0; // Contador de comandos en historial
static int current_index = 0; // Índice actual para navegación

// Configuración de terminal para modo raw
static struct termios orig_termios; // Configuración original de la terminal

// Restaura el modo normal de la terminal 
void disable_raw_mode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Activa el modo raw (sin buffering, sin echo)
void enable_raw_mode()
{
    tcgetattr(STDIN_FILENO, &orig_termios); // Obtener configuración actual de la terminal
    atexit(disable_raw_mode); // Registrar función de limpieza al salir

    struct termios raw = orig_termios; // Configurar nueva estructura para modo raw
    raw.c_lflag &= ~(ECHO | ICANON); // Desactivar echo y modo canónico
    raw.c_cc[VMIN] = 1; // Mínimo de caracteres a leer
    raw.c_cc[VTIME] = 0; // Timeout de lectura

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // Aplica la nueva configuración
}

// Funcion para agregar comandos al historial
void add_to_history(const std::string& cmd)
{
    // Verificar que hay espacio en el historial
    if (history_count < MAX_HISTORY)
    {
        history[history_count] = cmd;
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
        return history[current_index].c_str(); // Regresa el comando
    }
    return nullptr;
}

// Recupera el siguiente comando
const char* get_history_down()
{
    if (current_index < history_count - 1)
    {
        current_index++; // Incrementar índice
        return history[current_index].c_str();
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
        std::cout << history[i] << std::endl;
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
            std::cout << std::endl;
            break;
        }

        // Checa si se presiono Backspace o Delete 
        else if (c == 127 || c == '\b')
        {
            if (len > 0)
            {
                len--; // Decrementar longitud
                buffer[len] = '\0'; // Actualizar buffer
                std::cout << "\b \b"; // Borrar carácter en terminal
                std::cout.flush();
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
                                std::cout << "\b \b";
                            }

                            // Copiar comando del historial al buffer
                            len = snprintf(buffer, MAX_INPUT, "%s", prevCmd);
                            std::cout << buffer; // Muestra el comando
                            std::cout.flush();
                        }
                    }

                    // Se presiono la flecha para ir abajo
                    else if (seq[1] == 'B')
                    {
                        const char* nextCmd = get_history_down();

                        // Limpia la linea actual
                        for (int i = 0; i < len; i++)
                        {
                            std::cout << "\b \b";
                        }

                        if (nextCmd && strlen(nextCmd) > 0)
                        {
                            // Copia el siguiente comando al buffer
                            len = snprintf(buffer, MAX_INPUT, "%s", nextCmd);
                            std::cout << buffer; // Muestra el comando
                            std::cout.flush();
                        }
                        // No hay siguiente comando 
                        else
                        {
                            len = 0;
                            buffer[len] = '\0';
                            std::cout.flush();
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

    // Hashmap de comandos internos (built-in commands)
    std::unordered_map<std::string, std::function<bool(char* [])>> command;

    // Comando "exit"
    command["exit"] = [](char* []) -> bool 
        {
            std::cout << "Program ended" << std::endl;
            exit(0);
            return true;
        };

    // Comando "print_history"
    command["print_history"] = [](char* []) -> bool 
        {
            print_history();
            return true;
        };

    // Comando "cd"
    command["cd"] = [](char* args[]) -> bool 
        {
            if (args[1] == nullptr)
            {
                std::cerr << "cd: Missing argument" << std::endl;
            }
            else if (chdir(args[1]) != 0)
            {
                perror("cd failed");
            }
            return true;
        };

    // Bucle principal
    while (true)
    {
        // Mostrar prompt
        std::cout << "tsh>";
        std::cout.flush();

        read_input(input);  // Leer entrada del usuario

        // Elimina saltos de línea o retorno de carro
        input[strcspn(input, "\r\n")] = 0;

        // Checa si la entrada esta vacia
        if (strlen(input) == 0)
        {
            continue;
        }

        add_to_history(input); // Agrega el comando al historial



        // Tokenizar la entrada en argumentos
        char* token = strtok(input, " ");
        int i = 0;

        while (token != nullptr && i < MAX_ARGS)
        {
            args[i] = token; // Almacena argumento
            token = strtok(nullptr, " ");  // Siguiente token
            i++;
        }
        args[i] = nullptr; // Terminar array con NULL

        std::string cmd = args[0];

        // Verifica si el comando está en el hashmap
        auto it = command.find(cmd);
        if (it != command.end())
        {
            // Ejecuta el comando interno
            it->second(args);
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
            std::cout << "Exit status: " << WEXITSTATUS(status) << std::endl;
        }
        else
        {
            perror("fork Failed");
        }
    }
    return 0;
}
