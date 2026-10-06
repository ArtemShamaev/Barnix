#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argc; (void)argv;
    barnix->puts(barnix->translate("Barnix Shell"));

    barnix->println(15, barnix->translate("help        - show help"));
    barnix->println(15, barnix->translate("clear       - clear screen"));
    barnix->println(15, barnix->translate("ls          - list files"));
    barnix->println(15, barnix->translate("cat <file>  - print file"));
    barnix->println(15, barnix->translate("touch <f>   - create file"));
    barnix->println(15, barnix->translate("write f txt - write text"));
    barnix->println(15, barnix->translate("rm <file>   - remove file"));
    barnix->println(15, barnix->translate("mkdir / cd  - create / enter directory"));
    barnix->println(15, barnix->translate("pwd         - current directory"));
    barnix->println(15, barnix->translate("rmdir <dir> - remove empty directory"));
    barnix->println(15, barnix->translate("cp src dst  - copy file (new name)"));
    barnix->println(15, barnix->translate("mv old new  - rename file or directory"));
    barnix->println(15, barnix->translate("append f txt- append text (no added newline)"));
    barnix->println(15, barnix->translate("stat <name> - file or directory details"));
    barnix->println(15, barnix->translate("df          - filesystem usage and limits"));
    barnix->println(15, barnix->translate("diskinfo    - disk type and capacity"));
    barnix->println(15, barnix->translate("devices [lsblk] - list devices or disk usage and mounts"));
    barnix->println(15, barnix->translate("mount dev dir - mount a device at a directory"));
    barnix->println(15, barnix->translate("unmount [dir] - flush and unmount filesystem"));
    barnix->println(15, barnix->translate("sync        - save filesystem to disk"));
    barnix->println(15, barnix->translate("./f args    - run a Barnix ELF32 program"));
    barnix->println(15, barnix->translate("echo text   - print text"));
    barnix->println(15, barnix->translate("panic       - crash system"));
    barnix->println(15, barnix->translate("init        - reload system configuration"));
    barnix->println(15, barnix->translate("Shift+Alt   - switch EN/RU keyboard layout"));
    barnix->println(15, barnix->translate("bnm connect WIFI_NAME - connect to Wi-Fi"));
    barnix->println(15, barnix->translate("bnm internet - connect by cable"));
    barnix->println(15, barnix->translate("bnm get-networks - list Wi-Fi networks"));
    barnix->println(15, barnix->translate("bnm test-ping - test Internet latency"));
    barnix->println(15, barnix->translate("get URL [FILE] - download a file"));
    barnix->println(15, barnix->translate("git clone URL - clone a repository"));
    barnix->println(15, barnix->translate("cmd | cmd   - pipe two Linux commands"));
    barnix->println(15, barnix->translate("< > >>      - Linux input / output / append"));
    barnix->println(15, barnix->translate("Quotes keep spaces and operators in arguments"));
    barnix->println(15, "");
    barnix->println(15, barnix->translate("format DRIVE - erase drive and create empty ext2"));
    barnix->puts("su [USER], sudo COMMAND, useradd USER, whoami, logout");
    barnix->puts("chmod USER=rwx [passwd=PASSWORD] file=/path");
    barnix->puts("theme NAME; settings: /home/USER/useretc");
    barnix->puts("execute - Barnix Execute desktop and file manager");
    barnix->puts("snake; pong [1|2]; fdisk DEVICE list (run fdisk for usage)");
    barnix->puts("Partitions: ata0p1..ata0p4, disk2p1, etc.");
    barnix->puts("paint [FILE.bmp] - mouse pixel-art editor, palette, fill, undo, BMP");
    return 0;
}
