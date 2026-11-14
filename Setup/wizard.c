#include "setup.h"
#include "barnix.h"
#include "keyboard.h"
#include "mouse.h"
#include "text.h"
#include "disk.h"
#include "fs.h"
#include "partition.h"
#include "permissions.h"
#include "console_video.h"
#include "net.h"
#include "package.h"
#include "build/profiles.h"
#include "memory.h"

#define PREFIX_SECTORS 2048U
#define ROOT_SECTORS 8192U   /* 4 MiB template: Setup/build.py FS_SECTORS, ext2_import.inc */
#define PAYLOAD_SECTORS (PREFIX_SECTORS + ROOT_SECTORS)
#ifdef BARNIX_EFI
#define ESP_SECTORS (64U * 2048U)
#define SYSTEM_START (PREFIX_SECTORS + ESP_SECTORS)
#define SETUP_MODULES 5
#define GCC_MODULE 4
#define BOOT_LABEL "LiveCD  |  UEFI x64  |  Barnix ext2"
#else
#define SYSTEM_START PREFIX_SECTORS
#define SETUP_MODULES 4
#define GCC_MODULE 3
#define BOOT_LABEL "LiveCD  |  Legacy BIOS  |  Barnix ext2"
#endif
#define INSTALL_SECTORS (SYSTEM_START + ROOT_SECTORS)
#define MAX_TARGETS DISK_MAX_DEVICES
static struct { const char *name; DiskSelection disk; uint64_t capacity; } targets[MAX_TARGETS];
static int target_count, selected, profile, russian;
static int component_network, component_games, component_gcc, component_cursor;
static const MultibootInfo *setup_info;
static char hostname[24] = "barnix";
static int network_mode; /* 0: offline, 1: Ethernet, 2: Wi-Fi */
static char wifi_name[33], wifi_password[65];
static char repository[96] = "http://10.0.2.2:8073";
static int repository_custom;

// User configuration
#define MAX_USERS 8
#undef MAX_USERNAME
#define MAX_USERNAME 24
#undef MAX_PASSWORD
#define MAX_PASSWORD 64
static struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int is_root;
} users[MAX_USERS];
static int user_count = 0;

#define screen console_cells
#define SETUP_OPTION 0x200U
static int option_rows[8],option_count;
static unsigned int wizard_key(void) {
    static unsigned int previous_buttons;
    for(;;) {
        BarnixMouse mouse;mouse_get(&mouse);
        int pos=(mouse.y/16)*80+mouse.x/8;
        unsigned short saved=screen[pos];
        if(mouse.available)screen[pos]=saved^0x7700;
        console_present();
        unsigned int key=keyboard_event();
        screen[pos]=saved;
        if(key!=KEY_MOUSE)return key;
        mouse_get(&mouse);int click=(mouse.buttons&1)&&!(previous_buttons&1);previous_buttons=mouse.buttons;
        if(!click)continue;
        int x=mouse.x/8,y=mouse.y/16;
        if(y==22) {
            if(x>=3&&x<=10)return KEY_UP;
            if(x>=13&&x<=22)return KEY_DOWN;
            if(x>=43&&x<=56)return KEY_ESCAPE;
            if(x>=61&&x<=76)return '\n';
        }
        for(int i=0;i<option_count;i++)if(y==option_rows[i]&&x>=25&&x<=76)return SETUP_OPTION+i;
    }
}
static const char *say(const char *en, const char *ru) { return russian ? ru : en; }
/* The plain Setup ISO leaves GCC out so it loads fast from slow USB sticks;
 * the GCC Setup ISO adds it as the last module. */
static int modules_valid(const MultibootInfo *info)
{
    return info && (info->flags & MULTIBOOT_MODULES) &&
           (info->module_count == SETUP_MODULES || info->module_count == GCC_MODULE);
}
static int gcc_module_present(void) { return modules_valid(setup_info) && setup_info->module_count == SETUP_MODULES; }
static const char *package_hash(const char *name)
{
    static char hash[65];
    if (!modules_valid(setup_info)) return NULL;
    const MultibootModule *modules = (const MultibootModule *)setup_info->modules;
    const MultibootModule *manifest = &modules[2];
    const char *text = (const char *)manifest->start;
    unsigned int size = manifest->end - manifest->start, name_length = strlen(name);
    if (manifest->end <= manifest->start || size > 1024) return NULL;
    for (unsigned int i = 0; i + name_length + 66 <= size; i++) {
        if (i && text[i - 1] != '\n') continue;
        if (memcmp(text + i, name, name_length) || text[i + name_length] != ' ') continue;
        if (text[i + name_length + 65] != '\n') continue;
        memcpy(hash, text + i + name_length + 1, 64); hash[64] = 0;
        return hash;
    }
    return NULL;
}

