#include "game.h"
#define W 38
#define H 18
typedef struct { int x,y; } Point;
static Point body[W*H],food;
static int length,dx,dy,score,paused,done;
static unsigned int random_state=42;
static unsigned int random_next(void){random_state=random_state*1664525U+1013904223U;return random_state;}
static void place_food(void) {
    if(length==W*H){done=2;return;}
    int free=(int)(random_next()%(W*H-length));
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
        int occupied=0;for(int i=0;i<length;i++)if(body[i].x==x&&body[i].y==y){occupied=1;break;}
        if(!occupied && free--==0){food=(Point){x,y};return;}
    }
}
static void step(void) {
    Point head={body[0].x+dx,body[0].y+dy};
    if(head.x<0||head.x>=W||head.y<0||head.y>=H){done=1;return;}
    int eat=head.x==food.x&&head.y==food.y;
    for(int i=0;i<length-!eat;i++)if(body[i].x==head.x&&body[i].y==head.y){done=1;return;}
    if(eat){length++;score++;}
    else cell(body[length-1].x+21,body[length-1].y+3,' ',15);
    cell(body[0].x+21,body[0].y+3,'o',10);
    for(int i=length-1;i>0;i--)body[i]=body[i-1];
    body[0]=head;
    if(eat)place_food();
}
static void draw(void) {
    game_text(20,0,"Snake  Arrows/WASD  P pause  Q exit");
    game_text(20,1,"Score: ");game_number(27,1,score);
    for(int x=20;x<=59;x++){cell(x,2,'#',7);cell(x,21,'#',7);}
    for(int y=3;y<21;y++){cell(20,y,'#',7);cell(59,y,'#',7);}
    cell(food.x+21,food.y+3,'*',12);cell(body[0].x+21,body[0].y+3,'@',10);
    game_text(20,23,paused?"PAUSED":"      ");
}
int main(int argc,const char *const *argv) {
    (void)argc;(void)argv;length=3;dx=1;dy=0;score=paused=done=0;
    for(int i=0;i<length;i++)body[i]=(Point){10-i,9};
    place_food();barnix->clear();
    for(int i=0;i<length;i++)cell(body[i].x+21,body[i].y+3,'o',10);
    int ticks=0,turned=0;
    while(!done) {
        unsigned int key=barnix->poll_key();
        if(key=='q'||key=='Q'||key==KEY_ESCAPE){barnix->clear();return 0;}
        if(key=='p'||key=='P')paused=!paused;
        int nx=dx,ny=dy;
        if(key==KEY_UP||key=='w'||key=='W'){nx=0;ny=-1;}
        if(key==KEY_DOWN||key=='s'||key=='S'){nx=0;ny=1;}
        if(key==KEY_LEFT||key=='a'||key=='A'){nx=-1;ny=0;}
        if(key==KEY_RIGHT||key=='d'||key=='D'){nx=1;ny=0;}
        if(!turned&&!paused&&(nx!=dx||ny!=dy)&&!(nx==-dx&&ny==-dy)){dx=nx;dy=ny;turned=1;}
        if(!paused && ++ticks>=12){step();ticks=0;turned=0;}
        draw();barnix->sleep_ms(10);
    }
    game_text(20,23,done==2?"You win! Any key to exit.":"Game over. Any key to exit.");
    barnix->getch();barnix->clear();return 0;
}
