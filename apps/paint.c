#include "command.h"
#include "../keyboard.h"
#define WIDTH 78
#define HEIGHT 36
#define STRIDE ((WIDTH*3+3)&~3)
#define BMP_SIZE (54+STRIDE*HEIGHT)
static unsigned char pixels[WIDTH*HEIGHT],backup[WIDTH*HEIGHT],bitmap[BMP_SIZE];
static unsigned short screen[2000];
static char filename[256];
static int color=0,tool,brush=1,dirty,have_backup;
static const unsigned char palette[16][3]={
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}};
static void copy(unsigned char *to,const unsigned char *from,int n){for(int i=0;i<n;i++)to[i]=from[i];}
static void text(int x,int y,int attr,const char *s){while(*s&&x<80)screen[y*80+x++]=(attr<<8)|(unsigned char)*s++;}
static void snapshot(void){copy(backup,pixels,sizeof(pixels));have_backup=1;}
static void undo(void){if(have_backup)for(int i=0;i<WIDTH*HEIGHT;i++){unsigned char c=pixels[i];pixels[i]=backup[i];backup[i]=c;}dirty=1;}
static void dot(int x,int y,int ink){for(int j=y-brush/2;j<=y+brush/2;j++)for(int i=x-brush/2;i<=x+brush/2;i++)if(i>=0&&i<WIDTH&&j>=0&&j<HEIGHT)pixels[j*WIDTH+i]=ink;dirty=1;}
static int absint(int a){return a<0?-a:a;}
static void line(int x,int y,int endx,int endy,int ink){
    int dx=absint(endx-x),dy=-absint(endy-y),sx=x<endx?1:-1,sy=y<endy?1:-1,error=dx+dy;
    for(;;){dot(x,y,ink);if(x==endx&&y==endy)break;int twice=2*error;if(twice>=dy){error+=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}}
}
static void fill(int x,int y,int ink){
    unsigned char old=pixels[y*WIDTH+x];if(old==ink)return;
    static unsigned short queue[WIDTH*HEIGHT];int head=0,tail=0;queue[tail++]=y*WIDTH+x;pixels[y*WIDTH+x]=ink;
    while(head<tail){int p=queue[head++],neighbors[4]={p-1,p+1,p-WIDTH,p+WIDTH};
        for(int i=0;i<4;i++){int n=neighbors[i];if(n<0||n>=WIDTH*HEIGHT||(i==0&&p%WIDTH==0)||(i==1&&p%WIDTH==WIDTH-1))continue;
            if(pixels[n]==old){pixels[n]=ink;queue[tail++]=n;}}}
    dirty=1;
}
static void put32(int offset,unsigned int value){for(int i=0;i<4;i++)bitmap[offset+i]=value>>(8*i);}
static unsigned int get32(int offset){return bitmap[offset]|((unsigned int)bitmap[offset+1]<<8)|((unsigned int)bitmap[offset+2]<<16)|((unsigned int)bitmap[offset+3]<<24);}
static int save(void){
    for(int i=0;i<BMP_SIZE;i++)bitmap[i]=0;
    bitmap[0]='B';bitmap[1]='M';put32(2,BMP_SIZE);put32(10,54);put32(14,40);put32(18,WIDTH);put32(22,HEIGHT);bitmap[26]=1;bitmap[28]=24;put32(34,STRIDE*HEIGHT);
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        int p=54+(HEIGHT-1-y)*STRIDE+x*3,c=pixels[y*WIDTH+x];
        bitmap[p]=palette[c][2];bitmap[p+1]=palette[c][1];bitmap[p+2]=palette[c][0];
    }
    if(barnix->write(filename,(const char *)bitmap,sizeof(bitmap)))return -1;
    dirty=0;return 0;
}
static int load(void){
    if(barnix->size(filename)!=BMP_SIZE||barnix->read(filename,0,bitmap,BMP_SIZE)!=BMP_SIZE)return -1;
    if(bitmap[0]!='B'||bitmap[1]!='M'||get32(10)!=54||get32(14)!=40||get32(18)!=WIDTH||get32(22)!=HEIGHT||bitmap[26]!=1||bitmap[27]||bitmap[28]!=24||bitmap[29]||get32(30))return -1;
    snapshot();
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        int p=54+(HEIGHT-1-y)*STRIDE+x*3,best=0,distance=200000;
        for(int i=0;i<16;i++){int r=bitmap[p+2]-palette[i][0],g=bitmap[p+1]-palette[i][1],b=bitmap[p]-palette[i][2];int d=r*r+g*g+b*b;if(d<distance){best=i;distance=d;}}
        pixels[y*WIDTH+x]=best;
    }
    dirty=0;return 0;
}
static void draw(int cx,int cy,int pending,const char *status,int available,int mouse_x,int mouse_y){
    for(int i=0;i<2000;i++)screen[i]=0x1720;
    text(0,0,0x1f,"[ New ] [ Load ][ Save ][ Undo ][ Brush ][ Erase ][ Fill ]              [Exit]");
    text(1,1,0x1f,tool==1?"ERASER":tool==2?"FILL":"BRUSH");
    text(12,1,0x1f,brush==1?"Size: 1":brush==3?"Size: 3":"Size: 5");
    text(25,1,0x1f,available?"Left: draw | Right: erase":"No mouse: arrows + Space draw");
    text(1,2,0x1f,"Ink");
    for(int i=0;i<16;i++){int x=5+i*4;for(int j=0;j<3;j++)screen[2*80+x+j]=((i*17)<<8)|' ';if(i==color){screen[2*80+x]=(i<<12)|((i<8?15:0)<<8)|'[';screen[2*80+x+2]=(i<<12)|((i<8?15:0)<<8)|']';}}
    for(int y=0;y<HEIGHT/2;y++)for(int x=0;x<WIDTH;x++)screen[(y+4)*80+x+1]=(pixels[(y*2)*WIDTH+x]<<8)|(pixels[(y*2+1)*WIDTH+x]<<12)|0xdf;
    if(cx>=0&&cx<WIDTH&&cy>=0&&cy<HEIGHT)screen[(cy/2+4)*80+cx+1]=0x0f2b;
    text(1,22,0x1f,"Tab: ink | 1/3/5: size | B/E/F tools | U undo | S save | L load | Q exit");
    text(1,23,0x1e,pending?"Unsaved changes: Y / click same action to discard; N / Esc to cancel":status);
    text(1,24,0x1f,dirty?"*":" ");text(3,24,0x1f,filename);
    if(available && (cx<0||cx>=WIDTH||cy<0||cy>=HEIGHT))screen[(mouse_y/16)*80+mouse_x/8]^=0x7700;
    barnix->screen(screen);
}
static int clicked_action(int x){if(x<7)return 'n';if(x<15)return 'l';if(x<23)return 's';if(x<31)return 'u';if(x<40)return 'b';if(x<49)return 'e';if(x<58)return 'f';if(x>=69)return 'q';return 0;}
int main(int argc,const char *const *argv){
    if(argc>2)return fail("paint [FILE.bmp] - 78x36 pixel canvas; defaults to /home/USER/picture.bmp");
    if(argc==2){if(strlen(argv[1])>=sizeof(filename))return fail("Filename too long");for(unsigned int i=0;i<=strlen(argv[1]);i++)filename[i]=argv[1][i];}
    else {const char *user=barnix->current_user();int n=0;const char *parts[]={"/home/",user,"/picture.bmp"};for(int j=0;j<3;j++)for(const char *p=parts[j];*p;p++)filename[n++]=*p;filename[n]=0;}
    for(int i=0;i<WIDTH*HEIGHT;i++)pixels[i]=15;
    const char *status="Paint: choose a color, then draw. Saves standard 24-bit BMP.";
    if(barnix->size(filename)>=0)status=load()?"Cannot load: expected an uncompressed 78x36 BMP.":"Picture loaded.";
    int cx=WIDTH/2,cy=HEIGHT/2,lastx=-1,lasty=-1,pending=0;
    BarnixMouse mouse;barnix->mouse(&mouse);unsigned int sequence=mouse.sequence,previous=mouse.buttons;
    if(mouse.available){cx=mouse.x/8-1;cy=mouse.y<64?-1:(mouse.y-64)/8;}
    for(;;){
        unsigned int key=barnix->poll_key();barnix->mouse(&mouse);
        int x=mouse.x/8,y=mouse.y/16,click=(mouse.buttons&1)&&!(previous&1);
        int command=key>='A'&&key<='Z'?key+32:key;
        if(mouse.sequence!=sequence){cx=x-1;cy=(mouse.y-64)/8;if(mouse.y<64)cy=-1;sequence=mouse.sequence;}
        if(click&&y==0)command=clicked_action(x);
        if(pending){
            if(command=='y'||command==pending){command=pending;pending=0;dirty=0;}
            else if(command=='n'||command==KEY_ESCAPE){pending=0;command=0;}
            else if(command!='s')command=0;
        }
        if(!pending){
            if(command=='q'||command==KEY_ESCAPE){if(dirty)pending='q';else break;}
            if(command=='n'){if(dirty)pending='n';else{snapshot();for(int i=0;i<WIDTH*HEIGHT;i++)pixels[i]=15;dirty=1;status="New canvas.";}}
            if(command=='l'){if(dirty)pending='l';else status=load()?"Load failed: file missing, denied, or incompatible BMP.":"Picture loaded.";}
            if(command=='s'){status=save()?"Save failed: check path, permissions and free space.":"Picture saved.";pending=0;}
            if(command=='u')undo();
            if(command=='b')tool=0;
            if(command=='e')tool=1;
            if(command=='f')tool=2;
            if(command=='1'||command=='3'||command=='5')brush=command-'0';
            if(command=='\t')color=(color+1)%16;
            if(click&&y==2&&x>=5&&x<69)color=(x-5)/4;
            if(key==KEY_LEFT&&cx>0)cx--;
            if(key==KEY_RIGHT&&cx<WIDTH-1)cx++;
            if(key==KEY_UP&&cy>0)cy--;
            if(key==KEY_DOWN&&cy<HEIGHT-1)cy++;
            int drawing=(mouse.buttons&3)||key==' ';
            if(drawing&&cx>=0&&cx<WIDTH&&cy>=0&&cy<HEIGHT){
                int ink=(tool==1||(mouse.buttons&2))?15:color;
                if(lastx<0||key==' '){snapshot();if(tool==2)fill(cx,cy,ink);else dot(cx,cy,ink);}
                else if(tool!=2)line(lastx,lasty,cx,cy,ink);
                lastx=cx;lasty=cy;
            } else lastx=lasty=-1;
        }
        previous=mouse.buttons;draw(cx,cy,pending,status,mouse.available,mouse.x,mouse.y);barnix->sleep_ms(10);
    }
    barnix->clear();return 0;
}