static void rect(int x, int y, int w, int h, unsigned char attr)
{
    for (int row = y; row < y + h; row++) for (int col = x; col < x + w; col++)
        screen[row * 80 + col] = (unsigned short)attr << 8 | ' ';
}
static void label(int x, int y, unsigned char attr, const char *text)
{
    Utf8Decoder decoder = {0};
    while (*text && x < 79) {
        unsigned int codes[2]; int n = utf8_feed(&decoder, (unsigned char)*text++, codes);
        for (int i = 0; i < n && x < 79; i++) screen[y * 80 + x++] = (unsigned short)attr << 8 | text_glyph(codes[i]);
    }
}
static void number(int x, int y, unsigned char attr, unsigned int value)
{ char text[16]; utoa(value, text); label(x, y, attr, text); }
static void cursor_off(void)
{
    if (console_graphics()) { console_cursor_visible(0); return; }
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x0a), "Nd"((unsigned short)0x3d4));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x20), "Nd"((unsigned short)0x3d5));
}
static void cursor_on(void)
{
    if (console_graphics()) { console_cursor_visible(1); return; }
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x0a), "Nd"((unsigned short)0x3d4));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x0e), "Nd"((unsigned short)0x3d5));
}
static void frame(int stage, const char *title)
{
    option_count=0;
    rect(0, 0, 80, 25, 0x10); rect(1, 1, 78, 22, 0x17);
    rect(23, 4, 55, 17, 0x07);
    label(3, 1, 0x1b, SETUP_DISTRIBUTION "  /  SETUP");
    label(3, 2, 0x17, say("A small system. A fresh start.", "Маленькая система. Новое начало."));
    label(25, 5, 0x0f, title);
    const char *en[] = {"Welcome", "Destination", "Components", "Network", "Settings", "Users", "Review", "Install", "Ready"};
    const char *ru[] = {"Начало", "Выбор диска", "Компоненты", "Сеть", "Настройки", "Пользователи", "Проверка", "Установка", "Готово"};
    for (int i = 0; i < 9; i++) {
        unsigned char color = i == stage ? 0x3f : i < stage ? 0x1a : 0x18;
        if (i == stage) rect(2, 4 + i * 2, 20, 1, color);
        number(3, 4 + i * 2, color, i + 1); label(6, 4 + i * 2, color, russian ? ru[i] : en[i]);
    }
    label(3,22,0x3f,say("[ Up ]","[Вверх]"));label(13,22,0x3f,say("[ Down ]","[Вниз]"));
    label(43,22,0x3f,say("[ Back / Esc ]","[Назад / Esc]"));label(61,22,0x3f,say("[ Next / Enter ]","[Далее / Enter]"));
    label(3, 24, 0x18, BOOT_LABEL);
}
static void option(int row, int active, const char *name)
{
    if(option_count<8)option_rows[option_count++]=row;
    unsigned char color = active ? 0x30 : 0x07;
    rect(25, row, 51, 1, color); label(26, row, color, active ? ">" : " "); label(29, row, color, name);
}
static void error_page(const char *en, const char *ru)
{
    frame(6, say("Installation did not finish", "Установка не завершена"));
    label(25, 8, 0x0c, say(en, ru));
    label(25, 11, 0x07, say("The selected disk may contain partial data.", "На выбранном диске могут быть неполные данные."));
    label(25, 13, 0x07, say("Check the disk and retry from the wizard.", "Проверьте диск и повторите установку."));
    label(25, 17, 0x0b, say("ENTER: return to disk selection", "ENTER: вернуться к выбору диска"));
    while (wizard_key() != '\n') {}
}
static void scan_targets(void)
{
    target_count = 0; DiskSelection previous = disk_selection();
    int count = disk_device_count();
    for (int i = 0; i < count && target_count < MAX_TARGETS; i++) {
        const char *name = disk_device_name(i);
        if (!strcmp(name, "ram0") || disk_select_raw(name)) continue;
        targets[target_count].name = name; targets[target_count].capacity = disk_capacity();
        targets[target_count++].disk = disk_selection();
    }
    disk_restore(previous);
    if (selected >= target_count) selected = 0;
}
static int choose_disk(void)
{
    scan_targets();
    for (;;) {
        frame(1, say("Where should Barnix be installed?", "Куда установить Barnix?"));
        label(25, 7, 0x08, say("DISK        SIZE KiB         STATUS", "ДИСК        РАЗМЕР КиБ       СОСТОЯНИЕ"));
        int first = (selected / 6) * 6;
        for (int i = first; i < target_count && i < first + 6; i++) {
            int row = 9 + i - first; unsigned char color = i == selected ? 0x30 : 0x07;
            option(row, i == selected, targets[i].name);
            char capacity_text[21]; disk_u64toa(targets[i].capacity >> 1, capacity_text);
            label(39, row, color, capacity_text);
            label(53, row, color, targets[i].disk.sectors >= INSTALL_SECTORS ?
                  say("Ready", "Готов") : say("Too small", "Мал"));
        }
        if (!target_count) label(25, 10, 0x0e, say("No supported disks detected.", "Нет поддерживаемых дисков."));
        label(25, 16, 0x0e, say("Choose partitioning mode on the next screen.", "Способ разметки — на следующем экране."));
        label(25, 18, 0x08,
#ifdef BARNIX_EFI
              say("Needs 67 MiB. R: scan disks again.", "Нужно 67 МиБ. R: повторный поиск дисков.")
#else
              say("Needs 3 MiB. R: scan disks again.", "Нужно 3 МиБ. R: повторный поиск дисков.")
#endif
              );
        unsigned int key = wizard_key();
        if (key == KEY_ESCAPE) return 0;
        if(key>=SETUP_OPTION && key<SETUP_OPTION+6 && first+(int)(key-SETUP_OPTION)<target_count)selected=first+key-SETUP_OPTION;
        if (key == 'r' || key == 'R') scan_targets();
        if (key == KEY_UP && selected) selected--;
        if (key == KEY_DOWN && selected + 1 < target_count) selected++;
        if (key == '\n' && target_count && targets[selected].disk.sectors >= INSTALL_SECTORS) return 1;
    }
}
static int choose_profile(void)
{
    int gcc_available = gcc_module_present() && package_hash("gcc") != NULL &&
                        targets[selected].disk.sectors >= 1048576U; /* GCC needs room beyond a 128 MiB test disk. */
    static int default_disk = -1;
    if (default_disk != selected) { component_gcc = gcc_available; default_disk = selected; }
    for (;;) {
        frame(2, say("Choose what to install", "Что установить?"));
        option(8, component_cursor == 0, say("[x] Minimal system", "[x] Минимальная система"));
        option(10, component_cursor == 1, component_network ? "[x] Network tools, downloads, Git" : "[ ] Network tools, downloads, Git");
        option(12, component_cursor == 2, component_games ? "[x] Games and example programs" : "[ ] Games and example programs");
        option(14, component_cursor == 3, gcc_available ?
             (component_gcc ? "[x] GCC 16 C/C++ (offline)" : "[ ] GCC 16 C/C++ (offline)") :
               say("[ ] GCC: later from BarnStore", "[ ] GCC: позже из BarnStore"));
        label(25, 17, 0x0b, say("Space: toggle. Enter: continue.", "Пробел: выбрать. Enter: далее."));
        if (gcc_module_present())
            label(25, 19, 0x08, say("GCC is included; network is only for tools and games.",
                                    "GCC включён в ISO; сеть нужна только для утилит и игр."));
        unsigned int key = wizard_key();
        if (key == KEY_ESCAPE) return 0;
        if(key>=SETUP_OPTION && key<SETUP_OPTION+4) { component_cursor=key-SETUP_OPTION; key=' '; }
        if (key == KEY_UP && component_cursor) component_cursor--;
        if (key == KEY_DOWN && component_cursor < 3) component_cursor++;
        if (key == ' ') {
            if (component_cursor == 1) component_network=!component_network;
            if (component_cursor == 2) component_games=!component_games;
            if (component_cursor == 3 && gcc_available) component_gcc=!component_gcc;
        }
        profile = component_games ? 2 : component_network ? 1 : 0;
        if (key == '\n') { if ((component_network || component_games) && !network_mode) network_mode=1; return 1; }
    }
}
static int network_input(const char *title, char *value, int capacity, int secret)
{
    keyboard_set_layout(0);
    for (;;) {
        frame(3, title);
        label(25, 8, 0x07, say("Type a value and press Enter; Esc returns.", "Введите значение и нажмите Enter; Esc — назад."));
        rect(25, 11, 51, 1, 0x30);
        if (secret) for (int i = 0; value[i] && i < 49; i++) label(26 + i, 11, 0x30, "*");
        else label(26, 11, 0x30, value);
        unsigned int key = wizard_key(); int length = strlen(value);
        if (key == KEY_ESCAPE) return 0;
        if (key == '\n' && (length || secret)) return 1;
        if (key == '\b' && length) value[length - 1] = 0;
        if (key >= ' ' && key <= '~' && length < capacity - 1) {
            value[length] = (char)key; value[length + 1] = 0;
        }
    }
}
static int choose_wifi_network(void)
{
    char scan[2048];
    char names[24][33];
    int secured[24], count = net_scan(scan, sizeof(scan));
    if (count <= 0) return -1;
    int found = 0;
    for (char *line = scan; *line && found < 24;) {
        char *next = strchr(line, '\n');
        if (next) *next = 0;
        char *name = line + (*line == '*' || *line == ' ');   /* net_scan marks each line: '*' current, ' ' other */
        char *tab = strchr(name, '\t');
        if (tab) {
            *tab++ = 0;
            if (*name && strlen(name) < sizeof(names[0])) {
                strcpy(names[found], name);
                secured[found] = strncmp(tab, "open", 4) != 0;
                found++;
            }
        }
        if (!next) break;
        line = next + 1;
    }
    if (!found) return -1;
    int selected = 0;
    for (;;) {
        frame(3, say("Choose a Wi-Fi network", "Выберите сеть Wi-Fi"));
        label(25, 7, 0x08, say("Select with arrows, then press Enter", "Выберите стрелками и нажмите Enter"));
        int first = selected / 6 * 6;
        for (int i = first; i < found && i < first + 6; i++) {
            char title[48]; strcpy(title, names[i]);
            strcat(title, secured[i] ? "  [WPA2]" : "  [open]");
            option(8 + (i - first) * 2, i == selected, title);
        }
        if (found > 6) number(25, 20, 0x08, selected + 1);
        unsigned int key = wizard_key();
        if (key == KEY_ESCAPE) return 0;
        if (key >= SETUP_OPTION && key < SETUP_OPTION + (unsigned int)option_count)
            selected = first + (int)(key - SETUP_OPTION);
        if (key == KEY_UP && selected) selected--;
        if (key == KEY_DOWN && selected + 1 < found) selected++;
        if (key == '\n' || (key >= SETUP_OPTION && key < SETUP_OPTION + (unsigned int)option_count)) {
            strcpy(wifi_name, names[selected]);
            wifi_password[0] = 0;
            if (secured[selected] && !network_input(say("Wi-Fi password", "Пароль Wi-Fi"), wifi_password, sizeof(wifi_password), 1)) return 0;
            return 1;
        }
    }
}
static int network_settings(void)
{
    int result = 0;
    for (;;) {
        frame(3, say("Network settings", "Настройки сети"));
        option(8, network_mode == 0, say("Offline (minimal installation)", "Без сети (минимальная установка)"));
        option(10, network_mode == 1, say("Ethernet / DHCP", "Кабель / DHCP"));
        option(12, network_mode == 2, say("Wi-Fi / DHCP", "Wi-Fi / DHCP"));
        label(25, 15, 0x0b, say("W: choose Wi-Fi  R: repository", "W: выбрать Wi-Fi  R: репозиторий"));
        label(25, 16, 0x07, repository);
        if (wifi_name[0]) label(25, 18, 0x08, wifi_name);
        if (result) label(25, 19, 0x0c, say("Connection failed. Check network settings.", "Ошибка подключения. Проверьте настройки сети."));
        else if (net_state() == NET_CABLE || net_state() == NET_WIFI)
            label(25, 19, 0x0a, say("Connected. Press Enter to continue.", "Подключено. Enter: далее."));
        unsigned int key = wizard_key();
        if (key == KEY_ESCAPE) return 0;
        if (key >= SETUP_OPTION && key < SETUP_OPTION + 3) network_mode = key - SETUP_OPTION;
        if (key == KEY_UP && network_mode) network_mode--;
        if (key == KEY_DOWN && network_mode < 2) network_mode++;
        if (!repository_custom) strcpy(repository, network_mode == 2 ?
                                        "http://192.168.77.2:8073" : "http://10.0.2.2:8073");
        if (key == 's' || key == 'S') { network_mode = 2; result = choose_wifi_network() < 0 ? -1 : 0; }
        if (key == 'r' || key == 'R') {
            if (network_input(say("Package repository URL", "Адрес репозитория пакетов"), repository, sizeof(repository), 0)) repository_custom = 1;
        }
        if (key == 'w' || key == 'W') {
            network_mode = 2;
            if (choose_wifi_network() > 0)
                result = net_connect_wifi(wifi_name, wifi_password);
        }
        if (key == '\n') {
            if (network_mode == 0) {
                if (component_network || component_games) { result=-1; continue; }
                return 1;
            }
            if (network_mode == 2 && !wifi_name[0]) choose_wifi_network();
            result = network_mode == 1 ? net_connect_cable() :
                wifi_name[0] ? net_connect_wifi(wifi_name, wifi_password) : -1;
            if (!result) return 1;
        }
    }
}
static int settings(void)
{
    keyboard_set_layout(0);
    for (;;) {
        frame(4, say("Make this system yours", "Настройка системы"));
        label(25, 8, 0x07, say("Computer name (letters, digits, hyphen):", "Имя компьютера (латиница, цифры, дефис):"));
        rect(25, 10, 40, 1, 0x30); label(26, 10, 0x30, hostname);
        label(25, 13, 0x07, say("Installed language: English", "Язык системы: русский"));
        label(25, 15, 0x08, say("Follows the language chosen at Welcome.", "Выбран на первом экране мастера."));
        label(25, 18, 0x0b, say("Type to edit; Backspace to erase; Enter to save.", "Ввод: изменить. Backspace: стереть. Enter: далее."));
        unsigned int key = wizard_key(); int length = strlen(hostname);
        if (key == KEY_ESCAPE) return 0;
        if (key == '\n' && length && hostname[0] != '-' && hostname[length - 1] != '-') return 1;
        if (key == '\b' && length) hostname[length - 1] = 0;
        if (length < 23 && ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') || key == '-')) {
            hostname[length] = key; hostname[length + 1] = 0;
        }
    }
}

