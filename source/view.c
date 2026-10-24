#include "view.h"
#include "ui.h"
#include "pace.h"
#include <stdio.h>
#include <string.h>

static void button(int x,int y,int w,const char *label,bool active) {
    ui_roundrect(x,y,w,56,14,active?ui_palette.ink:ui_palette.soft);
    ui_text(x+20,y+14,24,active?ui_palette.canvas:ui_palette.ink,label,w-40);
}
static void chip(int x,int y,int w,const char *label) {
    ui_roundrect(x,y,w,40,12,ui_palette.soft);
    ui_text(x+12,y+9,20,ui_palette.ink,label,w-24);
}
static void caps_chip(const View *v,int x,int y,int width) {
    char label[64];
    const char *state=v->caps_pending?"...":v->caps_state<0?"unknown":v->caps_state?"on":"off";
    snprintf(label,sizeof(label),"X  Caps Lock: %s",state);
    chip(x,y,width,label);
}
void view_render(const View *v,uint32_t *pixels,int stride) {
    ui_begin(pixels,stride,v->dark); Palette p=ui_palette;
    ui_text(48,28,32,p.ink,"Plume USB",340);
    ui_text(48,68,20,p.muted,"Version 0.1.7",470);
    ui_roundrect(766,32,466,56,16,p.soft);
    ui_roundrect(786,53,12,12,6,v->usb_ready?p.positive:v->usb_initialized?p.accent:p.negative);
    ui_text(812,47,24,p.ink,v->usb_label,400);
    ui_line(48,108,1184,p.line);
    char buffer[256];
    if(v->screen==SCREEN_HOME) {
        ui_text(48,144,48,p.ink,"Send text",850);
        ui_text(50,207,24,p.muted,"Select a test or a text file.",850);
        for(int i=0;i<2;i++) {
            int y=279+i*128;bool active=v->selected==i;
            ui_roundrect(48,y,726,110,20,active?p.tint:p.surface);
            if(active) ui_roundrect(48,y+24,5,62,2,p.accent);
            ui_text(76,y+18,32,p.ink,i==0?"Send Hello":"Select .txt file",640);
            ui_text(76,y+65,20,p.muted,i==0?"Types Hello without pressing Enter.":"Browse the SD card.",640);
        }
        ui_roundrect(808,279,424,238,20,p.soft);
        ui_text(834,302,24,p.ink,"Connection",360);
        ui_wrap(834,348,360,20,4,p.muted,"Connect this Switch to the target device by USB. Open a text field on the target.");
        ui_text(834,471,20,p.ink,"Sending requires confirmation.",360);
    } else if(v->screen==SCREEN_FILES) {
        ui_text(48,132,32,p.ink,"Select a file",900);
        ui_text(48,176,20,p.muted,v->path,1170);
        for(int i=0;i<v->file_rows;i++) {
            int y=218+i*44;bool active=v->selected==i;
            if(active) ui_roundrect(48,y,1184,42,10,p.tint);
            ui_text(64,y+10,20,active?p.ink:p.muted,v->directories[i]?"FOLDER":"TXT",110);
            ui_text(198,y+8,24,p.ink,v->files[i],1000);
            if(active) ui_rect(48,y+8,4,26,p.accent);
        }
        if(!v->file_rows) ui_wrap(52,268,1000,24,3,p.muted,"No .txt files in this folder. Press B to go back.");
        snprintf(buffer,sizeof(buffer),"%zu items  /  A open or select  /  L, R change page",v->file_count);
        ui_text(48,584,20,p.muted,buffer,1184);
    } else if(v->screen==SCREEN_CONFIRM) {
        ui_text(48,134,32,p.ink,v->name,1150);
        snprintf(buffer,sizeof(buffer),"%zu characters   /   %zu line breaks   /   %zu tabs",v->characters,v->lines,v->tabs);
        ui_text(48,182,20,p.muted,buffer,1184);
        ui_roundrect(48,230,1184,254,20,p.surface);
        ui_wrap(72,251,1136,24,6,p.ink,v->preview);
        if(v->lines || v->tabs) ui_text(48,507,20,p.muted,"Line breaks press Enter. Tabs press Tab.",1184);
        else ui_text(48,507,20,p.muted,"No Enter key is added at the end.",1184);
        ui_text(48,540,20,p.muted,"The keyboard layout must match the target device.",1184);
        button(48,580,375,v->usb_ready?"A  Send (3 s delay)":"USB not ready",v->usb_ready);
        button(441,580,235,"B  Back",false);
        snprintf(buffer,sizeof(buffer),"ZR  Delay: %s",pace_label(v->pace));
        chip(722,588,350,buffer);
        caps_chip(v,48,658,248);
        snprintf(buffer,sizeof(buffer),"L  Auto-indent: %s",v->cancel_autoindent?"on":"off");
        chip(308,658,308,buffer);
        chip(628,658,196,"-  Diagnostics");
        snprintf(buffer,sizeof(buffer),"R  Enter pause: %d ms",v->enter_pause_ms);
        chip(836,658,396,buffer);
    } else if(v->screen==SCREEN_TRANSFER) {
        ui_text(48,159,48,p.ink,v->sending?"Sending":"Starting",1150);
        ui_text(50,229,24,p.muted,v->name,1150);
        snprintf(buffer,sizeof(buffer),"Delay: %s  /  Enter pause: %d ms",pace_label(v->pace),v->enter_pause_ms);
        ui_text(50,271,20,p.muted,buffer,1150);
        if(!v->sending) {
            snprintf(buffer,sizeof(buffer),"%d",v->seconds);
            ui_text(52,311,48,p.ink,buffer,200);
            ui_text(150,327,24,p.muted,"Select a text field on the target device.",1000);
        } else {
            snprintf(buffer,sizeof(buffer),"%zu / %zu keystrokes",v->done,v->total);
            ui_text(48,320,32,p.ink,buffer,1100);
            ui_roundrect(48,388,1184,16,8,p.soft);
            if(v->total) ui_roundrect(48,388,(int)(1184*v->done/v->total),16,8,p.accent);
            ui_text(48,437,24,p.muted,"Press B to stop.",1150);
        }
        button(48,557,300,"B  Cancel",false);
    } else if(v->screen==SCREEN_RESULT) {
        ui_text(48,159,48,v->error?p.negative:p.ink,v->error?"Error":"Finished",1150);
        ui_wrap(48,247,1100,24,6,p.ink,v->message);
        button(48,545,326,"A  Back to menu",true);
        button(392,545,300,"-  USB diagnostics",false);
    } else {
        ui_text(48,130,32,p.ink,"USB diagnostics",1180);
        for(int i=0;i<12;i++) ui_text(48,181+i*31,20,i==0&&v->error?p.negative:p.ink,v->diagnostics[i],1184);
        ui_text(48,581,20,p.muted,"Log: sdmc:/switch/plume-usb/usb.log",1184);
        button(48,617,220,"B  Back",false);
        button(286,617,300,"A  Reset USB",false);
    }
    if(v->screen==SCREEN_HOME || v->screen==SCREEN_FILES || v->screen==SCREEN_RESULT) {
        ui_line(48,637,1184,p.line);
        snprintf(buffer,sizeof(buffer),"Y  %s",v->layout==LAYOUT_FR?"AZERTY FR":"QWERTY US");chip(48,658,196,buffer);
        snprintf(buffer,sizeof(buffer),"ZR  %s",pace_label(v->pace));chip(256,658,164,buffer);
        chip(432,658,148,v->dark?"ZL  Light":"ZL  Dark");
        caps_chip(v,592,658,250);
        chip(854,658,214,"-  USB diagnostics");chip(1080,658,152,"+  Exit");
    }
}
Action view_touch(const View *v,int x,int y) {
    if(v->screen==SCREEN_CONFIRM && y>=658 && y<698) {
        if(x>=48 && x<296) return ACTION_CAPS_LOCK;
        if(x>=308 && x<616) return ACTION_AUTOINDENT;
        if(x>=628 && x<824) return ACTION_DIAGNOSTICS;
        if(x>=836 && x<1232) return ACTION_ENTER_PAUSE;
    }
    if(v->screen==SCREEN_HOME) {
        if(x>=48 && x<774 && y>=279 && y<389) return ACTION_HELLO;
        if(x>=48 && x<774 && y>=407 && y<517) return ACTION_FILES;
    }
    if(v->screen==SCREEN_FILES && x>=48 && x<1232 && y>=218 && y<570) {
        int row=(y-218)/44;if(row<v->file_rows) return (Action)(ACTION_ROW0+row);
    }
    if(v->screen==SCREEN_CONFIRM && y>=580 && y<636) {
        if(x>=48 && x<423) return ACTION_SEND;
        if(x>=441 && x<676) return ACTION_BACK;
        if(x>=722 && x<1072) return ACTION_PACE;
    }
    if(v->screen==SCREEN_TRANSFER && x>=48 && x<348 && y>=557 && y<613) return ACTION_ABORT;
    if(v->screen==SCREEN_RESULT && y>=545 && y<601) {
        if(x>=48 && x<374) return ACTION_BACK;
        if(x>=392 && x<692) return ACTION_DIAGNOSTICS;
    }
    if(v->screen==SCREEN_DIAGNOSTICS && y>=617 && y<673) {
        if(x>=48 && x<268) return ACTION_BACK;
        if(x>=286 && x<586) return ACTION_SEND;
    }
    if(v->screen==SCREEN_HOME || v->screen==SCREEN_FILES || v->screen==SCREEN_RESULT) {
        if(y>=658 && y<698) {
            if(x>=48 && x<244) return ACTION_LAYOUT;
            if(x>=256 && x<420) return ACTION_PACE;
            if(x>=432 && x<580) return ACTION_THEME;
            if(x>=592 && x<842) return ACTION_CAPS_LOCK;
            if(x>=854 && x<1068) return ACTION_DIAGNOSTICS;
            if(x>=1080 && x<1232) return ACTION_QUIT;
        }
    }
    return ACTION_NONE;
}
