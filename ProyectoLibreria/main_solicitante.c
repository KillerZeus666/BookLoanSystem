// PROYECTO SISTEMAS OPERATIVOS
// Por: Katheryn Guasca, Juan Esteban Díaz, Nicolás Morales, Daniel Bohórquez

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define PIPE_NAME "pipeReceptor"
#define MAX_LIBROS 100
#define MAX_EJEMPLARES 10

typedef struct {
    char titulo[100];
    int isbn;
    int num_ejemplares;
    int prestados[MAX_EJEMPLARES];       // 0 = disponible, 1 = prestado
    char fecha_devolucion[MAX_EJEMPLARES][20];  // Formato "DD-MM-AAAA"
} Libro;

Libro biblioteca[MAX_LIBROS];
int num_libros = 0;

// ================= FUNCIONES AUXILIARES ================= //

Libro* buscarLibroPorNombre(const char* nombre) {
    for (int i = 0; i < num_libros; i++) {
        if (strcmp(biblioteca[i].titulo, nombre) == 0) {
            return &biblioteca[i];
        }
    }
    return NULL;
}

void cargarLibrosDesdeArchivo() {
    char nombre_archivo[100];
    printf("📥 Ingrese el archivo de libros disponibles: ");
    fgets(nombre_archivo, sizeof(nombre_archivo), stdin);
    nombre_archivo[strcspn(nombre_archivo, "\n")] = '\0';

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

                // Obtener fecha actual
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                char fecha_actual[20];
                strftime(fecha_actual, sizeof(fecha_actual), "%d-%m-%Y", tm);

                // Inicializar ejemplares como disponibles con fecha actual
                for (int i = 0; i < num_ejemplares; i++) {
                    biblioteca[num_libros].prestados[i] = 0;
                    strcpy(biblioteca[num_libros].fecha_devolucion[i], fecha_actual);
                }
                num_libros++;
            }
        }
    }
    fclose(archivo);
    printf("✅ Libros cargados correctamente (%d libros).\n", num_libros);
}

void enviar_solicitud(int pipe_fd, char* operacion, char* libro, int isbn) {
    char mensaje[256];
    snprintf(mensaje, sizeof(mensaje), "%s,%s,%d", operacion, libro, isbn);
    write(pipe_fd, mensaje, strlen(mensaje) + 1);
}

void trim(char* str) {
    char* end;
    while (*str == ' ' || *str == '\n') str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\n')) end--;
    *(end + 1) = '\0';
}

// ================= MODO MANUAL/AUTOMATICO ================= //

void modo_manual(const char* archivo) {
    FILE *fp = fopen(archivo, "r");
    if (!fp) {
        perror("❌ Error al abrir archivo de solicitudes");
        exit(1);
    }

    int pipe_fd = open(PIPE_NAME, O_WRONLY);
    if (pipe_fd == -1) {
        perror("❌ Error al abrir el pipe");
        exit(1);
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), fp)) {
        // Eliminar salto de línea final
        linea[strcspn(linea, "\n")] = '\0';

        char operacion[2], libro[100];
        int isbn, ejemplar = -1;
        int num_campos = sscanf(linea, "%1[^,],%99[^,],%d,%d", operacion, libro, &isbn, &ejemplar);

        if (num_campos < 3) {
            printf("⚠️ Formato de línea inválido: %s\n", linea);
            continue;
        }

        // Construir mensaje según el número de campos encontrados
        char mensaje[256];
        if (num_campos == 4) {
            snprintf(mensaje, sizeof(mensaje), "%s,%s,%d,%d", operacion, libro, isbn, ejemplar);
        } else if (num_campos == 3) {
            snprintf(mensaje, sizeof(mensaje), "%s,%s,%d", operacion, libro, isbn);
        } else {
            printf("⚠️ Línea no válida: %s\n", linea);
            continue;
        }

        // Enviar al receptor
        write(pipe_fd, mensaje, strlen(mensaje) + 1);
        printf("📨 Enviando: %s\n", mensaje);
        sleep(1);
    }

    // Al finalizar, enviar solicitud de parada
    char mensaje_salida[] = "Q,Salir,0";
    write(pipe_fd, mensaje_salida, strlen(mensaje_salida) + 1);
    printf("📨 Enviando solicitud de parada: %s\n", mensaje_salida);

    close(pipe_fd);
    fclose(fp);
}

// ================= MODO INTERACTIVO ================= //

void modo_interactivo() {
    int pipe_fd = open(PIPE_NAME, O_WRONLY);
    if (pipe_fd == -1) {
        perror("❌ Error al abrir el pipe");
        exit(1);
    }

    int opcion;
    char libro[100];
    char operacion[2];

    while (1) {
        printf("\n📚 Menú de Solicitudes:\n");
        printf("1. Solicitar préstamo\n");
        printf("2. Renovar libro\n");
        printf("3. Devolver libro\n");
        printf("4. Salir\n");
        printf("Seleccione una opción: ");
        scanf("%d", &opcion);
        getchar();

        if (opcion == 4) break;

        printf("Ingrese título del libro: ");
        fgets(libro, sizeof(libro), stdin);
        trim(libro);

        Libro* encontrado = buscarLibroPorNombre(libro);
        if (!encontrado) {
            printf("⚠️ Libro no encontrado\n");
            continue;
        }

        int ejemplar = -1;
        if (opcion != 1) {
            printf("Ingrese número de ejemplar: ");
            scanf("%d", &ejemplar);
            getchar();
        }

        char mensaje[256];
        switch(opcion) {
            case 1: 
                snprintf(mensaje, sizeof(mensaje), "P,%s,%d", libro, encontrado->isbn);
                break;
            case 2:
                snprintf(mensaje, sizeof(mensaje), "R,%s,%d,%d", libro, encontrado->isbn, ejemplar);
                break;
            case 3:
                snprintf(mensaje, sizeof(mensaje), "D,%s,%d,%d", libro, encontrado->isbn, ejemplar);
                break;
        }

        write(pipe_fd, mensaje, strlen(mensaje)+1);
        printf("📨 Solicitud enviada\n");
    }
}

// ================= MAIN ================= //

int main() {
    if (access(PIPE_NAME, F_OK) == -1) {
        if (mkfifo(PIPE_NAME, 0666) == -1) {
            perror("❌ Error al crear el pipe");
            exit(1);
        }
    }

    cargarLibrosDesdeArchivo();

    int modo;
    printf("\n📌 Seleccione modo:\n1. Automatico\n2. Interactivo\nOpción: ");
    scanf("%d", &modo);
    getchar();

    if (modo == 1) {
        char archivo[100];
        printf("📂 Ingrese archivo de solicitudes: ");
        fgets(archivo, sizeof(archivo), stdin);
        archivo[strcspn(archivo, "\n")] = '\0';
        modo_manual(archivo);
    } else if (modo == 2) {
        modo_interactivo();
    } else {
        printf("⚠️  Opción inválida\n");
    }

    return 0;
}