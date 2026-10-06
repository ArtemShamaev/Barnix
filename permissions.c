/* Included by fs.c: private access to namespace normalization, never exported
 * as an application bypass. All public VFS entry points enforce this policy. */
#include "permissions.h"
#include "password.c"
static char current_user[MAX_USERNAME] = "root";
static int permission_internal;
const char *get_current_user(void) { return current_user; }
int permissions_root(void) { return !strcmp(current_user, "root"); }
void permissions_restore(const char *user) { strcpy(current_user, user); }
static int valid_user(const char *s) {
    int n = strlen(s);
    if (!n || n >= MAX_USERNAME || n > MAX_NAME) return 0;
    for (int i = 0; i < n; i++)
        if (!(s[i] >= 'a' && s[i] <= 'z') && !(s[i] >= '0' && s[i] <= '9') && s[i] != '_' && s[i] != '-') return 0;
    return 1;
}
static int private_read(const char *path, char *out, int cap) {
    permission_internal++;
    int n = fs_size(path);
    if (n < 0 || n >= cap || fs_read(path, 0, out, n) != n) n = -1;
    if (n >= 0) out[n] = 0;
    permission_internal--;
    return n;
}
/* Account records remain compatible with existing installations. Password
 * storage is root-only; no default account is silently created on bad input. */
