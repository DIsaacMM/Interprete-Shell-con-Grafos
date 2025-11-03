// Librerias Interpretador Shell
	#include <iostream>
	#include <string>
	#include <vector>
	#include <unordered_map>
	#include <cstring>
	#include <cstdlib>
// Librerias de Unix/linux
	#include <unistd.h> // Para fork(), execvp(), chdir(), read()
	#include <sys/wait.h> // Para waitpid()
	#include <termios.h> // Para usar modo raw en la terminal

// Libreria para DAGman
#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <sstream>
#include <algorithm>

	using namespace std;

// Variables Globales Interpetador Shell
	#define MAX_INPUT 1024 // Tamaño máximo de una línea de entrada
	#define MAX_ARGS 64 // Número máximo de argumentos en un comando
	#define MAX_HISTORY 100 // Maximo de comandos en el historial 
	#define MAX_COMMAND_LENGHT 1024 // Numero maximo de caracteres por comando

// Variables Globales DAGman
	unordered_map<string, shared_ptr<Node>> nodes; // Hash map global de nodos
	mutex q_mtx, log_mtx;                          // Mutex para proteger que los hilos de las cola y logs entren a las seccion critica 
	deque<shared_ptr<Node>> ready_q;               // Cola de tareas listas
	condition_variable q_cv;                       // Sistema de notificación que despierta a los hilos cuando hay trabajo
	atomic<int> remaining_tasks{ 0 };                // Contador atomico de tareas restantes por terminar
	atomic<int> active_tasks{ 0 };                   // Contador atomico de tareas que están ejecutándose
	int max_concurrency = 4;                       // Máximo de tareas (hilos) que pueden ejecutarse simultaneamente
	int max_retries = 1;                           // Reintentos máximos por tarea

// Estructuras Interpretador Shell
	// Almacenamiento del historial de comandos
		static std::string history[MAX_HISTORY];
		static int history_count = 0; // Contador de comandos en historial
		static int current_index = 0; // Índice actual para navegación

	// Configuración de terminal para modo raw
		static struct termios orig_termios; // Configuración original de la terminal

    // Hashmap de comandos internos (built-in commands)
        std::unordered_map<std::string, std::function<bool(char* [])>> command;

// Estructuras DAGman
	//    Estado actual de un nodo (tarea)
		enum class Status
		{
			PENDING,  // La tarea aún no se ha ejecutado
			RUNNING,  // Está ejecutándose actualmente
			SUCCESS,  // Terminó exitosamente (exit code == 0)
			FAILED    // Falló definitivamente tras agotar reintentos
		};

	// Estructura de nodo con informacion de una tarea
		struct Node 
		{
			string id;                                  // ID o nombre del nodo 
			string cmd;                                 // Comando shell a ejecutar
			vector<string> deps;                        // Lista de dependencias (archivos previos)
			vector<string> children;                    // Nodos que dependen de este nodo
			atomic<int> indeg{ 0 };                     // Número de dependencias pendientes (in-degree)
			atomic<Status> status{ Status::PENDING };   // Estado actual
			int retries = 0;                            // Número de intentos realizados
		};
	
    // Estructura para quitar espacios 
        static inline string trim(const string& s) 
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }


