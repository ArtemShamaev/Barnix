#include "command.h"
static int prefix(const char *s,const char *p) { while(*p)if(*s++!=*p++)return 0; return 1; }
int main(int argc,const char *const *argv) {
    const char *spec=0,*file=0,*password="";
    for(int i=1;i<argc;i++) {
        if(prefix(argv[i],"file=")) { if(file)return fail("Duplicate file"); file=argv[i]+5; }
        else if(prefix(argv[i],"passwd="))password=argv[i]+7;
        else { if(spec)return fail("One USER=rights entry is required"); spec=argv[i]; }
    }
    if(!spec||!file||!*file)return fail("chmod USER=rwx [passwd=PASSWORD] file=/path");
    return result(barnix->chmod(file,spec,password));
}
