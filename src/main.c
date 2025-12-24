#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#define SHARED_SIZE 4096

typedef struct {
    char data[256]; // Буфер для передачи данных между процессами
    int data_ready; // Флаг синхронизации: 1=данные готовы, 0=данные обработаны
} shared_data_t;

volatile sig_atomic_t child_failed = 0;

// Вспомогательные функции для работы со строками и числами
void write_string(int fd, const char *str) {
    write(fd, str, strlen(str));
}

void write_int(int fd, int num) {
    char buffer[32];
    int i = 0;
    int is_negative = 0;
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    do {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0);
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - j - 1];
        buffer[i - j - 1] = temp;
    }
    
    write(fd, buffer, i);
}

void handle_signal(int sig) {
    child_failed = 1;
    write_string(STDOUT_FILENO, "Родитель: Получен сигнал об ошибке\n");
}

// Функция дочернего процесса
void run_child_process(const char* shm_name) {
    write_string(STDOUT_FILENO, "Дочерний PID: ");
    write_int(STDOUT_FILENO, getpid());
    write_string(STDOUT_FILENO, " (родитель: ");
    write_int(STDOUT_FILENO, getppid());
    write_string(STDOUT_FILENO, ")\n");
    
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        write_string(STDERR_FILENO, "Дочерний: Ошибка открытия shared memory: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        _exit(1);
    }
    
    shared_data_t *shared = mmap(NULL, SHARED_SIZE, PROT_READ | PROT_WRITE,  // mmap() создает независимое отображение и закрытие экономит ресурсы (лимит дескрипторов)
                                MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        write_string(STDERR_FILENO, "Дочерний: Ошибка mmap: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        close(shm_fd);
        _exit(1);
    }
    
    close(shm_fd);
    
    while (1) {
        while (!shared->data_ready) {
            usleep(100000);
        }
        
        if (strcmp(shared->data, "EXIT") == 0) {
            write_string(STDOUT_FILENO, "Дочерний: Завершение\n");
            break;
        }
        
        write_string(STDOUT_FILENO, "Дочерний: Получено: ");
        write_string(STDOUT_FILENO, shared->data);
        write_string(STDOUT_FILENO, "\n");
        
        // Обработка команды
        char *token = strtok(shared->data, " ");
        int numbers[32], count = 0;
        
        while (token && count < 32) {
            char *end;
            long num = strtol(token, &end, 10);
            if (*end != '\0') {
                write_string(STDOUT_FILENO, "Дочерний: Ошибка парсинга '");
                write_string(STDOUT_FILENO, token);
                write_string(STDOUT_FILENO, "'\n");
                break;
            }
            numbers[count++] = num;
            token = strtok(NULL, " ");
        }
        
        if (count >= 2) {
            int result = numbers[0];
            write_string(STDOUT_FILENO, "Дочерний: Делимое: ");
            write_int(STDOUT_FILENO, result);
            write_string(STDOUT_FILENO, "\n");
            
            for (int i = 1; i < count; i++) {
                write_string(STDOUT_FILENO, "Дочерний: Делитель ");
                write_int(STDOUT_FILENO, i);
                write_string(STDOUT_FILENO, ": ");
                write_int(STDOUT_FILENO, numbers[i]);
                write_string(STDOUT_FILENO, "\n");
                
                if (numbers[i] == 0) {
                    write_string(STDOUT_FILENO, "Дочерний: ОШИБКА: деление на ноль!\n");
                    kill(getppid(), SIGUSR1);
                    _exit(1);
                }
                
                result /= numbers[i];
                write_string(STDOUT_FILENO, "Дочерний: Результат: ");
                write_int(STDOUT_FILENO, result);
                write_string(STDOUT_FILENO, "\n");
            }
            write_string(STDOUT_FILENO, "Дочерний: Итог: ");
            write_int(STDOUT_FILENO, result);
            write_string(STDOUT_FILENO, "\n\n");
        }
        
        shared->data_ready = 0;  // Сигнализирует родителю что команда обработана и можно отправлять следующую
    }
    
    munmap(shared, SHARED_SIZE); // удаление отображения памяти (не удаляет shared mamory объект из системы)
    _exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        write_string(STDERR_FILENO, "Использование: ");
        write_string(STDERR_FILENO, argv[0]);
        write_string(STDERR_FILENO, " <файл_команд>\n");
        return 1;
    }

    signal(SIGUSR1, handle_signal);

    // Открытие файла команд с помощью open вместо fopen
    int file_fd = open(argv[1], O_RDONLY);
    if (file_fd == -1) {
        write_string(STDERR_FILENO, "Ошибка открытия файла: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        return 1;
    }

    // Создание shared memory
    char shm_name[64];

    // форматирование имени
    char pid_str[32], time_str[32];
    int pid_len = 0, time_len = 0;
    pid_t pid_val = getpid();
    time_t time_val = time(NULL);
    
    // Форматирование PID
    pid_t temp_pid = pid_val;
    do {
        pid_str[pid_len++] = '0' + (temp_pid % 10);
        temp_pid /= 10;
    } while (temp_pid > 0);
    for (int i = 0; i < pid_len / 2; i++) {
        char temp = pid_str[i];
        pid_str[i] = pid_str[pid_len - i - 1];
        pid_str[pid_len - i - 1] = temp;
    }
    
    // Форматирование времени
    time_t temp_time = time_val;
    do {
        time_str[time_len++] = '0' + (temp_time % 10);
        temp_time /= 10;
    } while (temp_time > 0);
    for (int i = 0; i < time_len / 2; i++) {
        char temp = time_str[i];
        time_str[i] = time_str[time_len - i - 1];
        time_str[time_len - i - 1] = temp;
    }
    
    // Сборка имени shared memory
    write_string(STDOUT_FILENO, "/lab3_shm_");
    write(STDOUT_FILENO, pid_str, pid_len);
    write_string(STDOUT_FILENO, "_");
    write(STDOUT_FILENO, time_str, time_len);
    shm_name[0] = '\0';
    strcat(shm_name, "/lab3_shm_");
    strncat(shm_name, pid_str, pid_len);
    strcat(shm_name, "_");
    strncat(shm_name, time_str, time_len);
    
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        write_string(STDERR_FILENO, "Ошибка создания shared memory: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        close(file_fd);
        return 1;
    }
    ftruncate(shm_fd, SHARED_SIZE);  // устанавливает размер файла/shared memory

    shared_data_t *shared = mmap(NULL, SHARED_SIZE, PROT_READ | PROT_WRITE,
                                MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        write_string(STDERR_FILENO, "Ошибка mmap: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        close(shm_fd);
        shm_unlink(shm_name); // удаление shared memory объекта
        close(file_fd);
        return 1;
    }

    memset(shared, 0, sizeof(shared_data_t)); // заполнение памяти указанным байтом

    // Создание дочернего процесса
    pid_t pid = fork();
    if (pid == -1) {
        write_string(STDERR_FILENO, "Ошибка fork: ");
        write_string(STDERR_FILENO, strerror(errno));
        write_string(STDERR_FILENO, "\n");
        munmap(shared, SHARED_SIZE);
        close(shm_fd);
        shm_unlink(shm_name);
        close(file_fd);
        return 1;
    }

    if (pid == 0) {
        // Дочерний процесс
        close(shm_fd);
        close(file_fd);
        run_child_process(shm_name);
    }

    // Родительский процесс
    close(shm_fd);
    write_string(STDOUT_FILENO, "Родитель PID: ");
    write_int(STDOUT_FILENO, getpid());
    write_string(STDOUT_FILENO, ", Дочерний PID: ");
    write_int(STDOUT_FILENO, pid);
    write_string(STDOUT_FILENO, "\n");

    char line[256];
    ssize_t bytes_read;
    
    // Чтение файла с помощью read вместо fgets
    while ((bytes_read = read(file_fd, line, sizeof(line) - 1)) > 0 && !child_failed) {
        line[bytes_read] = '\0';
        char *current_line = line;
        char *newline;
        
        while ((newline = strchr(current_line, '\n')) != NULL && !child_failed) {
            *newline = '\0';
            
            if (strlen(current_line) > 0) {
                while (shared->data_ready && !child_failed) usleep(100000);
                if (child_failed) break;

                strncpy(shared->data, current_line, sizeof(shared->data)-1);
                shared->data[sizeof(shared->data)-1] = '\0';
                shared->data_ready = 1;
                write_string(STDOUT_FILENO, "Родитель: Отправка: ");
                write_string(STDOUT_FILENO, current_line);
                write_string(STDOUT_FILENO, "\n");
                
                while (shared->data_ready && !child_failed) usleep(100000);
            }
            current_line = newline + 1;
        }
    }

    // Отправка команды "EXIT" дочернему процессу
    strcpy(shared->data, "EXIT");
    shared->data_ready = 1;

    close(file_fd);
    waitpid(pid, NULL, 0);
    munmap(shared, SHARED_SIZE);
    shm_unlink(shm_name);
    write_string(STDOUT_FILENO, "Родитель: Завершение\n");
    
    return 0;
}