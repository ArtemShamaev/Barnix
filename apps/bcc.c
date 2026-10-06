/* BCC: the first self-hosted Barnix C compiler.
 *
 * This deliberately small compiler handles the useful bootstrap language:
 * one main function containing puts("text"), printf("text") and return N.
 * It emits a validated native ELF from a built-in freestanding ELF template,
 * so it needs no GCC, assembler, heap or host operating system at runtime.
 */
#include "command.h"
#include "bcc_template.inc"

#define SOURCE_MAX 32768
#define OUTPUT_MAX 65536
#define MESSAGE_MAX 128
#define MARKER "BCC_OUTPUT_MESSAGE_______________________________________________"

static char source[SOURCE_MAX], output[OUTPUT_MAX], message[MESSAGE_MAX];

static unsigned int length(const char *text) { unsigned int n=0;while(text[n])n++;return n; }
static const char *space(const char *text) { while(*text==' '||*text=='\t'||*text=='\r'||*text=='\n')text++;return text; }
static void copy(char *to,const char *from,unsigned int capacity) { unsigned int i=0;while(i+1<capacity&&from[i]){to[i]=from[i];i++;}to[i]=0; }
static char *find(char *text,const char *needle) {
    unsigned int n=length(needle);if(!n)return text;
    for(unsigned int i=0;text[i];i++){unsigned int j=0;while(j<n&&text[i+j]==needle[j])j++;if(j==n)return text+i;}return 0;
}
static unsigned char *find_bytes(unsigned char *data,unsigned int size,const char *needle) {
    unsigned int n=length(needle);if(!n||n>size)return 0;
    for(unsigned int i=0;i+n<=size;i++){unsigned int j=0;while(j<n&&data[i+j]==(unsigned char)needle[j])j++;if(j==n)return data+i;}return 0;
}
static char *find_call(char *text,const char *name) {
    unsigned int n=length(name);
    for(char *p=find(text,name);p;p=find(p+1,name)) {
        const char *after=space(p+n);
        if(*after=='(')return (char *)after;
    }
    return 0;
}
static int decode_string(const char *start) {
    unsigned int out=0;start++;
    while(*start&&*start!='"') {
        unsigned char c=(unsigned char)*start++;
        if(c=='\\') { c=(unsigned char)*start++; if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='"'&&c!='\\')return -1; }
        if(out+1>=sizeof(message))return -1;message[out++]=(char)c;
    }
    if(*start!='"')return -1;message[out]=0;return (int)out;
}
static int has_main(const char *text) {
    const char *p=find((char *)text,"int main");
    if(!p)return 0;
    p=space(p+8);
    if(*p!='(')return 0;
    while(*p&&*p!=')')p++;
    return *p==')'&&*space(p+1)=='{';
}
static const char *statement_end(const char *text) {
    text=space(text);
    while(*text&&*text!=';') {
        if(*text=='\n')return 0;
        text++;
    }
    return *text==';'?text:0;
}
static int output_name(const char *input,char *name) {
    const char *slash=input;for(const char *p=input;*p;p++)if(*p=='/')slash=p+1;
    copy(name,slash,64);char *dot=name;for(char *p=name;*p;p++)if(*p=='.')dot=p;
    if(dot==name)dot=name+length(name);copy(dot,".elf",64-(unsigned int)(dot-name));return length(name)<24?0:-1;
}
int main(int argc,const char *const *argv) {
    if(argc<2||argc>3)return fail("usage: bcc SOURCE.c [OUTPUT.elf]");
    int size=barnix->read(argv[1],0,source,sizeof(source)-1);if(size<0)return fail("bcc: cannot read source");source[size]=0;
    if(!has_main(source))return fail("bcc: expected int main(...) { ... }");
    char *call=find_call(source,"puts");if(!call)call=find_call(source,"printf");
    if(!call)return fail("bcc: supported source is puts(\"text\") or printf(\"text\") in main");
    while(*call&&*call!='"')call++;if(*call!='"'||decode_string(call)<0)return fail("bcc: expected one string literal");
    const char *after=call+1;while(*after&&*after!='"')after++;if(*after=='"')after++;
    after=space(after);if(*after!=')')return fail("bcc: expected ')' after string literal");
    if(*space(after+1)!=';')return fail("bcc: expected ';' after output call");
    if(find(message,"%"))return fail("bcc: format arguments are not supported yet");
    const char *ret=find(source,"return");
    if(ret&&!statement_end(ret+6))return fail("bcc: expected ';' after return");
    char name[64];if(argc==3)copy(name,argv[2],sizeof(name));else if(output_name(argv[1],name))return fail("bcc: output name is too long");
    if(!length(name)||length(name)>23)return fail("bcc: output must be a Barnix ELF filename of 23 characters or fewer");
    for(unsigned int i=0;i<length(name);i++)if(name[i]=='/')return fail("bcc: output must be a filename, not a path");
    unsigned int total=bcc_template_size;if(total>sizeof(output))return fail("bcc: built-in template is too large");
    for(unsigned int i=0;i<total;i++)output[i]=bcc_template[i];
    unsigned char *embedded=find_bytes((unsigned char *)output,total,MARKER);if(!embedded||length(message)>length(MARKER))return fail("bcc: built-in template is invalid");
    for(unsigned int i=0;i<length(MARKER);i++)embedded[i]=i<length(message)?message[i]:0;
    int result=barnix->write(name,output,(int)total);if(result)return fail("bcc: cannot write output (check permissions and space)");
    char permission[64];copy(permission,barnix->current_user(),sizeof(permission));
    unsigned int user_length=length(permission);if(user_length+5>=sizeof(permission))return fail("bcc: username is too long");
    permission[user_length++]='=';permission[user_length++]='r';permission[user_length++]='w';permission[user_length++]='x';permission[user_length]=0;
    if(barnix->chmod(name,permission,""))return fail("bcc: output was written but could not be marked executable");
    barnix->println(10,"BCC: compiled native Barnix ELF");return 0;
}
