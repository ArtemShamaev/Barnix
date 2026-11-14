#ifndef BARNIX_GAME_H
#define BARNIX_GAME_H
#include "command.h"
#include "../include/abi/keyboard.h"
static void cell(int x,int y,char ch,int color) {
    char text[2]={ch,0};barnix->goto_xy(x,y);barnix->print(color,text);
}
static void game_text(int x,int y,const char *text) { barnix->goto_xy(x,y);barnix->print(15,text); }
static void game_number(int x,int y,int n) {
    char out[12],rev[12];int k=0,i=0;
    do{rev[k++]='0'+n%10;n/=10;}while(n);while(k)out[i++]=rev[--k];out[i]=0;game_text(x,y,out);
}
/* Arcade sounds: CC0 clips by Kenney (sounds/LICENSE.txt) on a sound card,
 * a short beep on the PC speaker. */
#include <barnix_sound.h>
#include "arcade_sounds.h"
static void game_sound(int clip,unsigned int speaker_hz) {
    static short pcm[ARC_TOTAL_FRAMES],*start[ARC_COUNT];
    static int kind=-1;
    if(kind<0) {
        kind=bsnd_device(0,0);
        short *at=pcm;
        if(kind==BARNIX_SOUND_CARD)for(int i=0;i<ARC_COUNT;i++){start[i]=at;bsnd_ulaw(arc_data[i],arc_frames[i],at);at+=arc_frames[i];}
    }
    if(kind==BARNIX_SOUND_CARD)bsnd_play(start[clip],arc_frames[clip],22050,200,0);
    else if(kind==BARNIX_SOUND_SPEAKER)bsnd_beep(speaker_hz,clip==ARC_DIE?250:40);
}
#endif
