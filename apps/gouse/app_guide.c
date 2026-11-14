/* Barnix Guide: help available from the desktop and Win+H. */
#include "gouse.h"

typedef struct { int page; } State;
static const char *const titles_en[] = {"Welcome", "Programs", "Development", "Networking"};
static const char *const titles_ru[] = {"Начало", "Программы", "Разработка", "Сеть"};
static const char *const pages_en[][8] = {
    {"Barnix Guide", "Win+H opens this guide at any time in Happy Gouse.",
     "Use the desktop icons or the Gouse menu to open apps.",
     "Gouse Prompt runs system commands in a window.",
     "The shell is also available outside the desktop.",
     "F11 maximizes a window; Alt+Tab changes windows.", 0},
    {"Files and programs", "Executive browses files. Notepad edits text.",
     "Programs are in /bin. Run them by name in the shell.",
     "Type help to see commands; bnm manages the network.",
     "Java JAR files run with: java -jar FILE.jar",
     "Nova examples are in /usr/share/nova/examples.", 0},
    {"C, C++ and Nova", "Barnix Code edits, builds and runs them.",
     "GCC and G++ are in /usr/bin when installed.",
     "Compile C: gcc hello.c -o hello",
     "Compile C++: g++ hello.cpp -o hello",
     "Nova programs use Java 8 bytecode on BJVM.",
     "The Nova compiler requires a host JDK 21.", 0},
    {"Connect to Wi-Fi", "Run bnm connect to choose a network from a list.",
     "Enter its password when prompted; it is not echoed.",
     "Run bnm status to inspect the connection.",
     "Run bnm internet for a wired DHCP connection.",
     "The installer also lets you choose Wi-Fi from a list.", 0},
};
static const char *const pages_ru[][8] = {
    {"Barnix Guide", "Win+H открывает это руководство в Happy Gouse.",
     "Приложения открываются с рабочего стола или из меню.",
     "Gouse Prompt выполняет команды в окне.",
     "Вне рабочего стола доступна командная оболочка.",
     "F11 разворачивает окно; Alt+Tab переключает окна.", 0},
    {"Файлы и программы", "Executive открывает файлы. Блокнот редактирует текст.",
     "Программы находятся в /bin. Вводите их имя в оболочке.",
     "Команда help покажет список; bnm управляет сетью.",
     "JAR-файл запускается: java -jar FILE.jar",
     "Примеры Nova: /usr/share/nova/examples.", 0},
    {"C, C++ и Nova", "Barnix Code правит, собирает и запускает их.",
     "GCC и G++ находятся в /usr/bin после установки.",
     "Сборка C: gcc hello.c -o hello",
     "Сборка C++: g++ hello.cpp -o hello",
     "Программы Nova используют байткод Java 8 в BJVM.",
     "Компилятору Nova нужен JDK 21 на основном ПК.", 0},
    {"Подключение Wi-Fi", "bnm connect откроет список доступных сетей.",
     "Введите пароль выбранной сети.",
     "bnm status покажет текущее соединение.",
     "bnm internet подключит кабельную сеть через DHCP.",
     "Установщик тоже предлагает список сетей Wi-Fi.", 0},
};
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = w->state;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.window);
    bgx_fill(c, 0, 0, w->w, 42, g_theme.title_active);
    g_text(c, 14, 12, "BARNIX GUIDE", g_theme.title_text);
    for (int i = 0; i < 4; i++)
        g_toggle(w, c, 100 + i, 12 + i * ((w->w - 24) / 4), 50,
                 (w->w - 32) / 4, 26, g_russian ? titles_ru[i] : titles_en[i], s->page == i);
    const char *const *lines = g_russian ? pages_ru[s->page] : pages_en[s->page];
    g_text(c, 18, 91, lines[0], g_theme.title_active);
    for (int i = 1; lines[i]; i++) g_text_fit(c, 18, 105 + i * 25, w->w - 36, lines[i], g_theme.text);
    g_text_fit(c, 18, w->h - 24, w->w - 36,
               g_russian ? "Стрелки: страницы    Esc: закрыть    Win+H: открыть" :
                           "Arrows: pages    Esc: close    Win+H: open", g_theme.shadow);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = w->state;
    if (e->type == EV_COMMAND && e->id >= 100 && e->id < 104) s->page = e->id - 100;
    if (e->type == EV_KEY) {
        if (e->code == BGX_KEY_ESC) { g_close(w); return; }
        if (e->code == BGX_KEY_RIGHT && s->page < 3) s->page++;
        if (e->code == BGX_KEY_LEFT && s->page > 0) s->page--;
    }
}
const GApp app_guide = {"Barnix Guide", "Help for Barnix, development and Wi-Fi", ICON_DOCUMENT,
                        620, 360, 400, 260, APP_RESIZABLE | APP_SINGLE, 0, 0, sizeof(State),
                        0, event, 0, paint, 0};