// Funciones DAGman
    // Detección simple de ciclos — DFS
        bool detect_cycle_dfs(const string& node_id,
            unordered_map<string, int>& state) 
        { // 0=unvisited,1=visiting,2=done
            state[node_id] = 1;
            auto node = nodes[node_id];
            for (const auto& dep : node->deps) 
            {
                if (nodes.find(dep) == nodes.end()) continue; // depende se checó antes
                if (state[dep] == 1) return true;
                if (state[dep] == 0) {
                    if (detect_cycle_dfs(dep, state)) return true;
                }
            }
            state[node_id] = 2;
            return false;
        }
     // Funcion para detectar ciclos
        bool has_cycle() 
        {
            unordered_map<string, int> state;
            for (const auto& p : nodes) state[p.first] = 0;
            for (const auto& p : nodes) {
                if (state[p.first] == 0) {
                    if (detect_cycle_dfs(p.first, state)) return true;
                }
            }
            return false;
        }

    // Funcion Creacion del DAG
        void DAG_create()
    {
        vector<string> dag_commands; // Almacena los comandos DAG escritos
        dag_commands.clear();        // Borra todos los comandos que esten almacenados en el vector

        cout << "Modo DAGman Iniciado \n "; 
        cout << " Escribe comandos DAG con el siguiente formato: ArchivoSalida - Comando - Dependencias(Separadas por coma) \n"; 
        cout << "Escribe 'DAG_execute' para ejecutar o 'DAG_exit' para salir \n";

        string input;                                                   // Variable con la entrada de comandos en formato DAG
        while (true)
        {                                              
            input.clear();                                              // Se borra todo el contenido de input para evitar trasponer comandos
            cout << "DAG> ";                                            // Formato para terminal
            getline(cin, input);                                        // Se introduce un comando en el formato DAG
            
            if (input == "DAG_execute")                               // Checar si el comando que se puso es DAG_execute()
            {
                DAG_execute(dag_commands);                              // Ejecuta la funcion para ejecutar todo el DAG
            }
            
            else if (input == "DAG_exit") 
            {
                break;                                                  // Salir del bucle
            }
            
            else 
            {
                dag_commands.push_back(input);                          // Agregar comando al vector de comandos DAG
            }
        }
    }

    // Funcion Ejecucion DAG
        void DAG_execute(std::vector<std::string>& dag_commands)
        {
            if (dag_commands.empty()) 
            {
                cout << "No hay comandos DAG para ejecutar" << endl; 
                return;
            }

            cout << "Iniciando la ejecucion del DAG \n" << endl; 

            // Limpiar estructuras globales para nueva ejecucion
                lock_guard<mutex> lg(q_mtx);
                nodes.clear(); 
                ready_q.clear(); 
                remaining_tasks = 0;
                active_tasks = 0;

            // 1. Parsear todos los comandos y crear nodos
                for (const string& dag_cmd : dag_commands) // For recorre cada comando en el vector de comandos
                {
                    // Parsear el comando DAG: ArchivoSalida - Comando - Dependencias
                    size_t first_dash = dag_cmd.find(" - "); // Busca el primer " - " en el comando DAG
                    size_t second_dash = dag_cmd.find(" - ", first_dash + 3); // Busca el segundo " - " empezando despues del primero

                    // Verifica si no se encontró el primer separador (formato inválido)
                        if (first_dash == string::npos)
                        {
                            cout << "Error: Formato inválido en comando: " << dag_cmd << endl;
                            continue;
                        }
                
                
                    string output_file = trim(dag_cmd.substr(0, first_dash)); // Extrae el nombre del archivo de salida (desde inicio hasta primer separador)
                    string command; // Se declara un string para los comamdos
                    vector<string> dependencies; // Se declara un vector para llevar cuenta de las dependencias

                    // Verifica si no hay segundo separador (solo comando sin dependencias)
                        if (second_dash == string::npos)
                        {
                            // Solo hay comando, sin dependencias
                            command = trim(dag_cmd.substr(first_dash + 3)); // Extrae el comando (después del primer separador hasta el final)
                        }
                        else
                        {
                            // Hay comando y dependencias
                                command = trim(dag_cmd.substr(first_dash + 3, second_dash - (first_dash + 3))); // Extrae el comando (entre primer y segundo separador)
                                string deps_str = dag_cmd.substr(second_dash + 3); // Extrae la cadena de dependencias (después del segundo separador)

                            // Parsear dependencias separadas por comas
                            size_t start = 0;
                            size_t end = deps_str.find(',');
                        
                            // Itera sobre todas las dependencias separadas por comas
                                while (end != string::npos)
                                {
                                    // Agrega cada dependencia al vector
                                        dependencies.push_back(deps_str.substr(start, end - start));
                                        start = end + 1;
                                        end = deps_str.find(',', start);
                                }
                            dependencies.push_back(deps_str.substr(start)); // Agrega la última dependencia (después de la última coma)
                        }

                    // Crear Nodo
                    auto node = make_shared<Node>(); // Crea un nuevo nodo usando smart pointer (shared_ptr)
               
                    // Configura las propiedades del nodo
                        node->id = output_file;                     // ID = archivo de salida
                        node->cmd = command;                        // Comando a ejecutar
                        node->deps = dependencies;                  // Lista de dependencias
                        node->status = Status::PENDING;             // Estado inicial: pendiente
                        node->retries = 0;                          // Contador de reintentos en 0
                        node->indeg = dependencies.size();          // Grado de entrada = número de dependencias

                
                    
                    nodes[output_file] = node;  // Almacena el nodo en el mapa usando el output_file como clave
                    cout << "Nodo creado: " << output_file << " -> " << command << endl; // Muestra información del nodo creado
                
                    // Si hay dependencias, las muestra
                        if (!dependencies.empty())
                        {
                            cout << "  Dependencias: ";
                            for (const auto& dep : dependencies)
                            {
                                cout << dep << " ";
                            }
                            cout << endl;
                        }
                }
            
                // 2. Construir relaciones de hijos y verificar dependencias
                    for (const auto& pair : nodes) // Itera sobre cada par clave-valor en el mapa de nodos
                    {
                        const auto& node = pair.second; // Obtiene el nodo actual del par (pair.second contiene el shared_ptr<Node>)
                
                        for (const string& dep : node->deps) // Itera sobre cada dependencia del nodo actual
                        {
                            // Verifica si la dependencia existe en el mapa de nodos
                                if (nodes.find(dep) == nodes.end())
                                {
                                    cout << "Error: Dependencia '" << dep << "' no encontrada para nodo '" << node->id << "'" << endl; // Si la dependencia no existe muestra error y termina la ejecución
                                    return;
                                }
                    
                             nodes[dep]->children.push_back(node->id); // Si la dependencia existe, agrega el nodo actual como hijo de la dependencia
                        }
                    }

                    if (has_cycle()) 
                    {
                        cout << "Error: Se detectó un ciclo en el DAG. Abortando ejecución.\n";
                        return;
                    }

                // 3. Inicializar cola de tareas listas (sin dependencias)
                    {
                        lock_guard<mutex> lock(q_mtx); // Crea un lock_guard para sincronizar el acceso a la cola (evita condiciones de carrera)
                
                        for (const auto& pair : nodes) // Itera sobre todos los nodos en el mapa para encontrar los listos para ejecutar
                        {
                            if (pair.second->indeg == 0) // Verifica si el nodo tiene grado de entrada 0 (no tiene dependencias pendientes)
                            {
                                ready_q.push_back(pair.second);  // Agrega el nodo a la cola de listos para ejecución
                            }
                        }
                        remaining_tasks = nodes.size();  // Inicializa el contador de tareas pendientes con el total de nodos
                    }
            
                    // Muestra información de depuración sobre el estado inicial
                        cout << "Nodos listos para ejecutar: " << ready_q.size() << endl;
                        cout << "Total de nodos: " << nodes.size() << endl;

                // 4. Crear hilos workers

                        vector<thread> workers; // Crea un vector para almacenar los threads workers
                    
                        for (int i = 0; i < max_concurrency; ++i) // Crea tantos workers como el máximo de concurrencia especificado
                        {
                            // Crea un nuevo thread worker con una función lambda
                            workers.emplace_back([i]() 
                                {
                                    // Ciclo while que se ejecutara hasta que no haya trabajo 
                                        while (true)
                                        {
                                            shared_ptr<Node> task;

                                            // Obtener tarea de la cola
                                            {
                                                unique_lock<mutex> lock(q_mtx); // Usa unique_lock para la variable condición (permite unlock/relock)
                                            
                                                // Espera hasta que haya tareas en la cola O no queden tareas pendientes
                                                    q_cv.wait(lock, []()
                                                        {
                                                            return !ready_q.empty() || remaining_tasks == 0;
                                                        });

                                                // Condición de salida: cola vacía y no quedan tareas pendientes
                                                    if (ready_q.empty() && remaining_tasks == 0)
                                                    {
                                                        break;
                                                    }
                                                // Si hay tareas en la cola, toma una
                                                    if (!ready_q.empty())
                                                    {
                                                        task = ready_q.front();  // Obtiene la primera tarea
                                                        ready_q.pop_front();     // La remueve de la cola
                                                        active_tasks++;          // Incrementa contador de tareas activas
                                                    }
                                            }
                                        
                                            // Si se obtuvo una tarea válida, procesarla
                                                if (task)
                                                {
                                                    // Ejecutar la tarea
                                                    task->status = Status::RUNNING; // Cambia el estado de la tarea a "ejecutándose"

                                                    // Log: informa que comienza la ejecución
                                                        {
                                                            lock_guard<mutex> log_lock(log_mtx);
                                                            cout << "[Worker " << i << "] Ejecutando: " << task->id << " -> " << task->cmd << endl;
                                                        }

                                                    // Ejecutar comando del sistema
                                                        int result = system(task->cmd.c_str());

                                                        // Log: informa el resultado de la ejecución
                                                            {
                                                                lock_guard<mutex> log_lock(log_mtx);
                                                                if (result == 0)
                                                                {
                                                                    cout << "[Worker " << i << "] ✓ Éxito: " << task->id << endl;
                                                                    task->status = Status::SUCCESS;
                                                                }
                                                                else
                                                                {
                                                                    cout << "[Worker " << i << "] ✗ Fallo: " << task->id << " (código: " << result << ")" << endl;
                                                                    task->status = Status::FAILED;
                                                                }
                                                            }

                                                    // Notificar a los hijos que esta tarea terminó
                                                    {
                                                        lock_guard<mutex> lock(q_mtx);
                                                        active_tasks--;      // Reduce contador de tareas activas
                                                        remaining_tasks--;   // Reduce contador de tareas pendientes totales

                                                    
                                                        for (const string& child_id : task->children)  // Para cada nodo hijo (dependiente) de esta tarea
                                                        {
                                                            auto child = nodes[child_id];
                                                            // Reduce el grado de entrada del hijo
                                                                if (--(child->indeg) == 0)
                                                                {
                                                                    ready_q.push_back(child); // Si ya no tiene dependencias pendientes, lo agrega a la cola
                                                                }
                                                        }
                                                    }

                                                    q_cv.notify_all(); // Notifica a todos los workers que el estado cambió
                                                }
                                        }
                                });
                        }

                // 5. Esperar a que todos los workers terminen
                    for (auto& worker : workers)
                    {
                        worker.join();
                    }
            

                // Paso 6: Reporte final
                    cout << "\n EJECUCIÓN DAG COMPLETADA " << endl;
                    int success_count = 0;
                    int failed_count = 0;

                    for (const auto& pair : nodes)
                    {
                        if (pair.second->status == Status::SUCCESS)
                        {
                            success_count++;
                        }
                        else if (pair.second->status == Status::FAILED)
                        {
                            failed_count++;
                            cout << "Fallo: " << pair.first << endl;
                        }
                    }

                    cout << "Resumen: " << success_count << " éxitos, " << failed_count << " fallos" << endl;

                    if (failed_count == 0)
                    {
                        cout << "Todos los comandos se ejecutaron exitosamente" << endl;
                    }
                    else
                    {
                        cout << "Algunos comandos fallaron" << endl;
                    }
        }

// Funciones Interpretador_Shell

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


//Funcion Main Principal 
    int main()
    {
        char input[MAX_INPUT]; // Buffer para entrada del usuario
        char* args[MAX_ARGS]; // Array de argumentos para comandos
        
        
        // Comando "DAG_create()"
        command["DAG_create"] = [](char* []) -> bool
            {
                DAG_create();
                return true;
            };


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
