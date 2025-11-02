#include <bits/stdc++.h>
using namespace std;


//    Estado actual de un nodo (tarea)
enum class Status
{
    PENDING,  // La tarea aún no se ha ejecutado
    RUNNING,  // Está ejecutándose actualmente
    SUCCESS,  // Terminó exitosamente (exit code == 0)
    FAILED    // Falló definitivamente tras agotar reintentos
};

// Estructura de nodo sencilla con informacion de una tarea
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


// Variables globales
unordered_map<string, shared_ptr<Node>> nodes; // Hash map global de nodos
mutex q_mtx, log_mtx;                          // Mutex para cola y logs
deque<shared_ptr<Node>> ready_q;               // Cola de tareas listas
condition_variable q_cv;                       // Condición para notificar hilos
atomic<int> remaining_tasks{ 0 };                // Cuántas tareas faltan por terminar
atomic<int> active_tasks{ 0 };                   // Cuántas están ejecutándose
int max_concurrency = 4;                       // Máximo número de hilos simultáneos
int max_retries = 1;                           // Reintentos máximos por tarea


// Funcion de impresion de mensajes 
void logmsg(const string& msg) 
{
    lock_guard<mutex> lock(log_mtx);
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    cout << "[" << put_time(localtime(&now), "%H:%M:%S") << "] " << msg << endl;
}

//Funcion para ejecutar un comando en el shell del sistema. Esta funcion se va a quitar posiblemente
bool run_command(const string& cmd) {
    int rc = system(cmd.c_str());
    return rc == 0; //Retorna true si el comando devuelve código de salida 0.
}

/*
    FUNCIÓN: worker_thread
    Cada hilo del pool ejecuta esta función en bucle.
    - Espera a que haya nodos listos en la cola.
    - Ejecuta el comando del nodo.
    - Actualiza dependencias de sus hijos.
*/
void worker_thread() {
    while (true) {
        shared_ptr<Node> node;

        // Bloque sincronizado: obtener siguiente tarea
        {
            unique_lock<mutex> lk(q_mtx);
            q_cv.wait(lk, [] { return !ready_q.empty() || remaining_tasks == 0; });

            // Si ya no hay tareas pendientes, salir del hilo
            if (remaining_tasks == 0 && ready_q.empty())
                return;

            // Extraer primer nodo de la cola
            node = ready_q.front();
            ready_q.pop_front();
            node->status = Status::RUNNING;
            active_tasks++;
        }

        // Log: inicio de tarea
        logmsg("Ejecutando: " + node->id + " → " + node->cmd);

        // Ejecutar el comando del nodo
        bool ok = run_command(node->cmd);

        // Evaluar resultado
        if (ok) 
        {
            node->status = Status::SUCCESS;
            logmsg("Completado: " + node->id);
        }
        else 
        {
            node->retries++;
            if (node->retries <= max_retries) 
            {
                // Si puede reintentarse, lo reencola
                node->status = Status::PENDING;
                logmsg("Falló: " + node->id + " (Reintento " + to_string(node->retries) + ")");
                {
                    lock_guard<mutex> lk(q_mtx);
                    ready_q.push_back(node);
                }
                q_cv.notify_all();
                active_tasks--;
                continue;
            }
            else 
            {
                node->status = Status::FAILED;
                logmsg("Falló permanentemente: " + node->id);
            }
        }

        // Actualizar hijos: reducir indegree y activar si está en 0
        for (auto& child_id : node->children) 
        {
            auto child = nodes[child_id];
            int newdeg = --child->indeg; // Decremento atómico
            if (newdeg == 0 && child->status == Status::PENDING) 
            {
                lock_guard<mutex> lk(q_mtx);
                ready_q.push_back(child);
                q_cv.notify_all();
            }
        }

        // Actualizar contadores globales
        active_tasks--;
        remaining_tasks--;
        if (remaining_tasks == 0)
            q_cv.notify_all();
    }
}


