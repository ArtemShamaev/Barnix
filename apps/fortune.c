#include "barnix_app.h"
#include "../barnixiolib/stdio.h"

// Array of fortunes
const char *fortunes[] = {
    "The stars are in your favor",
    "Opportunity knocks soon",
    "Beware of false friends",
    "Hard work pays off",
    "New beginnings today",
    "Surprise awaits you",
    "Fortune favors brave",
    "Patience is virtue",
    "Stay open minded",
    "Journey begins with step"
};

const char *fortunes_ru[] = {
    "Звезды благоприятствуют",
    "Возможность скоро",
    "Остерегайтесь лжедрузей",
    "Труд будет вознагражден",
    "Новые начала сегодня",
    "Ваш ждет сюрприз",
    "Фортуна смелым",
    "Терпение - добродетель",
    "Будьте открыты",
    "Путь начинается с шага"
};

// Simple pseudo-random
static unsigned int fseed = 12345;

int frand() {
    fseed = fseed * 1103515245 + 12345;
    return (fseed / 65536) % 32768;
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
    // Check language preference
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
    
    // Generate random fortune
    int count = 10;
    int index = frand() % count;
    
    if (is_russian) {
        puts("Предсказание:");
        puts("");
        printf("  %s", fortunes_ru[index]);
    } else {
        puts("Your fortune:");
        puts("");
        printf("  %s", fortunes[index]);
    }
    
    return 0;
}
