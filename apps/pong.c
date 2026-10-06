#include "game.h"
static int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int clamp(int p){return p<2?2:p>17?17:p;}
int main(int argc,const char *const *argv) {
    int two=argc==2&&same(argv[1],"2");
    if(argc>2||(argc==2&&!two&&!same(argv[1],"1")))return fail("pong [1|2]: default is one player against computer");
    int left=10,right=10,x=40,y=10,dx=1,dy=1,a=0,b=0,paused=0,ticks=0,quit=0;
    barnix->clear();
    game_text(2,0,two?"Pong: W/S left, arrows right | P pause, Q exit":"Pong vs computer: W/S or arrows | P pause, Q exit");
    for(int col=2;col<=77;col++){cell(col,1,'=',7);cell(col,21,'=',7);}
    while(a<5&&b<5&&!quit) {
        int oldleft=left,oldright=right,oldx=x,oldy=y;
        unsigned int key=barnix->poll_key();
        if(key=='q'||key=='Q'||key==KEY_ESCAPE){quit=1;break;}
        if(key=='p'||key=='P')paused=!paused;
        if(!paused) {
            if(key=='w'||key=='W'||(!two&&key==KEY_UP))left--;
            if(key=='s'||key=='S'||(!two&&key==KEY_DOWN))left++;
            if(two&&key==KEY_UP)right--;
            if(two&&key==KEY_DOWN)right++;
            left=clamp(left);right=clamp(right);
            if(++ticks>=6) {
                ticks=0;
                /* AI moves on alternating ball columns: beatable, no teleporting. */
                if(!two&&!(x&1)&&dx>0){if(y<right+1)right--;else if(y>right+2)right++;right=clamp(right);}
                int nx=x+dx,ny=y+dy;
                if(ny<2||ny>20){dy=-dy;ny=y+dy;}
                if(nx==4&&dx<0&&ny>=left&&ny<left+4){dx=1;nx=5;dy=ny<left+2?-1:1;}
                if(nx==75&&dx>0&&ny>=right&&ny<right+4){dx=-1;nx=74;dy=ny<right+2?-1:1;}
                if(nx<=2||nx>=77){if(nx<=2)b++;else a++;nx=40;ny=10;dx=-dx;dy=1;}
                x=nx;y=ny;
            }
        }
        cell(oldx,oldy,' ',15);
        for(int i=0;i<4;i++){cell(4,oldleft+i,' ',15);cell(75,oldright+i,' ',15);}
        for(int i=0;i<4;i++){cell(4,left+i,'|',11);cell(75,right+i,'|',11);}
        cell(x,y,'O',14);game_text(30,22,"Score: ");game_number(37,22,a);game_text(39,22,"-");game_number(41,22,b);
        game_text(30,23,paused?"PAUSED":"      ");barnix->sleep_ms(10);
    }
    if(!quit){game_text(25,23,a==5?"Player 1 wins! Any key.":two?"Player 2 wins! Any key.":"Computer wins! Any key.");barnix->getch();}
    barnix->clear();return 0;
}
