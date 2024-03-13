#include <stdio.h>      // Biblioteca estándar de entrada/salida en C. Proporciona facilidades para la entrada y salida de datos, como funciones para leer y escribir en archivos, imprimir en la consola y leer la entrada del usuario.
#include <omp.h>        // Proporciona las funcionalidades de OpenMP, una API para la programación paralela en sistemas de memoria compartida. Permite paralelizar bloques de código fácilmente con directivas de preprocesador.
#include <stdlib.h>     // Biblioteca estándar de utilidades generales en C. Incluye funciones para la gestión de memoria dinámica, control de procesos, conversiones y otras utilidades como generación de números aleatorios.
#include <fcntl.h>      // Define las constantes necesarias para las llamadas al sistema open. Facilita la apertura de archivos utilizando diversos flags, como O_RDONLY, O_WRONLY, etc.
#include <sys/mman.h>   // Proporciona funcionalidades de mapeo de memoria, como mmap y munmap, que permiten mapear archivos o dispositivos en memoria, proporcionando un acceso eficiente a estos recursos.
#include <pthread.h>    // Ofrece las funcionalidades de la biblioteca de hilos POSIX. Es utilizada para crear y manejar hilos, mutexes y otras herramientas de sincronización en programación multihilo.
#include <unistd.h>     // Proporciona acceso a la API de POSIX para llamadas al sistema como read, write, close, y muchas otras que interactúan directamente con el sistema operativo Unix/Linux.
#include <sys/syscall.h> // Permite realizar llamadas al sistema directamente a través de sus identificadores numéricos. Se utiliza para funciones del sistema operativo que no están expuestas por las bibliotecas estándar.
#include <sys/stat.h>   // Define la estructura 'stat' que se usa en la llamada al sistema stat, la cual recopila información sobre el archivo identificado por la ruta dada, como tamaño, permisos, etc.
#include <sys/wait.h>   // Incluye las declaraciones para las llamadas al sistema de espera, como wait y waitpid, que permiten a un proceso esperar a que sus procesos hijos cambien de estado o terminen.

// Longitud de filas y columnas
#define SIZE 9

//Varibale global sudoku
int sudoku[SIZE][SIZE];

// Estrcutura de parametros para threads
typedef struct {
    int start_row;
    int start_col;
} parameters;

// Funciones prototype que serán ejecutadas por los hilos.
int check_rows(void *param);
int check_column(void *param);
void *check_columns(void *param);
int check_subgrid(void *param);

