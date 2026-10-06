#ifndef BARNIX_GAME_H
#define BARNIX_GAME_H
#include "command.h"
#include "../keyboard.h"
static void cell(int x,int y,char ch,int color) {
    char text[2]={ch,0};barnix->goto_xy(x,y);barnix->print(color,text);
}
static void game_text(int x,int y,const char *text) { barnix->goto_xy(x,y);barnix->print(15,text); }
static void game_number(int x,int y,int n) {
    char out[12],rev[12];int k=0,i=0;
    do{rev[k++]='0'+n%10;n/=10;}while(n);while(k)out[i++]=rev[--k];out[i]=0;game_text(x,y,out);
}
#endif