static int field(const char *title,char *value,int capacity,int secret) {
    value[0]=0;
    for(;;) {
        frame(5,title);label(25,8,0x07,say("Type a value, then Enter. ESC: back.","Введите значение. Enter: далее. ESC: назад."));
        rect(25,11,51,1,0x30);
        if(secret) { for(int i=0;value[i]&&i<49;i++)label(26+i,11,0x30,"*"); }
        else label(26,11,0x30,value);
        unsigned int key=wizard_key();int n=strlen(value);
        if(key==KEY_ESCAPE)return 0;
        if(key=='\n'&&n)return 1;
        if(key=='\b'&&n)value[n-1]=0;
        if(key>=' '&&key<='~'&&n<capacity-1){value[n]=key;value[n+1]=0;}
    }
}
static int configure_users(void) {
    char confirm[MAX_PASSWORD];keyboard_set_layout(0);user_count=0;
    strcpy(users[0].username,"root");users[0].is_root=1;
    for(;;) {
        if(!field(say("Create your username","Имя вашего пользователя"),users[1].username,MAX_USERNAME,0))return 0;
        int valid=strcmp(users[1].username,"root")!=0;
        for(char *p=users[1].username;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))valid=0;
        if(valid)break;
    }
    for(int i=1;i>=0;i--)for(;;) {
        if(!field(i?say("Your password","Ваш пароль"):say("Separate root password","Отдельный пароль root"),users[i].password,MAX_PASSWORD,1))return 0;
        if(!field(say("Repeat password","Повторите пароль"),confirm,sizeof(confirm),1))return 0;
        if(!strcmp(confirm,users[i].password))break;
    }
    memset(confirm,0,sizeof(confirm));user_count=2;return 1;
}

