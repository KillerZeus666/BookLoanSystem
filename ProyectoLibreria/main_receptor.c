// PROYECTO SISTEMAS OPERATIVOS
// Por: Katheryn Guasca, Juan Esteban Diaz, Nicolas Morales, Daniel Bohorquez

#define _XOPEN_SOURCE 700  // Necesario para strptime

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define PIPE_NAME "pipeReceptor"
#define MAX_LIBROS 100
#define BUFFER_SIZE 10   // Tamaño del buffer circular
#define MAX_EJEMPLARES 10

typedef struct {
    char titulo[100];
    int isbn;
    int num_ejemplares;
    int prestados[MAX_EJEMPLARES];
    char fecha_devolucion[MAX_EJEMPLARES][20];
} Libro;

typedef struct {
    char solicitudes[BUFFER_SIZE][256];  // Arreglo para almacenar las solicitudes
    int in;             // indice de inserción
    int out;            // indice de extracción
    int count;          // contador de solicitudes en el buffer
    pthread_mutex_t mutex;             // Mutex para exclusión mutua
    pthread_cond_t not_empty;          // variable de condición para buffer no vacío
    pthread_cond_t not_full;           // variable de condición para buffer no lleno
} BufferCompartido;

Libro biblioteca[MAX_LIBROS];
int num_libros = 0;
int modo_verbose = 0;
BufferCompartido buffer;   // Instancia global del buffer circular
volatile int receptor_activo = 1;

// Prototipos
void cargar_bd(const char* archivo);
void guardar_bd(const char* archivo);
void actualizar_fecha_devolucion(char* fecha, int dias_adicionales);
void procesar_solicitud_directa(char* mensaje);
char* obtener_fecha_actual();
Libro* buscarLibroPorISBN(int isbn);

// Implementación de funciones

Libro* buscarLibroPorISBN(int isbn) {
    for (int i = 0; i < num_libros; i++) {
        if (biblioteca[i].isbn == isbn) {
            return &biblioteca[i];
        }
    }
    return NULL;
}

void cargar_bd(const char* nombre_archivo) {
    FILE* archivo = fopen(nombre_archivo, "r");
    if (!archivo) {
        perror("❌ Error al abrir archivo de libros");
        exit(1);
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), archivo)) {
        char titulo[100];
        int isbn, num_ejemplares;
        if (sscanf(linea, "%99[^,],%d,%d", titulo, &isbn, &num_ejemplares) == 3) {
            if (num_libros < MAX_LIBROS) {
                strcpy(biblioteca[num_libros].titulo, titulo);
                biblioteca[num_libros].isbn = isbn;
                biblioteca[num_libros].num_ejemplares = num_ejemplares;

                for (int j = 0; j < num_ejemplares; j++) {
                    if (!fgets(linea, sizeof(linea), archivo)) {
                        break;
                    }
                    int ejemplar, estado;
                    char fecha[20];
                    if (sscanf(linea, "%d,%d,%19s", &ejemplar, &estado, fecha) == 3) {
                        biblioteca[num_libros].prestados[ejemplar-1] = estado;
                        strcpy(biblioteca[num_libros].fecha_devolucion[ejemplar-1], fecha);
                    }
                }
                num_libros++;
            }
        }
    }
    fclose(archivo);
}

void guardar_bd(const char* nombre_archivo) {
    FILE* archivo = fopen(nombre_archivo, "w");
    if (!archivo) {
        perror("❌ Error al guardar BD");
        return;
    }

    for (int i = 0; i < num_libros; i++) {
        fprintf(archivo, "%s,%d,%d\n", biblioteca[i].titulo, biblioteca[i].isbn, biblioteca[i].num_ejemplares);
        for (int j = 0; j < biblioteca[i].num_ejemplares; j++) {
            fprintf(archivo, "%d,%d,%s\n", j+1, biblioteca[i].prestados[j], biblioteca[i].fecha_devolucion[j]);
        }
    }
    fclose(archivo);
}

// Cambiar la función actualizar_fecha_devolucion por esta nueva versión:
void actualizar_fecha_devolucion(char* fecha, int dias_adicionales) {
    time_t t = time(NULL);
    t += dias_adicionales * 24 * 60 * 60; // Agregar días en segundos
    struct tm tm = *localtime(&t);
    strftime(fecha, 20, "%d-%m-%Y", &tm);
}

// Cambiar la función obtener_fecha_actual por esta versión simplificada:
char* obtener_fecha_actual() {
    static char fecha[20];
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    strftime(fecha, sizeof(fecha), "%d-%m-%Y", &tm);
    return fecha;
}

