#include "command.h"

/* The list is longer than the 25-row text screen: pause after each page. */
#define PAGE_ROWS 22
static int rows, quit;

static void line(int color, const char *text)
{
    if (quit) return;
    unsigned int characters = 0;
    for (const char *p = text; *p; p++)
        if (((unsigned char)*p & 0xc0) != 0x80) characters++;
    int height = characters ? (int)((characters + 79) / 80) : 1;
    if (rows + height > PAGE_ROWS) {
        barnix->print(14, barnix->translate("-- more: any key, Q or Esc - quit --"));
        unsigned int key = barnix->getch();
        /* Erase the prompt line (79 spaces keep the cursor on this row). */
        barnix->print(15, "\r                                                                               \r");
        if (key == 'q' || key == 'Q' || key == KEY_ESCAPE) { quit = 1; return; }
        rows = 0;
    }
    rows += height;
    barnix->println(color, text);
}

int main(int argc, const char *const *argv)
{
    (void)argc; (void)argv;
    line(15, barnix->translate("Barnix Shell"));

    line(15, barnix->translate("help        - show help"));
    line(15, barnix->translate("clear       - clear screen"));
    line(15, barnix->translate("ls          - list files"));
    line(15, barnix->translate("cat <file>  - print file"));
    line(15, barnix->translate("touch <f>   - create file"));
    line(15, barnix->translate("write f txt - write text"));
    line(15, barnix->translate("rm <file>   - remove file"));
    line(15, barnix->translate("mkdir / cd  - create / enter directory"));
    line(15, barnix->translate("pwd         - current directory"));
    line(15, barnix->translate("rmdir <dir> - remove empty directory"));
    line(15, barnix->translate("cp src dst  - copy file (new name)"));
    line(15, barnix->translate("mv old new  - rename file or directory"));
    line(15, barnix->translate("append f txt- append text (no added newline)"));
    line(15, barnix->translate("stat <name> - file or directory details"));
    line(15, barnix->translate("df          - filesystem usage and limits"));
    line(15, barnix->translate("diskinfo    - disk type and capacity"));
    line(15, barnix->translate("devices [lsblk] - list devices or disk usage and mounts"));
    line(15, barnix->translate("mount dev dir - mount a device at a directory"));
    line(15, barnix->translate("unmount [dir] - flush and unmount filesystem"));
    line(15, barnix->translate("sync        - save filesystem to disk"));
    line(15, barnix->translate("./f args    - run a Barnix ELF32 program"));
    line(15, barnix->translate("echo text   - print text"));
    line(15, barnix->translate("panic       - crash system"));
    line(15, barnix->translate("init        - reload system configuration"));
    line(15, barnix->translate("Shift+Alt   - switch EN/RU keyboard layout"));
    line(15, barnix->translate("Ctrl+Alt+M  - keyboard mouse: arrows move, Space/Enter click, Shift+Space right"));
    line(15, barnix->translate("bnm [internet | connect WIFI [PASSWORD] | get-networks | off] - network"));
    line(15, barnix->translate("bnm test-ping [HOST] - ping; bnm resolve HOST - look up an address"));
    line(15, barnix->translate("get URL [FILE] - download over HTTP or HTTPS"));
    line(15, barnix->translate("git clone URL [DIR] - fetch the newest commit of a repository"));
    line(15, barnix->translate("brm -S NAME - install a program from the repository; brm -h - all options"));
    line(15, barnix->translate("cmd | cmd   - pipe two Linux commands"));
    line(15, barnix->translate("< > >>      - Linux input / output / append"));
    line(15, barnix->translate("Quotes keep spaces and operators in arguments"));
    line(15, "");
    line(15, barnix->translate("format DRIVE - erase drive and create empty ext2"));
    line(15, "su [USER], sudo COMMAND, useradd USER, whoami, logout");
    line(15, "chmod USER=rwx [passwd=PASSWORD] file=/path");
    line(15, "theme NAME; settings: /home/USER/useretc");
    line(15, "execute - Barnix Execute desktop and file manager");
    line(15, "snake; pong [1|2]; fdisk DEVICE list (run fdisk for usage)");
    line(15, "Partitions: ata0p1..ata0p4, disk2p1, etc.");
    line(15, "paint [FILE.bmp] - mouse pixel-art editor, palette, fill, undo, BMP");
    line(15, "barnocraft - voxel sandbox: WASD, mouse/arrows, X/C break/place, E inventory");
    line(15, "gouse - Happy Gouse windowed environment: apps, games, many windows at once");
    line(15, "play [file.wav] - play a WAV file, or a fanfare; beep [hz] [ms] - PC speaker tone");
    line(15, "sounds [on|off] - shell sounds (login chime, error tone)");
    return 0;
}