static PartitionTable layout;
static int layout_mode,root_slot=-1,esp_slot=-1;
static int decimal(const char *text,unsigned int *n) {
    *n=0;if(!text||!*text)return -1;
    for(;*text;text++){if(*text<'0'||*text>'9'||*n>2000000)return -1;*n=*n*10+*text-'0';}return 0;
}
static int valid_layout(void) {
    if(partition_validate(&layout)||root_slot<0||root_slot>3)return 0;
    Partition root=layout.entries[root_slot];
    if(root.type!=0x83||root.size<ROOT_SECTORS||root.size>FS_MAX_BLOCKS*2)return 0;
    for(int i=0;i<4;i++)if(layout.entries[i].type&&layout.entries[i].start<2048)return 0;
#ifdef BARNIX_EFI
    if(esp_slot<0||esp_slot>3||layout.entries[esp_slot].type!=0xef||layout.entries[esp_slot].size<ESP_SECTORS)return 0;
#endif
    return 1;
}
static int choose_layout(void) {
    for(;;) {
        frame(1,say("Partitioning mode","Способ разметки"));
        option(8,layout_mode==0,say("Automatic: erase disk","Авто: стереть диск"));
        option(10,layout_mode==1,say("Automatic: add in free space","Авто: добавить в свободном месте"));
        option(12,layout_mode==2,say("Fully manual partitioning","Полностью ручная разметка"));
        label(25,16,0x08,say("MBR: up to four primary partitions.","MBR: до четырёх основных разделов."));
        unsigned int key=wizard_key();
        if(key==KEY_ESCAPE)return 0;
        if(key>=SETUP_OPTION && key<SETUP_OPTION+3)layout_mode=key-SETUP_OPTION;
        if(key==KEY_UP&&layout_mode)layout_mode--;
        if(key==KEY_DOWN&&layout_mode<2)layout_mode++;
        if(key!='\n')continue;
        DiskSelection previous=disk_selection();
        int failed=disk_select_raw(targets[selected].name)||partition_read(&layout);
        disk_restore(previous);
        if(layout_mode==0){memset(&layout,0,sizeof(layout));layout.sectors=targets[selected].disk.sectors;}
        else if(failed){label(25,18,0x0c,"Unsupported table. ESC or erase mode.");wizard_key();continue;}
        root_slot=esp_slot=-1;
        if(layout_mode!=2) {
#ifdef BARNIX_EFI
            esp_slot=partition_auto(&layout,ESP_SECTORS,0xef);
            if(esp_slot<0)continue;
#endif
            root_slot=partition_auto(&layout,0,0x83);
            if(root_slot>=0 && layout.entries[root_slot].size>FS_MAX_BLOCKS*2)layout.entries[root_slot].size=FS_MAX_BLOCKS*2;
            if(valid_layout())return 1;
            label(25,18,0x0c,"Insufficient space or free primary slots.");wizard_key();continue;
        }
        for(;;) {
            frame(1,say("Manual partition table (not yet saved)","Ручная разметка (ещё не сохранена)"));
            label(25,7,0x07,"Slot   Start MiB   Size MiB   Type");
            for(int i=0;i<4;i++) {
                number(25,9+i,0x0f,i+1);number(33,9+i,0x0f,layout.entries[i].start/2048);
                number(45,9+i,0x0f,layout.entries[i].size/2048);
                label(57,9+i,0x0f,layout.entries[i].type==0xef?"EFI":layout.entries[i].type?"data":"free");
            }
            label(25,14,0x07,"n SLOT START_MiB SIZE_MiB [efi]");
            label(25,15,0x07,"d SLOT | r ROOT_SLOT | e EFI_SLOT");
            label(25,16,0x07,"z = clear table | w = accept | q = back");
            char command[64]="";int length=0;
            for(;;) {
                rect(25,18,51,1,0x30);label(25,18,0x30,command);
                unsigned int k=wizard_key();if(k==KEY_ESCAPE){strcpy(command,"q");break;}if(k=='\n')break;
                if(k=='\b'&&length)command[--length]=0;
                if(k>=' '&&k<='~'&&length<50){command[length++]=k;command[length]=0;}
            }
            char *op=strtok(command," "),*arg=strtok(NULL," ");unsigned int slot,start,size;
            if(!op)continue;
            if(!strcmp(op,"q"))break;
            if(!strcmp(op,"z")){memset(layout.entries,0,sizeof(layout.entries));root_slot=esp_slot=-1;continue;}
            if(!strcmp(op,"w")){if(valid_layout())return 1;label(25,20,0x0c,"Select root/EFI partitions with sufficient size.");wizard_key();continue;}
            if(decimal(arg,&slot)||slot<1||slot>4)continue;
            slot--;
            if(!strcmp(op,"d")){memset(&layout.entries[slot],0,sizeof(Partition));continue;}
            if(!strcmp(op,"r")){root_slot=slot;continue;}
            if(!strcmp(op,"e")){esp_slot=slot;continue;}
            if(!strcmp(op,"n")) {
                char *a=strtok(NULL," "),*b=strtok(NULL," "),*type=strtok(NULL," ");
                if(decimal(a,&start)||decimal(b,&size)||start>0x1fffff||size>0x1fffff)continue;
                if(partition_add(&layout,slot,start*2048,size*2048,type&&!strcmp(type,"efi")?0xef:0x83)) {
                    label(25,20,0x0c,"Invalid, overlapping or out-of-disk partition.");wizard_key();
                }
            }
        }
    }
}
static int review(void)
{
    char confirm[24] = "";
    keyboard_set_layout(0);
    for (;;) {
        frame(6, say("Review and confirm", "Проверьте параметры"));
        label(25, 8, 0x07, say("Destination:", "Диск:")); label(43, 8, 0x0f, targets[selected].name);
        label(25, 9, 0x07, say("Components:", "Компоненты:"));
        label(43, 9, 0x0b, component_network ? "Network " : "Minimal ");
        if (component_games) label(55, 9, 0x0b, "Games");
        if (component_gcc) label(64, 9, 0x0b, "GCC");
        label(25, 11, 0x07, say("Computer:", "Компьютер:")); label(43, 11, 0x0f, hostname);
        label(25, 12, 0x07, say("Language:", "Язык:")); label(43, 12, 0x0f, russian ? "Русский" : "English");
        label(25,14,0x0e,layout_mode==0?say("Erase disk; install bootloader","Стереть диск; установить загрузчик"):say("Format selected root/EFI; replace bootloader","Форматировать root/EFI; заменить загрузчик"));
        label(25,15,0x08,"Root slot / start MiB / size MiB:");
        number(58,15,0x0f,root_slot+1);number(61,15,0x0f,layout.entries[root_slot].start/2048);number(68,15,0x0f,layout.entries[root_slot].size/2048);
        label(25, 17, 0x0f, say("Type the disk name below, then press Enter:", "Введите имя диска ниже и нажмите Enter:"));
        rect(25, 19, 30, 1, 0x40); label(26, 19, 0x4f, confirm);
        unsigned int key = wizard_key(); int length = strlen(confirm);
        if (key == KEY_ESCAPE) return 0;
        if (key == '\n' && !strcmp(confirm, targets[selected].name)) return 1;
        if (key == '\b' && length) confirm[length - 1] = 0;
        if (key >= ' ' && key <= '~' && length < 23) { confirm[length] = key; confirm[length + 1] = 0; }
    }
}
static void progress(const char *en, const char *ru, unsigned int percent)
{
    frame(7, say("Installing your system", "Установка системы"));
    label(25, 9, 0x0b, say(en, ru));
    rect(25, 12, 50, 2, 0x18);
    rect(25, 12, percent / 2, 2, 0x30);
    number(44, 15, 0x0f, percent); label(48, 15, 0x07, "%");
    label(25, 18, 0x08, say("Please keep the disk connected until completion.", "Не отключайте диск до завершения установки."));
    label(3, 22, 0x17, say("Writing and verifying. Cancellation is disabled.           ", "Запись и проверка. Отмена сейчас недоступна.                "));
    console_present();
}
static unsigned char verify_buffer[4096];
static unsigned int get32le(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
typedef struct { const char *en, *ru; unsigned int start, end, last, last_mib; } PackageProgress;
static void package_progress(unsigned int processed, unsigned int total, void *context)
{
    PackageProgress *state = context;
    unsigned int step = total / 100;
    if (!step) step = 1;
    unsigned int percent = processed / step;
    if (percent > 100) percent = 100;
    unsigned int current = state->start + (state->end - state->start) * percent / 100;
    unsigned int mib = processed / (1024U * 1024U);
    if (current != state->last || mib != state->last_mib) {
        state->last = current;
        state->last_mib = mib;
        progress(state->en, state->ru, current);
        label(25, 10, 0x07, say("GCC package:", "Пакет GCC:"));
        number(38, 10, 0x0f, mib);
        label(42, 10, 0x07, "/");
        number(44, 10, 0x0f, total / (1024U * 1024U));
        label(49, 10, 0x07, say("MiB processed", "МиБ обработано"));
        console_present();
    }
}
static int install_package(const char *name, const char *en, const char *ru, unsigned int percent)
{
    const char *hash = package_hash(name);
    if (!hash) return -1;
    progress(en, ru, percent);
    if (!strcmp(name, "gcc")) {
        if (!gcc_module_present()) return -1;
        const MultibootModule *modules = (const MultibootModule *)setup_info->modules;
        const MultibootModule *module = &modules[GCC_MODULE];
        if (module->end <= module->start) return -1;
        PackageProgress state = {"Installing GCC C/C++ offline", "Установка GCC C/C++ без сети", percent, 97, percent, 0xffffffffU};
        return setup_install_package_memory((const void *)module->start, module->end - module->start,
                                            hash, package_progress, &state);
    }
    char url[160]; strcpy(url, repository);
    int n = strlen(url);
    if (!n || n > 120) return -1;
    if (url[n - 1] != '/') strcat(url, "/");
    strcat(url, name); strcat(url, ".bpk");
    return setup_install_package(url, hash);
}
#ifdef BARNIX_EFI
static int patch_boot_slot(unsigned char *bytes,unsigned int size,int slot) {
    int count=0;
    for(unsigned int i=0;i+6<=size;i++)if(!memcmp(bytes+i,"msdos",5)&&bytes[i+5]>='1'&&bytes[i+5]<='4') {
        bytes[i+5]='1'+slot;count++;
    }
    return count?0:-1;
}
#endif
static int install_image(const MultibootInfo *info)
{
    if (!modules_valid(info)) return -1;
    const MultibootModule *modules = (const MultibootModule *)info->modules;
    const MultibootModule *module = &modules[1];
    if (module->end <= module->start || module->end - module->start != PAYLOAD_SECTORS * 512) return -1;
    const unsigned char *payload = (const unsigned char *)module->start;
    if (payload[510] != 0x55 || payload[511] != 0xaa || payload[450] != 0x83 ||
        get32le(payload + 454) != PREFIX_SECTORS || get32le(payload + 458) != ROOT_SECTORS ||
        payload[PREFIX_SECTORS * 512 + 1080] != 0x53 || payload[PREFIX_SECTORS * 512 + 1081] != 0xef) return -1;
#ifdef BARNIX_EFI
    const MultibootModule *esp_module = &modules[3];
    if (esp_module->end <= esp_module->start) return -1;
    unsigned int esp_bytes = esp_module->end - esp_module->start;
    const unsigned char *esp = (const unsigned char *)esp_module->start;
    if (esp_bytes < 512 || esp_bytes > ESP_SECTORS * 512 || (esp_bytes & 511) ||
        esp[510] != 0x55 || esp[511] != 0xaa || get32le(esp + 32) != ESP_SECTORS) return -1;
#endif
    if(!valid_layout())return -1;
    /* Minecraft Alpha (/mc on the LiveCD, scripts/install_minecraft.py)
     * goes along: read now, while the LiveCD is the filesystem. */
    static const char *const game_files[2] = {"/mc/mc.jar", "/mc/mod.jar"};
    static unsigned char *game_data[2];
    int game_size[2] = {-1, -1};
    for (int i = 0; i < 2; i++) {
        int size = fs_size(game_files[i]);
        if (size <= 0 || size > (8 << 20)) continue;
        if (!game_data[i]) game_data[i] = memory_kernel_alloc((unsigned int)size);
        if (game_data[i] && fs_read(game_files[i], 0, game_data[i], (unsigned int)size) == size) game_size[i] = size;
    }
#ifdef BARNIX_EFI
    if(patch_boot_slot((unsigned char *)esp,esp_bytes,root_slot))return -1;
    /* BPB hidden-sector count must match the chosen ESP location. */
    for(int i=0;i<4;i++) { ((unsigned char *)esp)[28+i]=layout.entries[esp_slot].start>>(8*i); ((unsigned char *)esp)[6*512+28+i]=layout.entries[esp_slot].start>>(8*i); }
#else
    /* Four BIOS core images occupy fixed slots in the embedding gap. */
#endif
    if (disk_select_raw(targets[selected].name)) return -1;
    DiskSelection target = disk_selection(), expected = targets[selected].disk;
    if (target.id != expected.id || target.sectors != expected.sectors ||
        target.generation != expected.generation || target.sectors < INSTALL_SECTORS) return -1;
    /* Make the disk unbootable first. Publish its partition table and boot
     * signature only after payload verification and configuration succeed. */
    memset(verify_buffer, 0, 512);
    if (layout_mode == 0 && (disk_write(0, verify_buffer) || disk_flush())) return -1;
#ifdef BARNIX_EFI
    /* Remove stale GPT/BIOS metadata in the alignment gap as well. */
    for (unsigned int s = 1; s < PREFIX_SECTORS; s++) if (disk_write(s, verify_buffer)) return -1;
    progress("Writing EFI System Partition", "Запись системного раздела EFI", 5);
    for (unsigned int s = 0; s < esp_bytes / 512; s++) {
        if (disk_write(layout.entries[esp_slot].start + s, esp + s * 512)) return -1;
        if (!(s % 128)) progress("Writing EFI System Partition", "Запись системного раздела EFI", 5 + s * 25 / (esp_bytes / 512));
    }
    if (disk_flush()) return -1;
    for (unsigned int s = 0; s < esp_bytes / 512;) {
        unsigned int count = esp_bytes / 512 - s; if (count > 8) count = 8;
        if (disk_read_many(layout.entries[esp_slot].start + s, verify_buffer, count) || memcmp(verify_buffer, esp + s * 512, count * 512)) return -1;
        s += count;
    }
#else
    for (unsigned int s = 1; s < PREFIX_SECTORS; s++) {
        if (disk_write(s, payload + s * 512)) return -1;
        if (!(s % 128)) progress("Copying kernel, components and bootloader", "Копирование ядра, компонентов и загрузчика", s * 30 / PREFIX_SECTORS);
    }
    if (disk_flush()) return -1;
    for (unsigned int s = 1; s < PREFIX_SECTORS;) {
        unsigned int count = PREFIX_SECTORS - s; if (count > 8) count = 8;
        if (disk_read_many(s, verify_buffer, count) || memcmp(verify_buffer, payload + s * 512, count * 512)) return -1;
        s += count;
        if ((s % 128) == 1) progress("Verifying written data", "Проверка записанных данных", 30 + s * 10 / PREFIX_SECTORS);
    }
#endif
    DiskSelection partition = target; partition.offset = layout.entries[root_slot].start;
    partition.sectors = layout.entries[root_slot].size;
    if (partition.sectors > FS_MAX_BLOCKS * 2) partition.sectors = FS_MAX_BLOCKS * 2;
    disk_restore(partition);
    progress("Formatting ext2 (up to 5 GiB)", "Форматирование ext2 (до 5 ГиБ)", 45);
    if (fs_format_selected()) return -1;
    progress("Copying and verifying system files", "Копирование и проверка файлов системы", 60);
    if (fs_import_template(payload + PREFIX_SECTORS * 512, ROOT_SECTORS * 512)) return -1;
    progress("Saving language and computer name", "Сохранение языка и имени компьютера", 92);
    /* Give every installation its own volume identity. This is not a secret:
     * mix the build UUID with keyboard timing (the running PIT) and device. */
    if (disk_read(2, verify_buffer)) return -1;
    unsigned int mix = *(volatile unsigned int *)0x46c ^ target.sectors ^ (target.id << 24);
    for (int i = 0; i < 16; i++) {
        unsigned char tick; __asm__ volatile("inb %1, %0" : "=a"(tick) : "Nd"((unsigned short)0x40));
        mix ^= tick + i; mix ^= mix << 13; mix ^= mix >> 17; mix ^= mix << 5;
        verify_buffer[104 + i] ^= (unsigned char)mix;
    }
    verify_buffer[110] = (verify_buffer[110] & 0x0f) | 0x40;
    verify_buffer[112] = (verify_buffer[112] & 0x3f) | 0x80;
    if (disk_write(2, verify_buffer) || disk_flush() || fs_init()) return -1;
    char text[128]; strcpy(text, hostname); strcat(text, "\n");
    if (fs_write("/etc/hostname", text, strlen(text))) return -1;
    const char *language = russian ? "LANG=ru\n" : "LANG=en\n";
    if (fs_write("/etc/sys-lang.cfg", language, strlen(language))) return -1;
    strcpy(text, "PROFILE="); strcat(text, profile_ids[profile]); strcat(text, "\nCOMPONENTS=minimal");
    if (component_network) strcat(text, ",network");
    if (component_games) strcat(text, ",games");
    if (component_gcc) strcat(text, ",gcc");
    strcat(text, "\nHOSTNAME=");
    strcat(text, hostname); strcat(text, "\nINSTALLER=Barnix-Setup\n");
    if (fs_write("/etc/install.cfg", text, strlen(text))) return -1;
    if (component_network && install_package("network", "Downloading network tools", "Загрузка сетевых утилит", 76)) return -2;
    if (component_games && install_package("games", "Downloading games and examples", "Загрузка игр и примеров", 81)) return -2;
    if (component_gcc && install_package("gcc", "Downloading GCC C/C++", "Загрузка GCC C/C++", 86)) return -2;
    /* After the packages, and only with room to spare (a small partition
     * keeps its space for the system): an installation works without it. */
    unsigned int free_kib = 0;
    if (game_size[0] > 0 && !fs_space("/", &free_kib, 0) &&
        free_kib > (unsigned int)(game_size[0] + (game_size[1] > 0 ? game_size[1] : 0)) / 1024 + 2048) {
        fs_mkdir("/mc"); fs_mkdir("/mc/minecraft");
        for (int i = 0; i < 2; i++)
            if (game_size[i] > 0 && fs_write(game_files[i], (const char *)game_data[i], game_size[i])) fs_rm(game_files[i]);
    }
    
    if (fs_write("/etc/userpasswd.cfg","",0)) return -1;
    for(int i=0;i<user_count;i++) {
        if(account_create(users[i].username,users[i].password))return -1;
        char path[96];strcpy(path,"/home/");strcat(path,users[i].username);strcat(path,"/useretc/sys-lang.cfg");
        if(fs_write(path,language,strlen(language)))return -1;
        memset(users[i].password,0,sizeof(users[i].password));
    }
    if (fs_sync()) return -1;
    disk_restore(target);
    progress("Enabling boot from the installed disk", "Подготовка загрузки с установленного диска", 98);
    if(!valid_layout())return -1;
#ifndef BARNIX_EFI
    memcpy(layout.sector,payload,440);
    unsigned int core_lba=1+root_slot*512;
    for(int i=0;i<8;i++)layout.sector[0x5c+i]=i<4?(core_lba>>(8*i)):0;
#else
    memset(layout.sector,0,440);
#endif
    layout.entries[root_slot].boot=0x80;
    if(partition_write(&layout))return -1;
    return 0;
}
static void restore_live(void)
{ disk_select("ram0"); fs_init(); }
static void reboot(void)
{
    fs_sync();
    for (int i = 0; i < 100000; i++) {
        unsigned char status; __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((unsigned short)0x64));
        if (!(status & 2)) break;
    }
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0xfe), "Nd"((unsigned short)0x64));
    for (;;) __asm__ volatile("hlt");
}
void setup_run(const MultibootInfo *info)
{
    setup_info = info;
    keyboard_set_layout(0); cursor_off();
    int stage = 0;
    for (;;) {
        if (stage == 0) {
            frame(0, say("Welcome to Barnix Setup", "Добро пожаловать в Barnix Setup"));
            label(25, 8, 0x07, say("Install a bootable Barnix system on a disk.", "Установите загрузочную систему Barnix на диск."));
            label(25, 10, 0x08, say("Nothing is written until you confirm the disk.", "Запись начнётся только после подтверждения."));
            option(13, !russian, "English"); option(15, russian, "Русский");
            label(25, 18, 0x0b, say("ESC opens the LiveCD shell without installing.", "ESC: оболочка LiveCD без установки."));
            unsigned int key = wizard_key();
            if(key==SETUP_OPTION||key==SETUP_OPTION+1)russian=key-SETUP_OPTION;
            if (key == KEY_UP || key == KEY_DOWN) russian = !russian;
            if (key == KEY_ESCAPE) break;
            if (key == '\n') stage = 1;
        } else if (stage == 1) stage = choose_disk() ? (choose_layout() ? 2 : 1) : 0;
        else if (stage == 2) stage = choose_profile() ? 3 : 1;
        else if (stage == 3) stage = network_settings() ? 4 : 2;
        else if (stage == 4) stage = settings() ? 5 : 3;
        else if (stage == 5) stage = configure_users() ? 6 : 4;
        else if (stage == 6) {
            if (!review()) { stage = 4; continue; }
            progress("Preparing installation", "Подготовка установки", 0);
            int result = install_image(info);
            restore_live();
            if (result) { error_page(result == -2 ? "Package download or checksum failed." : "Disk write, verification or image check failed.",
                                     result == -2 ? "Ошибка загрузки или контрольной суммы пакета." : "Ошибка записи, проверки диска или образа."); stage = 1; continue; }
            frame(8, say("Barnix is installed!", "Barnix установлен!"));
            label(25, 8, 0x0a, say("Installation completed and verified.", "Установка завершена, данные проверены."));
            label(25, 10, 0x07, say("Disk:", "Диск:")); label(35, 10, 0x0f, targets[selected].name);
            label(25, 11, 0x07, say("Components:", "Компоненты:")); label(38, 11, 0x0b, profile_titles[profile]);
            label(25, 14, 0x0e, say("Remove the CD and boot the selected disk.", "Извлеките CD и загрузитесь с выбранного диска."));
            label(25, 17, 0x0f, say("ENTER: reboot    ESC: return to LiveCD shell", "ENTER: перезагрузка    ESC: оболочка LiveCD"));
            for (;;) { unsigned int key = wizard_key(); if (key == '\n') reboot(); if (key == KEY_ESCAPE) { cursor_on(); return; } }
        }
    }
    cursor_on();
}