int authenticate_user(const char *user, const char *password) {
    char data[4096];
    if (!valid_user(user) || private_read("/etc/userpasswd.cfg", data, sizeof(data)) < 0) return -1;
    char *p = data;
    while (*p) {
        char *name = p; while (*p && *p != ':' && *p != '\n') p++;
        if (*p != ':') return -1;
        *p++ = 0; char *secret = p; while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (!strcmp(name, user)) return password_matches(password, secret) ? 0 : -1;
    }
    return -1;
}
int account_create(const char *user, const char *password) {
    namespace_start();
    if (!permissions_root() || !valid_user(user) || !*password || strlen(password) >= MAX_PASSWORD ||
        strchr(password, '\n') || strchr(password, '\r')) return -1;
    char encoded[96], salt[17];
    /* Mix a per-boot counter and account identity; platform timing is supplied by the caller. */
    static unsigned int nonce = 0x6d2b79f5;
#if defined(__i386__) && !defined(BARNIX_HOST_TEST)
    for(int i=0;i<16;i++){unsigned char tick;__asm__ volatile("inb %1,%0":"=a"(tick):"Nd"((unsigned short)0x40));nonce=nonce*1664525U+tick+1013904223U;}
#endif
    for (const char *p = user; *p; p++) nonce = nonce * 1664525U + (unsigned char)*p + 1013904223U;
    for (int i=0;i<16;i++) { nonce=nonce*1664525U+1013904223U; salt[i]="0123456789abcdef"[nonce>>28]; }
    salt[16]=0; password_encode(password,salt,encoded);
    char data[4096]; int n = private_read("/etc/userpasswd.cfg", data, sizeof(data));
    if (n < 0) { if (fs_size("/etc/userpasswd.cfg") >= 0) return -1; n = 0; data[0] = 0; }
    for (char *p = data; *p;) {
        if (!strncmp(p, user, strlen(user)) && p[strlen(user)] == ':') return -1;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    if (n + strlen(user) + strlen(encoded) + 3 >= (int)sizeof(data)) return -1;
    char home[96]; fs_mkdir("/etc"); fs_mkdir("/home");
    strcpy(home, "/home/"); strcat(home, user);
    if (!fs_is_dir(home) && fs_mkdir(home)) return -1;
    strcat(home, "/useretc"); if (!fs_is_dir(home) && fs_mkdir(home)) return -1;
    char config[64];int config_size=private_read("/etc/sys-lang.cfg",config,sizeof(config));
    if(config_size<0){strcpy(config,"LANG=en\n");config_size=8;}
    strcat(home,"/sys-lang.cfg");if(fs_write(home,config,config_size))return -1;
    strcat(data, user); strcat(data, ":"); strcat(data, encoded); strcat(data, "\n");
    return fs_write("/etc/userpasswd.cfg", data, strlen(data));
}
int switch_user(const char *user, const char *password) {
    /* Even root must name an existing account. */
    if (authenticate_user(user, password)) {
        if (!permissions_root()) return -1;
        char data[4096], prefix[40];
        if (!valid_user(user)) return -1;
        strcpy(prefix, user); strcat(prefix, ":");
        if (!valid_user(user) || private_read("/etc/userpasswd.cfg", data, sizeof(data)) < 0) return -1;
        char *p = data; int found = 0;
        while (*p) { if (!strncmp(p, prefix, strlen(prefix))) found = 1; while (*p && *p != '\n') p++; if (*p) p++; }
        if (!found) return -1;
    }
    namespace_start();
    strcpy(current_user, user);
    char home[64]; strcpy(home, "/home/"); strcat(home, user);
    if (fs_cd(home)) { permission_internal++; fs_cd("/"); permission_internal--; }
    return 0;
}
int check_permission(const char *name, char mode) {
    if (permission_internal || permissions_root()) return 1;
    char path[PATH_CAP], home[64];
    namespace_start();
    if (normalize(name, path)) return 0;
    /* Credentials and ACL metadata cannot be delegated. */
    if (beneath(path, "/etc")) return 0;
    strcpy(home, "/home/"); strcat(home, current_user);
    int allowed = beneath(path, home) || (mode == 'x' && beneath(path, "/bin"));
    if (mode == 'x' && (!strcmp(path, "/") || !strcmp(path, "/home"))) allowed = 1;
    char data[8192];
    int acl_size=private_read("/etc/access.cfg", data, sizeof(data));
    if(acl_size<0) { permission_internal++;int exists=fs_size("/etc/access.cfg")>=0;permission_internal--;if(exists)return 0; }
    int specificity=-1;
    if (acl_size >= 0) {
        for (char *p = data; *p;) {
            char *file = p; while (*p && *p != '|' && *p != '\n') p++;
            if (*p != '|') return 0;
            *p++ = 0; char *user = p; while (*p && *p != '|') p++;
            if (!*p) return 0;
            *p++ = 0; char *rights = p; while (*p && *p != '|' && *p != '\n') p++;
            if (!*p) return 0;
            char separator = *p; *p++ = 0;
            if (beneath(path, file) && !strcmp(user, current_user) && strlen(file)>=specificity) {
                allowed = strchr(rights, mode) != 0; specificity=strlen(file);
            }
            if (separator == '|') { while (*p && *p != '\n') p++; if (*p) p++; }
        }
    }
    return allowed;
}
int set_file_permissions(const char *name, const char *spec, const char *password) {
    char path[PATH_CAP], user[MAX_USERNAME], data[8192];
    namespace_start();
    if (normalize(name, path) || strchr(path, '|') || strchr(path, '\n')) return -1;
    const char *equal = strchr(spec, '=');
    if (!equal || equal - spec >= MAX_USERNAME || equal == spec) return -1;
    memcpy(user, spec, equal-spec); user[equal-spec] = 0;
    if (!valid_user(user)) return -1;
    for (const char *p = equal + 1; *p; p++) if (!strchr("rwx", *p)) return -1;
    if (strlen(equal + 1) > 3) return -1;
    int n = private_read("/etc/access.cfg", data, sizeof(data));
    if (n < 0) { n = 0; data[0] = 0; }
    char lock[96] = "", encoded[96] = "";
    int lock_depth=-1;
    /* The most recent lock belongs to the file, not a particular ACL user. */
    for (const char *p=data; *p;) {
        const char *start=p; while(*p && *p!='|')p++;
        int plen=p-start;
        int match=plen<=strlen(path) && !strncmp(start,path,plen) && (!path[plen]||path[plen]=='/');
        for(int i=0;i<2 && *p;i++) { p++;while(*p && *p!='|' && *p!='\n')p++; }
        if(*p=='|') {
            const char *secret=++p;while(*p && *p!='\n')p++;
            if(match && p>secret && plen>=lock_depth) { int length=p-secret;if(length>=96)return -1;memcpy(lock,secret,length);lock[length]=0;lock_depth=plen; }
        } else while(*p && *p!='\n')p++;
        if(*p)p++;
    }
    if (!permissions_root()) {
        if (beneath(path,"/etc")) return -13;
        char home[64];strcpy(home,"/home/");strcat(home,current_user);
        if (*lock ? !password_matches(password,lock) : !beneath(path,home)) return -13;
        strcpy(encoded,lock);
    } else if (*password) {
        if(strlen(password)>=MAX_PASSWORD)return -1;
        /* A distinct file identity supplies the salt; account salts are separate. */
        unsigned int mix=2166136261U;char salt[17];
        for(const char *p=path;*p;p++)mix=(mix^(unsigned char)*p)*16777619U;
        for(int i=0;i<16;i++){mix=mix*1664525U+1013904223U;salt[i]="0123456789abcdef"[mix>>28];}
        salt[16]=0;password_encode(password,salt,encoded);
    } else strcpy(encoded,lock);
    permission_internal++;
    int exists=fs_size(path)>=0 || fs_is_dir(path);
    permission_internal--;
    if(!exists)return -1;
    /* Replace this user record so repeated chmod does not consume the table. */
    char output[8192];int used=0;
    for(const char *p=data;*p;) {
        const char *start=p;while(*p && *p!='\n')p++;
        int length=p-start;if(*p)p++;
        int skip=!strncmp(start,path,strlen(path)) && start[strlen(path)]=='|' &&
            !strncmp(start+strlen(path)+1,user,strlen(user)) && start[strlen(path)+1+strlen(user)]=='|';
        if(!skip) { memcpy(output+used,start,length);used+=length;output[used++]='\n'; }
    }
    output[used]=0;
    if(used+strlen(path)+strlen(spec)+strlen(encoded)+5>=(int)sizeof(output))return -1;
    strcat(output,path);strcat(output,"|");strcat(output,user);strcat(output,"|");
    strcat(output,equal+1);strcat(output,"|");strcat(output,encoded);strcat(output,"\n");
    permission_internal++;
    int result=fs_write("/etc/access.cfg",output,strlen(output));
    if(!result && strchr(equal+1,'x')) {
        char local[PATH_CAP];
        if(route(path,local,0)<0 || begin())result=-1;
        else {
            int ino=lookup_path(local);unsigned char *node=ino>0?inode(ino):NULL;
            if(node)put16(node,get16(node)|0111);
            result=finish(node?0:-1);
        }
    }
    permission_internal--;
    return result;
}
/* Executable contents are available to the loaders under x permission without
 * making an execute-only program readable through ordinary file APIs. */
int fs_load_executable(const char *path, void *buffer, unsigned int capacity) {
    if (!check_permission(path, 'x')) return -13;
    permission_internal++;
    int result = fs_exec_check(path);
    int n = result ? result : fs_size(path);
    if (n >= 0 && ((unsigned int)n > capacity || fs_read(path, 0, buffer, n) != n)) n = -5;
    permission_internal--;
    return n;
}
int fs_file_size(const char *path) {
    if(!check_permission(path,'r')&&!check_permission(path,'w')&&!check_permission(path,'x'))return -13;
    permission_internal++;int n=fs_size(path);permission_internal--;return n;
}
int fs_pwrite(const char *path,unsigned int position,const void *bytes,unsigned int count,int append) {
    if(!check_permission(path,'w'))return -13;
    static char buffer[FS_MAX_FILE_SIZE];
    permission_internal++;
    int size=fs_size(path),result=-5;
    if(size<0)goto done;
    if(append)position=size;
    if(position>sizeof(buffer)||count>sizeof(buffer)-position){result=-27;goto done;}
    unsigned int next=position+count;if(next<(unsigned int)size)next=size;
    memset(buffer,0,next);
    if(fs_read(path,0,buffer,size)!=size)goto done;
    memcpy(buffer+position,bytes,count);
    result=fs_write(path,buffer,next)?-5:(int)(position+count);
done:
    permission_internal--;return result;
}
/* Stage guarded ACLs for both names before a rename, then remove the old
 * records after success. Also check protected descendants of directories. */
static char renamed_acl[8192], rename_guard[8192];
static int renamed_acl_needed;
int permissions_move_prepare(const char *source,const char *dest) {
    char from[PATH_CAP],to[PATH_CAP],data[8192];
    namespace_start();renamed_acl_needed=0;
    if(normalize(source,from)||normalize(dest,to))return -1;
    int n=private_read("/etc/access.cfg",data,sizeof(data));if(n<0)return 0;
    strcpy(rename_guard,data);int guard_size=n;
    int used=0;
    for(char *p=data;*p;) {
        char *start=p;while(*p&&*p!='|'&&*p!='\n')p++;
        if(*p!='|')return -1;
        *p++=0;char *fields=p;while(*p&&*p!='\n')p++;int size=p-fields;
        if(*p)*p++=0;
        char moved[PATH_CAP];const char *name=start;
        if(beneath(start,from)) {
            if(!check_permission(start,'w'))return -13;
            if(strlen(to)+strlen(start+strlen(from))>=PATH_CAP)return -1;
            strcpy(moved,to);strcat(moved,start+strlen(from));name=moved;renamed_acl_needed=1;
            if(guard_size+strlen(name)+size+3>=(int)sizeof(rename_guard))return -1;
            strcpy(rename_guard+guard_size,name);guard_size+=strlen(name);rename_guard[guard_size++]='|';
            memcpy(rename_guard+guard_size,fields,size);guard_size+=size;rename_guard[guard_size++]='\n';rename_guard[guard_size]=0;
        }
        if(used+strlen(name)+size+3>=(int)sizeof(renamed_acl))return -1;
        strcpy(renamed_acl+used,name);used+=strlen(name);renamed_acl[used++]='|';
        memcpy(renamed_acl+used,fields,size);used+=size;renamed_acl[used++]='\n';
    }
    renamed_acl[used]=0;return 0;
}
int permissions_move_commit(void) {
    if(!renamed_acl_needed)return 0;
    permission_internal++;
    /* Protect both old and new names before moving any directory entry. If
     * the rename fails, the extra ACL is restrictive rather than a bypass. */
    int result=fs_write("/etc/access.cfg",rename_guard,strlen(rename_guard));
    permission_internal--;return result;
}
void permissions_move_finish(void) {
    if(!renamed_acl_needed)return;
    permission_internal++;
    fs_write("/etc/access.cfg",renamed_acl,strlen(renamed_acl));
    permission_internal--;renamed_acl_needed=0;
}
unsigned int permissions_uid(void) {
    if(permissions_root())return 0;
    char data[4096];if(private_read("/etc/userpasswd.cfg",data,sizeof(data))<0)return 65534;
    unsigned int uid=1000;
    for(char *p=data;*p;) {
        char *name=p;while(*p&&*p!=':'&&*p!='\n')p++;
        if(*p!=':')return 65534;
        *p++=0;
        if(!strcmp(name,current_user))return uid;
        if(strcmp(name,"root"))uid++;
        while(*p&&*p!='\n')p++;
        if(*p)p++;
    }
    return 65534;
}
