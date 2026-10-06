#include "lang.h"
#include "barnix.h"
#include "elf.h"
#include "linux_exec.h"
#include "linux_task.h"
#include "shell_parse.h"
#include "fs.h"
#include "keyboard.h"
#include "disk.h"

#define MAX_CMD SHELL_LINE_MAX
#include "permissions.h"
#include "settings.h"

_Static_assert(SHELL_STAGES_MAX + 1 <= LINUX_TASK_LIMIT, "pipeline runner needs a parent task slot");
static char cmd[MAX_CMD];
static ShellPlan plan;
static struct { char user[MAX_USERNAME], cwd[256]; } sessions[8];
static int session_depth;

static void secret_input(char *out, int capacity, const char *prompt) {
    print(CYAN,prompt);int n=0;
    for(;;) {
        unsigned int key=getch();
        if(key=='\n')break;
        if(key=='\b') { if(n)n--;continue; }
        if(key>=' ' && key<='~' && n<capacity-1)out[n++]=key;
    }
    out[n]=0;println(WHITE,"");
}
static void do_login(void) {
    char user[MAX_USERNAME],password[MAX_PASSWORD],confirm[MAX_PASSWORD];
    if(fs_size("/etc/userpasswd.cfg")<0) {
        println(YELLOW,"First start: create root and your personal account.");
        do {
            secret_input(password,sizeof(password),"New root password: ");
            secret_input(confirm,sizeof(confirm),"Repeat root password: ");
        } while(!*password || strcmp(password,confirm) || account_create("root",password));
        for(;;) {
            input(user,sizeof(user),"New username: ");
            secret_input(password,sizeof(password),"New user password: ");
            secret_input(confirm,sizeof(confirm),"Repeat user password: ");
            if(strcmp(user,"root") && !strcmp(password,confirm) && !account_create(user,password))break;
            println(RED,"Invalid or duplicate username, empty password, or passwords differ.");
        }
        fs_sync();
    }
    for(;;) {
        input(user,sizeof(user),"Login: ");
        secret_input(password,sizeof(password),"Password: ");
        if(!authenticate_user(user,password) && !switch_user(user,password))break;
        println(RED,"Login failed. Try again.");
    }
    memset(password,0,sizeof(password));memset(confirm,0,sizeof(confirm));
    input_history_clear();clear();settings_load();
    print(GREEN,"Welcome, ");println(GREEN,get_current_user());
}
extern const unsigned char bssh_runner_start[], bssh_runner_end[];

static void execute(int argc, const char *const *argv)
{
    if (plan.compound) {
        const char *args[] = {"bssh-run", cmd};
        int status;
        int error = linux_run_image(bssh_runner_start, bssh_runner_end - bssh_runner_start, 2, args, &status);
        console_finish_line();
        print(error ? RED : GREEN, error ? "Linux load error: " : tr("program exited: "));
        print_int(error ? RED : GREEN, error ? error : status);
        println(WHITE, "");
        return;
    }
    if(!strcmp(argv[0],"su")||!strcmp(argv[0],"sudo")||!strcmp(argv[0],"useradd")) {
        char path[48];strcpy(path,"/bin/");strcat(path,argv[0]);
        if(!check_permission(path,'x')){println(RED,"Permission denied");return;}
    }
    if (!strcmp(argv[0],"su")) {
        if(argc>2 || session_depth==8){println(RED,"su [USER] (maximum 8 nested sessions)");return;}
        strcpy(sessions[session_depth].user,get_current_user());
        fs_getcwd(sessions[session_depth].cwd,sizeof(sessions[session_depth].cwd));
        char password[MAX_PASSWORD]="";
        if(!permissions_root())secret_input(password,sizeof(password),"Password: ");
        if(switch_user(argc==2?argv[1]:"root",password))println(RED,"Authentication failed");
        else {session_depth++;input_history_clear();clear();settings_load();}
        memset(password,0,sizeof(password));return;
    }
    if (!strcmp(argv[0],"sudo")) {
        if(argc<2){println(RED,"sudo COMMAND [ARGS] (root password required)");return;}
        char password[MAX_PASSWORD]="",previous[MAX_USERNAME],cwd[256];
        int depth=session_depth;
        strcpy(previous,get_current_user());fs_getcwd(cwd,sizeof(cwd));
        if(!permissions_root()) {
            secret_input(password,sizeof(password),"[sudo] root password: ");
            if(authenticate_user("root",password)){memset(password,0,sizeof(password));println(RED,"Authentication failed");return;}
        }
        memset(password,0,sizeof(password));permissions_restore("root");
        execute(argc-1,argv+1);
        session_depth=depth;permissions_restore(previous);fs_cd(cwd);settings_load();return;
    }
    if (!strcmp(argv[0],"useradd")) {
        if(argc!=2 || !permissions_root()){println(RED,"useradd USER requires root");return;}
        char password[MAX_PASSWORD],confirm[MAX_PASSWORD];
        secret_input(password,sizeof(password),"New password: ");
        secret_input(confirm,sizeof(confirm),"Repeat password: ");
        if(strcmp(password,confirm) || account_create(argv[1],password))println(RED,"Cannot create user");
        else println(GREEN,"User created");
        memset(password,0,sizeof(password));memset(confirm,0,sizeof(confirm));return;
    }
    if (!strcmp(argv[0],"exit") || !strcmp(argv[0],"logout")) {
        if(!strcmp(argv[0],"exit") && session_depth) {
            session_depth--;permissions_restore(sessions[session_depth].user);
            fs_cd(sessions[session_depth].cwd);input_history_clear();clear();settings_load();
        } else {session_depth=0;input_history_clear();clear();permissions_restore("root");do_login();}
        return;
    }
    if (!strcmp(argv[0], "linux")) {
        if (argc < 2) { println(RED, "usage: linux /path/program [args]"); return; }
        int status;
        int error = linux_run(argv[1], argc - 1, (const char *const *)(argv + 1), &status);
        console_finish_line();
        print(error ? RED : GREEN, error ? "Linux load error: " : "Linux program exited: ");
        print_int(error ? RED : GREEN, error ? error : status);
        println(WHITE, "");
        return;
    }
    char path[MAX_CMD + 5];
    const char *name = argv[0];
    if (!strchr(name, '/')) {
        strcpy(path, "/bin/"); strcat(path, name); name = path;
    }
    int status;
    if (elf_run(name, argc, (const char *const *)argv, &status) == 0) {
        console_finish_line();
        print(GREEN, tr("program exited: ")); print_int(GREEN, status); println(GREEN, "");
    }
}

static void exec_cmd(void) {
    const char *error=shell_parse(cmd,&plan);
    if(error){print(RED,"bssh: ");println(RED,error);return;}
    if(plan.count)execute(plan.commands[0].argc,plan.commands[0].argv);
}

/* =========================================================
   main shell loop
   ========================================================= */

void bssh_main(void)
{
    // Initialize filesystem and users
    if (fs_init() != 0) {
        // Try with ram0
        if (disk_select("ram0") == 0) {
            fs_init();
        }
    }

    // Perform login
    do_login();

    banner(tr("Barnix Shell v0.3"));

    while (1)
    {
        print(CYAN, "bssh> ");

        input(cmd, MAX_CMD, "");

        exec_cmd();
    }
}
