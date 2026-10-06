/* Barnix Execute: a single-task desktop and file manager inspired by Executive. */
#include <barnix_graphics.h>
#include "barnix_app.h"
#include "../text.h"

#define MAX_ENTRIES 256
#define NAME_CAP 256
#define PREVIEW_LINES 192
#define PREVIEW_BYTES 8192
#define ID_FILES 1
#define ID_OPEN 2
#define ID_UP 3
#define ID_PROGRAMS 4
#define ID_HOME 5
#define ID_REFRESH 6
#define ID_VIEW 7
#define ID_COPY 8
#define ID_RENAME 9
#define ID_DELETE 10
#define ID_MKDIR 11
#define ID_ABOUT 12
#define ID_EXIT 13

typedef struct { char name[NAME_CAP], label[NAME_CAP+16]; unsigned int kind; } Entry;
static BglApp app;
static Entry entries[MAX_ENTRIES];
static const char *rows[MAX_ENTRIES];
static char cwd[256], status[128], title[320];
static char input[256], source[256];
static char preview_data[PREVIEW_BYTES+1], preview_lines[PREVIEW_LINES][256];
static const char *preview_rows[PREVIEW_LINES];
static int count, truncated;

static unsigned int length(const char *s) { unsigned int n=0;while(s[n])n++;return n; }
static int compare(const char *a,const char *b) { while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b; }
static void copy(char *out,const char *s,unsigned int capacity) {
    unsigned int i=0;while(i+1<capacity&&s[i]){out[i]=s[i];i++;}out[i]=0;
}
static void append(char *out,const char *s,unsigned int capacity) { unsigned int n=length(out);if(n<capacity)copy(out+n,s,capacity-n); }
static void message(const char *s) { copy(status,s,sizeof(status)); }
static int equal(const char *a,const char *b) { return compare(a,b)==0; }
static BglWidget *files(void) { return &app.widgets[0]; }
static Entry *selected(void) { int i=files()->selected;return i>=0&&i<count?&entries[i]:0; }
static void branding(void) {
    app.desktop_title="Barnix Execute     File manager & program launcher";
    app.hint="Tab: focus  Enter: open  Backspace: up  Esc: exit";
}
static void label(int id,int x,int y,int width,const char *s) { bgl_label(&app,id,x,y,width,s); }
static void button(int id,int x,int y,int width,const char *s) { bgl_button(&app,id,x,y,width,s); }
static void layout(void) {
    copy(title,"Barnix Execute - ",sizeof(title));append(title,cwd,sizeof(title));
    bgl_init(&app,title,8,16,624,368);branding();
    bgl_list(&app,ID_FILES,12,88,600,192,rows,count);
    button(ID_OPEN,12,32,96,"Open");button(ID_UP,116,32,80,"Up");
    button(ID_PROGRAMS,204,32,128,"Programs");button(ID_HOME,340,32,120,"Home");
    button(ID_REFRESH,468,32,144,"Refresh");
    label(20,12,64,600,cwd);label(21,12,288,600,status);
    button(ID_VIEW,12,320,72,"View");button(ID_COPY,92,320,72,"Copy");
    button(ID_RENAME,172,320,80,"Rename");button(ID_DELETE,260,320,80,"Delete");
    button(ID_MKDIR,348,320,88,"New dir");button(ID_ABOUT,444,320,80,"About");
    button(ID_EXIT,532,320,80,"Exit");
}
static int before(const Entry *a,const Entry *b) {
    if(a->kind==4&&b->kind!=4)return 1;
    if(a->kind!=4&&b->kind==4)return 0;
    return compare(a->name,b->name)<0;
}
static void refresh(const char *keep) {
    char saved[256];copy(saved,keep?keep:"",sizeof(saved));
    count=0;truncated=0;
    if(barnix->getcwd(cwd,sizeof(cwd))<0)copy(cwd,"/",sizeof(cwd));
    unsigned int cursor=0;BarnixDirEntry item;int result;
    while((result=barnix->directory(".",&cursor,&item))>0) {
        if(equal(item.name,".")||equal(item.name,".."))continue;
        if(count==MAX_ENTRIES){truncated=1;break;}
        copy(entries[count].name,item.name,sizeof(entries[count].name));
        entries[count++].kind=item.kind;
    }
    for(int i=1;i<count;i++) {
        Entry value=entries[i];int j=i;
        while(j>0&&before(&value,&entries[j-1])){entries[j]=entries[j-1];j--;}
        entries[j]=value;
    }
    for(int i=0;i<count;i++) {
        copy(entries[i].label,entries[i].kind==4?"[DIR]  ":"       ",sizeof(entries[i].label));
        append(entries[i].label,entries[i].name,sizeof(entries[i].label));rows[i]=entries[i].label;
    }
    message(result<0?"Cannot read directory: access denied or disk error.":truncated?"First 256 entries shown. Open a subdirectory to browse further.":count?"Select a file or directory. Open runs ELF programs; View reads text.":"This directory is empty.");
    layout();
    for(int i=0;i<count;i++)if(equal(entries[i].name,saved)) {
        files()->selected=i;files()->first=i>=12?i-11:0;break;
    }
}
static void change_directory(const char *path) {
    if(barnix->cd(path)){message("Cannot open directory: access denied or directory unavailable.");return;}
    refresh(0);
}
/* Modal dialogs reuse the static canvas; callers rebuild the desktop afterwards. */
static int dialog(const char *heading,const char *prompt,int edit,int confirm) {
    bgl_init(&app,heading,96,96,448,192);branding();
    app.hint=edit?"Type a name or path. Enter: accept  Esc: cancel":"Enter: confirm  Esc: cancel";
    button(100,24,144,160,confirm?"Confirm":"OK");button(101,256,144,160,"Cancel");
    label(102,24,40,400,prompt);
    if(edit)label(103,24,80,400,input);
    else label(103,24,80,400,source);
    for(;;) {
        bgl_draw(&app);BglEvent event=bgl_poll(&app);
        if(event.type==BGL_EVENT_CLOSE||(event.type==BGL_EVENT_CLICK&&event.id==101))return 0;
        if(event.type==BGL_EVENT_CLICK&&event.id==100)return 1;
        if(edit) {
            unsigned int n=length(input);
            if(event.key=='\b'&&n){do {n--;}while(n&&((unsigned char)input[n]&0xc0)==0x80);input[n]=0;}
            else if(event.key>=32&&event.key!=127&&!(event.key>=KEY_UP&&event.key<=KEY_MOUSE)) {
                char encoded[4];unsigned int bytes=utf8_encode(event.key,encoded);
                if(n+bytes<sizeof(input)){for(unsigned int i=0;i<bytes;i++)input[n++]=encoded[i];input[n]=0;}
            }
        }
        barnix->sleep_ms(10);
    }
}
static void file_operation(int action) {
    Entry *entry=selected();
    if(action!=ID_MKDIR&&!entry){message("Select a file first.");return;}
    unsigned int kind=entry?entry->kind:0;
    copy(source,entry?entry->name:"",sizeof(source));input[0]=0;
    if(action==ID_RENAME)copy(input,source,sizeof(input));
    const char *heading=action==ID_COPY?"Copy file":action==ID_RENAME?"Rename / move":action==ID_DELETE?"Delete permanently?":"Create directory";
    const char *prompt=action==ID_DELETE?"This cannot be undone. Confirm deletion:":"Enter destination name or path:";
    if(action==ID_COPY&&kind==4){message("Directory copy is not supported; select a regular file.");return;}
    int accepted=dialog(heading,prompt,action!=ID_DELETE,action==ID_DELETE);
    int result=-1;
    if(accepted&&(action==ID_DELETE||input[0])) {
        if(action==ID_COPY)result=barnix->cp(source,input);
        else if(action==ID_RENAME)result=barnix->mv(source,input);
        else if(action==ID_MKDIR)result=barnix->mkdir(input);
        else result=kind==4?barnix->rmdir(source):barnix->rm(source);
    }
    refresh(action==ID_RENAME||action==ID_MKDIR?input:source);
    if(accepted)message(result?"Operation failed: check permissions, destination and available space.":"Operation completed.");
}
static void view_file(void) {
    Entry *entry=selected();if(!entry||entry->kind==4){message("Select a regular file to view.");return;}
    copy(source,entry->name,sizeof(source));
    int n=barnix->read(source,0,preview_data,PREVIEW_BYTES);
    if(n<0){message("Cannot read file: access denied or disk error.");return;}
    for(int i=0;i<n;i++)if(!preview_data[i]){message("Binary file: use Open to run ELF programs.");return;}
    preview_data[n]=0;
    int line=0,column=0;
    for(int i=0;i<n&&line<PREVIEW_LINES;i++) {
        unsigned char ch=preview_data[i];
        if(ch=='\r')continue;
        if(ch=='\n'||column>=68) {
            preview_lines[line][column]=0;preview_rows[line]=preview_lines[line];line++;column=0;
            if(line==PREVIEW_LINES)break;
            if(ch=='\n')continue;
        }
        /* Keep multibyte UTF-8 characters together at wrapping boundaries. */
        if(column>=64&&(ch&0xc0)!=0x80) {
            preview_lines[line][column]=0;preview_rows[line]=preview_lines[line];line++;column=0;
            if(line==PREVIEW_LINES)break;
        }
        preview_lines[line][column++]=ch=='\t'?' ':ch<32?'?':ch;
    }
    if(line<PREVIEW_LINES){preview_lines[line][column]=0;preview_rows[line]=preview_lines[line];line++;}
    bgl_init(&app,"Barnix Execute - Text viewer",8,16,624,368);branding();
    app.hint="Up/Down, Home/End, mouse wheel: scroll  Esc: back";
    bgl_list(&app,200,12,64,600,240,preview_rows,line);
    label(201,12,32,600,source);label(202,12,312,600,"Preview: first 8192 bytes / 192 wrapped lines. Read only.");
    button(203,484,336,128,"Back");
    for(;;){bgl_draw(&app);BglEvent event=bgl_poll(&app);if(event.type==BGL_EVENT_CLOSE||(event.type==BGL_EVENT_CLICK&&event.id==203))break;barnix->sleep_ms(10);}
    refresh(source);
}
static int open_selected(void) {
    Entry *entry=selected();if(!entry)return 0;
    if(entry->kind==4){change_directory(entry->name);return 0;}
    unsigned char magic[4];
    int readable=barnix->read(entry->name,0,magic,4)==4;
    if(readable&&magic[0]==127&&magic[1]=='E'&&magic[2]=='L'&&magic[3]=='F') {
        if(barnix->launch(entry->name)){message("Cannot run this program: permission denied or invalid executable.");return 0;}
        bgl_close(&app);return 1;
    }
    /* Execute-only programs intentionally reject read(). Give the kernel a
     * chance to load them; launch() checks x permission and ELF validity
     * without exposing their contents through the ordinary file API. */
    if(!readable&&barnix->launch(entry->name)==0){bgl_close(&app);return 1;}
    view_file();return 0;
}
int main(int argc,const char *const *argv) {
    (void)argc;(void)argv;
    refresh(0);
    for(;;) {
        bgl_draw(&app);BglEvent event=bgl_poll(&app);
        int action=event.type==BGL_EVENT_CLICK?event.id:0;
        if(event.type==BGL_EVENT_CLOSE||action==ID_EXIT)break;
        if(event.key=='\b')action=ID_UP;
        /* Letter shortcuts only apply while the file list has focus. */
        if(app.focus==0) {
            unsigned int key=event.key;
            if(key>='A'&&key<='Z')key+=32;
            if(key=='o')action=ID_OPEN;
            if(key=='v')action=ID_VIEW;
            if(key=='c')action=ID_COPY;
            if(key=='m')action=ID_RENAME;
            if(key=='d'||key==KEY_DELETE)action=ID_DELETE;
            if(key=='n')action=ID_MKDIR;
            if(key=='p')action=ID_PROGRAMS;
            if(key=='h')action=ID_HOME;
            if(key=='r')action=ID_REFRESH;
        }
        if(action==ID_FILES||action==ID_OPEN){if(open_selected())return 0;}
        else if(action==ID_UP)change_directory("..");
        else if(action==ID_PROGRAMS)change_directory("/bin");
        else if(action==ID_HOME){char home[256];copy(home,"/home/",sizeof(home));append(home,barnix->current_user(),sizeof(home));change_directory(home);}
        else if(action==ID_REFRESH){Entry *entry=selected();refresh(entry?entry->name:0);}
        else if(action==ID_VIEW)view_file();
        else if(action==ID_COPY||action==ID_RENAME||action==ID_DELETE||action==ID_MKDIR)file_operation(action);
        else if(action==ID_ABOUT) {
            copy(source,"Single-task desktop for Barnix. C + BGL.",sizeof(source));
            dialog("About Barnix Execute","Inspired by the classic MS-DOS Executive.",0,0);refresh(0);
        }
        barnix->sleep_ms(10);
    }
    bgl_close(&app);return 0;
}
