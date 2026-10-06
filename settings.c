#include "barnix.h"
#include "fs.h"
#include "permissions.h"
#include "settings.h"
#include "lang.h"
static const char *names[] = {"default","dark","light","solarized","monokai","dracula","nord","bios","pink-panther","green"};
static const unsigned char colors[] = {0x0f,0x07,0x70,0x36,0x0a,0x05,0x1b,0x1f,0x5f,0x02};
void user_config_path(const char *file,char *out) {
    strcpy(out,"/home/");strcat(out,get_current_user());strcat(out,"/useretc/");strcat(out,file);
}
static int theme_index(const char *name) {
    for(int i=0;i<10;i++)if(!strcmp(names[i],name))return i;
    return -1;
}
int theme_set(const char *name) {
    static unsigned int next=0x6d2b79f5U;
    next=next*1664525U+1013904223U;
    int i=!strcmp(name,"random") ? ((next>>16) % 10) : theme_index(name);
    if(i<0)return -1;
    char path[96]; user_config_path("theme.cfg",path);
    if(fs_write(path,names[i],strlen(names[i])))return -1;
    console_theme(colors[i]>>4,colors[i]&15);return 0;
}
void settings_load(void) {
    char path[96],text[64]; user_config_path("theme.cfg",path);
    int n=fs_size(path),i=0;
    if(n>0 && n<64 && fs_read(path,0,text,n)==n) { text[n]=0; int found=theme_index(text);if(found>=0)i=found; }
    console_theme(colors[i]>>4,colors[i]&15);
    language_reset();
    system_init();
}