// Funcion para leer y analizar el archivo de definicion del DAG
void parse_dag_file(const string& filename) 
{
    ifstream file(filename);
    if (!file)
        throw runtime_error("No se pudo abrir " + filename);

    string line;
    while (getline(file, line)) 
    {
        if (line.empty() || line[0] == '#') // Saltar comentarios y líneas vacías
            continue;

        // Separar por " - "
        vector<string> parts;
        size_t pos = 0, prev = 0;
        while ((pos = line.find(" - ", prev)) != string::npos) 
        {
            parts.push_back(line.substr(prev, pos - prev));
            prev = pos + 3;
        }
        parts.push_back(line.substr(prev)); // Último fragmento

        if (parts.size() < 2)
            throw runtime_error("Línea mal formada: " + line);

        string id = parts[0];
        string cmd = parts[1];
        string deps_str = (parts.size() >= 3) ? parts[2] : "";

        auto node = make_shared<Node>();
        node->id = id;
        node->cmd = cmd;

        // Parsear dependencias separadas por comas
        if (!deps_str.empty() && deps_str != "-") 
        {
            stringstream ss(deps_str);
            string dep;
            while (getline(ss, dep, ',')) 
            {
                if (!dep.empty())
                    node->deps.push_back(dep);
            }
        }

        // Insertar nodo en el mapa global
        nodes[id] = node;
    }

    // Construir relaciones (llenar lista de hijos e indegree)
    for (auto& [id, node] : nodes) 
    {
        node->indeg = node->deps.size();
        for (auto& dep : node->deps) 
        {
            if (!nodes.count(dep))
                throw runtime_error("Dependencia no encontrada: " + dep);
            nodes[dep]->children.push_back(id);
        }
    }
}


// Funcion para detectar ciclos en el DAG mediante DFS
bool detect_cycle() {
    unordered_map<string, int> state; // 0=unvisited, 1=visiting, 2=done

    function<bool(string)> dfs = [&](string id) {
        state[id] = 1; // visitando
        for (auto& child : nodes[id]->children) {
            if (state[child] == 1) return true; // ciclo detectado
            if (state[child] == 0 && dfs(child)) return true;
        }
        state[id] = 2;
        return false;
        };

    for (auto& [id, _] : nodes)
        if (state[id] == 0 && dfs(id))
        {
            return true; // Regresa true si existe un ciclo
        }

    return false;
}

// Funcion Main
int main(int argc, char* argv[]) 
    {
    if (argc < 2) {
        cerr << "Uso: " << argv[0] << " archivo_dag [max_concurrencia] [max_reintentos]\n";
        return 1;
    }

    // Leer argumentos de entrada
    string dagfile = argv[1];
    if (argc >= 3) max_concurrency = stoi(argv[2]);
    if (argc >= 4) max_retries = stoi(argv[3]);

    // Cargar archivo DAG
    try {
        parse_dag_file(dagfile);
    }
    catch (exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    // Verificar que el grafo no tenga ciclos
    if (detect_cycle()) {
        cerr << "Error: El DAG contiene un ciclo. Abortando.\n";
        return 1;
    }

    // Inicializar contador global de tareas pendientes
    remaining_tasks = nodes.size();

    // Encolar los nodos iniciales (sin dependencias)
    {
        lock_guard<mutex> lk(q_mtx);
        for (auto& [id, node] : nodes)
            if (node->indeg == 0)
                ready_q.push_back(node);
    }

    logmsg("=== Iniciando ejecución DAG con " + to_string(nodes.size()) + " tareas ===");

    // Crear y lanzar los hilos trabajadores
    vector<thread> workers;
    for (int i = 0; i < max_concurrency; i++)
        workers.emplace_back(worker_thread);

    // Esperar a que terminen todos
    for (auto& t : workers)
        t.join();

    logmsg("=== Ejecución completada ===");

    // Mostrar resumen final
    int success = 0, fail = 0;
    for (auto& [id, node] : nodes) {
        if (node->status == Status::SUCCESS)
            success++;
        else if (node->status == Status::FAILED)
            fail++;
    }

    cout << "\nResumen final:\n";
    cout << "  Total de nodos: " << nodes.size() << "\n";
    cout << "  Exitosos: " << success << "\n";
    cout << "  Fallidos: " << fail << "\n";

    return 0;
}