void* procesar_buffer(void* arg) {
    while (receptor_activo || buffer.count > 0) {
        pthread_mutex_lock(&buffer.mutex);
        while (buffer.count == 0 && receptor_activo) {
            pthread_cond_wait(&buffer.not_empty, &buffer.mutex);
        }

        if (buffer.count > 0) {
            char solicitud[256];
            strcpy(solicitud, buffer.solicitudes[buffer.out]);
            buffer.out = (buffer.out + 1) % BUFFER_SIZE;
            buffer.count--;

            pthread_cond_signal(&buffer.not_full);
            pthread_mutex_unlock(&buffer.mutex);

            // Procesar la solicitud
            printf("⚙️ Procesando solicitud en buffer: %s\n", solicitud);
            procesar_solicitud_directa(solicitud);
        } else {
            pthread_mutex_unlock(&buffer.mutex);
        }
    }
    return NULL;
}

void procesar_solicitud_directa(char* mensaje) {
    char operacion[2], titulo[100];
    int isbn, ejemplar = -1;

    if (sscanf(mensaje, "%1s,%99[^,],%d,%d", operacion, titulo, &isbn, &ejemplar) < 3) {
        printf("⚠️ Formato de mensaje inválido\n");
        return;
    }

    Libro* libro = buscarLibroPorISBN(isbn);
    if (!libro) {
        printf("⚠️ Libro no encontrado (ISBN: %d)\n", isbn);
        return;
    }

    if (operacion[0] == 'R') {
        if (ejemplar < 1 || ejemplar > libro->num_ejemplares) {
            printf("⚠️ Ejemplar inválido\n");
            return;
        }
        if (libro->prestados[ejemplar-1] == 1) {
            actualizar_fecha_devolucion(libro->fecha_devolucion[ejemplar-1], 7);
            printf("✅ Renovación exitosa: %s (Ejemplar %d). Nueva fecha: %s\n", 
                   titulo, ejemplar, libro->fecha_devolucion[ejemplar-1]);
            guardar_bd("libros.txt");
        } else {
            printf("⚠️ El ejemplar %d no está prestado\n", ejemplar);
        }
    }
    else if (operacion[0] == 'D') {
        if (ejemplar < 1 || ejemplar > libro->num_ejemplares) {
            printf("⚠️ Ejemplar inválido\n");
            return;
        }
        if (libro->prestados[ejemplar-1] == 1) {
            libro->prestados[ejemplar-1] = 0;
            printf("✅ Devolución exitosa: %s (Ejemplar %d)\n", titulo, ejemplar);
            guardar_bd("libros.txt");
        } else {
            printf("⚠️ El ejemplar %d no estaba prestado\n", ejemplar);
        }
    }
    else {
        printf("⚠️ Operación desconocida: %s\n", operacion);
    }
}


// ================= MANEJO DE COMANDOS ================= //

void* manejar_comandos(void* arg) {
    char comando[50];
    while (1) {
        printf("\nIngrese comando (r para reporte/s para salir): ");
        fgets(comando, sizeof(comando), stdin);
        comando[strcspn(comando, "\n")] = '\0';

        if (strcmp(comando, "s") == 0) {
            printf("🛑 Terminando proceso receptor...\n");
            receptor_activo = 0;
            pthread_cond_broadcast(&buffer.not_empty);  // Despierta al consumidor si está esperando
            pthread_exit(NULL);  // Termina el hilo de comandos

        } else if (strcmp(comando, "r") == 0) {
            printf("\n📊 Reporte de Libros - %s\n", obtener_fecha_actual());
            printf("==================================\n");
            printf("Estado, Título, ISBN, Ejemplar, Fecha Devolución\n");
            
            for (int i = 0; i < num_libros; i++) {
                for (int j = 0; j < biblioteca[i].num_ejemplares; j++) {
                    printf("%s, %s, %d, %d, %s\n",
                           biblioteca[i].prestados[j] ? "P" : "D",
                           biblioteca[i].titulo,
                           biblioteca[i].isbn,
                           j+1,
                           biblioteca[i].fecha_devolucion[j]);
                }
            }
            printf("==================================\n");
        } else {
            printf("⚠️ Comando no reconocido\n");
        }
    }
    return NULL;
}

// ================= PROCESAMIENTO DE SOLICITUDES ================= //

