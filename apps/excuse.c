#include "barnix_app.h"
#include "../barnixiolib/stdio.h"

// Array of excuses
const char *excuses[] = {
    "The dog ate my homework",
    "I lost it on the bus",
    "My computer crashed",
    "The Wi-Fi was down",
    "I forgot to save it",
    "My little brother deleted it",
    "The power went out",
    "I was helping my grandma",
    "My cat walked on the keyboard",
    "I thought it was due tomorrow"
};

const char *excuses_ru[] = {
    "Собака съела мое ДЗ",
    "Потерял в автобусе",
    "Компьютер сломался",
    "Wi-Fi не работал",
    "Забыл сохранить",
    "Младший брат удалил",
    "Отключили свет",
    "Помогал бабушке",
    "Кот прошелся по клаве",
    "Думал что на завтра"
};

// Simple pseudo-random
static unsigned int rseed = 42;

int rand_r() {
    rseed = rseed * 1103515245 + 12345;
    return (rseed / 65536) % 32768;
}

// Check if buffer contains substring
int contains(const char *buf, const char *sub, int buf_len) {
    if (buf_len <= 0) return 0;
    for (int i = 0; i < buf_len; i++) {
        int match = 1;
        int j = 0;
        while (sub[j]) {
            if (i + j >= buf_len || buf[i + j] != sub[j]) {
                match = 0;
                break;
            }
            j++;
        }
        if (match) return 1;
    }
    return 0;
}

int main(int argc, const char *const *argv) {
    // Check language preference from /etc/sys-lang.cfg
    int is_russian = 0;
    
    if (barnix->size("/etc/sys-lang.cfg") > 0) {
        char buffer[64];
        int bytes_read = barnix->read("/etc/sys-lang.cfg", 0, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            if (contains(buffer, "LANG=ru", bytes_read) || contains(buffer, "ru", bytes_read)) {
                is_russian = 1;
            }
        }
    }
    
    // Generate random excuse
    int count = 10; // Both arrays have 10 items
    int index = rand_r() % count;
    
    if (is_russian) {
        puts("Ваша отмазка:");
        puts("");
        printf("  %s", excuses_ru[index]); // Print with indentation
    } else {
        puts("Your excuse:");
        puts("");
        printf("  %s", excuses[index]);
    }
    
    return 0;
}
