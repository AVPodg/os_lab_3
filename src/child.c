#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#define SHARED_SIZE 4096

typedef struct {
    char data[256];  // Буфер для передачи команд
    int data_ready;  // Флаг: 1 = данные готовы, 0 = данные обработаны
} shared_data_t;

// Вспомогательная функция для записи строк
void write_str(int fd, const char *str) {
    write(fd, str, strlen(str));
}

// Вспомогательная функция для записи чисел
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

void process_command(char *line) {
    char *token = strtok(line, " ");
    int numbers[32];
    int count = 0;
    
    while (token && count < 32) {
        char *end;
        long num = strtol(token, &end, 10);
        if (*end != '\0') {
            write_str(STDOUT_FILENO, "Дочерний: Ошибка парсинга '");
            write_str(STDOUT_FILENO, token);
            write_str(STDOUT_FILENO, "'\n");
            return;
        }
        numbers[count++] = num;
        token = strtok(NULL, " ");
    }
    
    if (count < 2) {
        write_str(STDOUT_FILENO, "Дочерний: Нужно минимум 2 числа\n");
        return;
    }
    
    int result = numbers[0];
    write_str(STDOUT_FILENO, "Дочерний: Делимое: ");
    write_int(STDOUT_FILENO, result);
    write_str(STDOUT_FILENO, "\n");

    for (int i = 1; i < count; i++) {
        write_str(STDOUT_FILENO, "Дочерний: Делитель ");
        write_int(STDOUT_FILENO, i);
        write_str(STDOUT_FILENO, ": ");
        write_int(STDOUT_FILENO, numbers[i]);
        write_str(STDOUT_FILENO, "\n");
    
        if (numbers[i] == 0) {
            write_str(STDOUT_FILENO, "Дочерний: ОШИБКА: деление на ноль!\n");
        
            memset(shared->data, 0, sizeof(shared->data));
            shared->data_ready = 0;
        
            kill(getppid(), SIGUSR1);
            _exit(1);
        }
    
        result /= numbers[i];
        write_str(STDOUT_FILENO, "Дочерний: Результат: ");
        write_int(STDOUT_FILENO, result);
        write_str(STDOUT_FILENO, "\n");
    }

    memset(shared->data, 0, sizeof(shared->data));
    
    write_str(STDOUT_FILENO, "Дочерний: Итог: ");
    write_int(STDOUT_FILENO, result);
    write_str(STDOUT_FILENO, "\n\n");
}

int main_child(int argc, char *argv[]) {
    if (argc != 2) {
        write_str(STDERR_FILENO, "Неверные аргументы: ");
        write_str(STDERR_FILENO, strerror(errno));
        write_str(STDERR_FILENO, "\n");
        return 1;
    }
    
    write_str(STDOUT_FILENO, "Дочерний PID: ");
    write_int(STDOUT_FILENO, getpid());
    write_str(STDOUT_FILENO, " (родитель: ");
    write_int(STDOUT_FILENO, getppid());
    write_str(STDOUT_FILENO, ")\n");
    
    // Открываем shared memory
    int shm_fd = shm_open(argv[1], O_RDWR, 0666);
    if (shm_fd == -1) {
        write_str(STDERR_FILENO, "Ошибка открытия shared memory: ");
        write_str(STDERR_FILENO, strerror(errno));
        write_str(STDERR_FILENO, "\n");
        return 1;
    }
    
    shared_data_t *shared = mmap(NULL, SHARED_SIZE, PROT_READ | PROT_WRITE, 
                                MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        write_str(STDERR_FILENO, "Ошибка mmap: ");
        write_str(STDERR_FILENO, strerror(errno));
        write_str(STDERR_FILENO, "\n");
        close(shm_fd);
        return 1;
    }
    
    close(shm_fd);
    
    while (1) {
        while (!shared->data_ready) { // ожидание данных от родителя
            usleep(100000); // 100ms
        }
        
        if (strcmp(shared->data, "EXIT") == 0) {
            write_str(STDOUT_FILENO, "Дочерний: Завершение\n");
            break;
        }
        
        write_str(STDOUT_FILENO, "Дочерний: Получено: ");
        write_str(STDOUT_FILENO, shared->data);
        write_str(STDOUT_FILENO, "\n");
        process_command(shared->data);
        
        shared->data_ready = 0;
    }
    
    munmap(shared, SHARED_SIZE);
    return 0;
}