int main(int argc, char *argv[]) {
    omp_set_num_threads(1);
    if (argc != 2) {
        printf("Uso: %s <input_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Abrir el archivo para lectura
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Error al abrir el archivo");
        exit(EXIT_FAILURE);
    }

    // Obtener el tamanio del archivo
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Error al obtener el tamanio del archivo");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Mapear el archivo a memoria
    void *file_in_memory = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_in_memory == MAP_FAILED) {
        perror("Error al mapear el archivo a memoria");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Leer la solución de Sudoku del archivo mapeado
    int i = 0, j = 0;
    for (size_t k = 0; k < sb.st_size && i < SIZE; ++k) {
        char item = ((char *)file_in_memory)[k];
        if (item >= '1' && item <= '9') {
            sudoku[i][j] = item - '0'; // Convertir de caracter a entero
            j++;
            if (j == SIZE) {
                j = 0;
                i++;
            }
        }
    }

    // Verificar si la matriz Sudoku se ha llenado correctamente
    if (i < SIZE) {
        fprintf(stderr, "El archivo no contiene suficientes digitos para un Sudoku completo.\n");
        munmap(file_in_memory, sb.st_size);
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Imprimir la matriz Sudoku para verificar
    for (i = 0; i < SIZE; ++i) {
        for (j = 0; j < SIZE; ++j) {
            printf("%d ", sudoku[i][j]);
        }
        printf("\n");
    }

    // Verificacion de subarreglos 3x3
    for (i = 0; i < 7; i = i+3) {
        for (j = 0; j < 7; j = j+3) {
            parameters subgrid_param = {i, j};
            check_subgrid((void *)&subgrid_param);
        }
    }

    pid_t father_id = getpid();
    pid_t child_id = fork();

    pthread_t threads[2];

    // Ejecucion de comando ps desde proceso hijo
    if (child_id < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (child_id == 0) { // Proceso hijo
        char pid_str[20];
        sprintf(pid_str, "%d", father_id);
        execlp("ps", "ps", "-p", pid_str, "-lLf", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    } else { // Proceso padre
        parameters col_param = {0, 0};
        pthread_create(&threads[0], NULL, check_columns, &col_param);
        pthread_join(threads[0], NULL);
        printf("El thread en el que se ejecuta main es: %ld\n", syscall(SYS_gettid));

        wait(NULL); // Esperar a que el proceso hijo termine

        parameters row_param = {0, 0};
        check_rows((void *)&row_param);

        
        printf("Sudoku resuelto!\n");

        pid_t other_child_id = fork();

        if (other_child_id < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (other_child_id == 0) { // Proceso hijo
            char pid_str[20];
            printf("Antes de terminar el estado de este proceso y sus threads es: \n");
            sprintf(pid_str, "%d", father_id);
            execlp("ps", "ps", "-p", pid_str, "-lLf", NULL);
            perror("execlp");
            exit(EXIT_FAILURE);
        } else {
            wait(NULL); // Esperar a que el proceso hijo termine
            // Limpiar
            munmap(file_in_memory, sb.st_size); // Desmapear el archivo de la memoria
            close(fd); // Cerrar el descriptor de archivo

            return 0;
        }
    }
}

// Funcion para revisar filas
int check_rows(void *param) {
    parameters *p = (parameters *)param;
    int start_row = p->start_row;

    #pragma omp parallel for schedule(dynamic)
    for (int row = start_row; row < SIZE; ++row) {
        int used[SIZE + 1] = {0};
        for (int col = 0; col < SIZE; ++col) {
            int digit = sudoku[row][col];
            if (used[digit] || digit < 1 || digit > SIZE) {
                printf("Sudoku invalido (fila %d)\n", row + 1);
                exit(EXIT_FAILURE);
            }
            used[digit] = 1;
        }
    }
    printf("Filas validas.\n");
    return 0;
}

//Funcion para revisar columnas
int check_column(void* param){
    parameters *p = (parameters *)param;
    int start_col = p->start_col;

    #pragma omp parallel for schedule(dynamic)
    for (int col = start_col; col < SIZE; ++col) {
        printf("En la revision de columnas el siguiente es un thread en ejecucion: %lu\n", syscall(SYS_gettid));
        int used[SIZE + 1] = {0};
        for (int row = 0; row < SIZE; ++row) {
            int digit = sudoku[row][col];
            if (used[digit] || digit < 1 || digit > SIZE) {
                printf("Sudoku invalido (columna %d)\n", col + 1);
                pthread_exit(NULL);
            }
            used[digit] = 1;
        }
    }

    return 0;
}

// Funcion asignable a thread
void *check_columns(void *param) {
    printf("El thread que ejecuta el metodo para ejecutar el metodo de revision de columnas es: %lu\n", syscall(SYS_gettid));
    int res = check_column(param);
    
    if (res == 0){
        printf("Columnas validas.\n");
        pthread_exit(0);
    } else {
        exit(EXIT_FAILURE);
    }

    
}

//Funcion para revisar subarreglos de 3x3
int check_subgrid(void *param) {
    parameters *p = (parameters *)param;
    int start_row = p->start_row;
    int start_col = p->start_col;

    int used[SIZE + 1] = {0};
    omp_lock_t lock;
    omp_init_lock(&lock); // Inicializar el bloqueo

    #pragma omp parallel for schedule(dynamic)
    for (int row = start_row; row < start_row + 3; ++row) {
        for (int col = start_col; col < start_col + 3; ++col) {
            int digit = sudoku[row][col];

            omp_set_lock(&lock); // Establecer el bloqueo
            if (used[digit] == 1 || digit < 1 || digit > SIZE) {
                printf("Sudoku invalido (subarreglo en fila %d, columna %d)\n", row + 1, col + 1);
                exit(EXIT_FAILURE); // Nota: exit terminará el programa inmediatamente, lo cual puede no ser deseable
            }
            used[digit] = 1;
            omp_unset_lock(&lock); // Liberar el bloqueo
        }
    }

    omp_destroy_lock(&lock); // Destruir el bloqueo
    printf("Subarreglo valido.\n");
    return 0;
}