void procesar_solicitud(char* mensaje) {
    char operacion[2], titulo[100];
    int isbn, ejemplar = -1;

    // Analizar la solicitud
    if (sscanf(mensaje, "%1s,%99[^,],%d,%d", operacion, titulo, &isbn, &ejemplar) < 3) {
        printf("⚠️ Formato de mensaje inválido\n");
        return;
    }

    // Clasificar la operación
    if (operacion[0] == 'D' || operacion[0] == 'R') {
        // ➡️ Encolar la solicitud en el buffer
        pthread_mutex_lock(&buffer.mutex);
        while (buffer.count == BUFFER_SIZE) {
            pthread_cond_wait(&buffer.not_full, &buffer.mutex);
        }
        strcpy(buffer.solicitudes[buffer.in], mensaje);
        buffer.in = (buffer.in + 1) % BUFFER_SIZE;
        buffer.count++;
        pthread_cond_signal(&buffer.not_empty);
        pthread_mutex_unlock(&buffer.mutex);

        printf("📥 Solicitud %s encolada en buffer.\n", operacion);
    }
    else if (operacion[0] == 'P') {
        // ➡️ Procesar de inmediato como antes
        Libro* libro = buscarLibroPorISBN(isbn);
        if (!libro) {
            printf("⚠️ Libro no encontrado (ISBN: %d)\n", isbn);
            return;
        }
        for (int j = 0; j < libro->num_ejemplares; j++) {
            if (libro->prestados[j] == 0) {
                libro->prestados[j] = 1;
                actualizar_fecha_devolucion(libro->fecha_devolucion[j], 7);
                printf("✅ Préstamo exitoso: %s (Ejemplar %d). Devuelva antes del %s\n", 
                       titulo, j+1, libro->fecha_devolucion[j]);
                guardar_bd("libros.txt");
                return;
            }
        }
        printf("⚠️ No hay ejemplares disponibles de %s\n", titulo);
    }
    else {
        printf("⚠️ Operación desconocida: %s\n", operacion);
    }
}

// ================= FUNCIÓN PRINCIPAL ================= //

int main(int argc, char* argv[]) {
    char* pipe_nombre = PIPE_NAME;
    char* archivo_bd = "libros.txt";
    char* archivo_salida = NULL;

    // Procesar argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) pipe_nombre = argv[++i];
        else if (strcmp(argv[i], "-f") == 0) archivo_bd = argv[++i];
        else if (strcmp(argv[i], "-v") == 0) modo_verbose = 1;
        else if (strcmp(argv[i], "-s") == 0) archivo_salida = argv[++i];
    }

    // Cargar base de datos
    cargar_bd(archivo_bd);
    printf("✅ BD cargada desde %s\n", archivo_bd);

    // Inicializar buffer compartido
    buffer.in = 0;
    buffer.out = 0;
    buffer.count = 0;
    pthread_mutex_init(&buffer.mutex, NULL);
    pthread_cond_init(&buffer.not_empty, NULL);
    pthread_cond_init(&buffer.not_full, NULL);

    // Crear pipe si no existe
    if (access(pipe_nombre, F_OK) == -1) {
        if (mkfifo(pipe_nombre, 0666) == -1) {
            perror("❌ Error al crear el pipe");
            exit(1);
        }
    }

    // Iniciar hilo de comandos
    pthread_t hilo_comandos;
    pthread_create(&hilo_comandos, NULL, manejar_comandos, NULL);

    // Crear hilo auxiliar 1 para procesar el buffer
    pthread_t hilo_auxiliar;
    pthread_create(&hilo_auxiliar, NULL, procesar_buffer, NULL);

    // Abrir pipe para lectura
    int pipe_fd = open(pipe_nombre, O_RDONLY);
    if (pipe_fd == -1) {
        perror("❌ Error al abrir el pipe");
        exit(1);
    }

    printf("✅ Receptor iniciado. Esperando solicitudes...\n");

    // Procesar solicitudes
    char mensaje[256];
    while (1) {
        ssize_t bytes_read = read(pipe_fd, mensaje, sizeof(mensaje));
        if (bytes_read > 0) {
            if (modo_verbose) {
                printf("[VERBOSE] Solicitud recibida: %s\n", mensaje);
            }
            if (strncmp(mensaje, "Q", 1) == 0) {
                printf("🛑 Recibido comando de terminación\n");
                break;
            }
            procesar_solicitud(mensaje);
        }
    }

    // Guardar BD si se especificó
    if (archivo_salida) {
        guardar_bd(archivo_salida);
        printf("✅ BD guardada en %s\n", archivo_salida);
    }

    // Limpieza
    close(pipe_fd);
    pthread_cancel(hilo_comandos);
    pthread_join(hilo_auxiliar, NULL);
    pthread_mutex_destroy(&buffer.mutex);
    pthread_cond_destroy(&buffer.not_empty);
    pthread_cond_destroy(&buffer.not_full);
    pthread_cancel(hilo_auxiliar);
    pthread_cancel(hilo_comandos);
    return 0;
}