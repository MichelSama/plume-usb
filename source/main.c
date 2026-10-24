#include <switch.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "text.h"
#include "ui.h"
#include "view.h"
#include "usb_keyboard.h"
#include "pace.h"
#include "typer.h"

/* Fits hbmenu's applet budget; full application mode is recommended. */
u32 __nx_heap_size=48*1024*1024;
typedef struct { char name[768]; bool directory; } Entry;
static View view;
static Framebuffer framebuffer;
static PadState pad;
static Entry *entries;
static size_t entry_count,browser_index;
static char directory[1024]="sdmc:/";
static char *loaded_text;
static size_t loaded_length;
static TextPlan plan;
static Typer sender;
static Typer caps_toggle;
static const Stroke caps_key={0,0x39};
static uint32_t caps_failure_base;
static uint32_t failure_base;
static bool quit_requested,abort_error;
static char abort_message[384];
static Screen previous_screen;

static uint64_t now_ms(void) { return armTicksToNs(armGetSystemTick())/1000000; }
static void save_settings(void) {
    FILE *file=fopen("sdmc:/switch/plume-usb/settings.txt","w");
    if(file) { fprintf(file,"dark=%d\nlayout=%d\npace=%d\nenter_pause_ms=%d\ncancel_autoindent=%d\n",view.dark,view.layout,view.pace,view.enter_pause_ms,view.cancel_autoindent);fclose(file); }
}
static void load_settings(void) {
    FILE *file=fopen("sdmc:/switch/plume-usb/settings.txt","r");
    if(!file) return;
    char line[80];int value;
    while(fgets(line,sizeof(line),file)) {
        if(sscanf(line,"dark=%d",&value)==1) view.dark=value!=0;
        if(sscanf(line,"layout=%d",&value)==1 && value>=0 && value<=1) view.layout=(KeyboardLayout)value;
        if(sscanf(line,"pace=%d",&value)==1 && value>=0 && value<PACE_COUNT) view.pace=value;
        if(sscanf(line,"enter_pause_ms=%d",&value)==1 && enter_pause_valid(value)) view.enter_pause_ms=value;
        if(sscanf(line,"cancel_autoindent=%d",&value)==1) view.cancel_autoindent=value!=0;
    }
    fclose(file);
}
static void message(bool error,const char *text) {
    view.error=error;snprintf(view.message,sizeof(view.message),"%s",text);view.screen=SCREEN_RESULT;
}
static int compare_entries(const void *a,const void *b) {
    const Entry *left=a,*right=b;
    if(strcmp(left->name,"..")==0) return -1;
    if(strcmp(right->name,"..")==0) return 1;
    if(left->directory!=right->directory) return left->directory?-1:1;
    return strcasecmp(left->name,right->name);
}
static bool join_path(char *out,size_t capacity,const char *base,const char *name) {
    size_t len=strlen(base);int n=snprintf(out,capacity,"%s%s%s",base,len&&base[len-1]=='/'?"":"/",name);
    return n>=0 && (size_t)n<capacity;
}
static bool browser_load(void) {
    free(entries);entries=NULL;entry_count=0;browser_index=0;
    DIR *dir=opendir(directory);
    if(!dir) { message(true,"Could not open the SD card folder.");return false; }
    size_t capacity=128;entries=calloc(capacity,sizeof(Entry));
    if(!entries) { closedir(dir);message(true,"Not enough memory to list this folder.");return false; }
    if(strcmp(directory,"sdmc:/")!=0) { strcpy(entries[0].name,"..");entries[0].directory=true;entry_count=1; }
    struct dirent *ent;
    while((ent=readdir(dir))!=NULL) {
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        char full[1024];struct stat st;
        if(strlen(ent->d_name)>=sizeof(entries[0].name) || !join_path(full,sizeof(full),directory,ent->d_name)) continue;
        if(stat(full,&st)!=0) continue;
        bool isdir=S_ISDIR(st.st_mode);const char *extension=strrchr(ent->d_name,'.');
        if(!isdir && (!S_ISREG(st.st_mode) || !extension || strcasecmp(extension,".txt")!=0)) continue;
        if(entry_count==8192) { keyboard_log("Browser limited to 8192 entries");break; }
        if(entry_count==capacity) {
            size_t next=capacity*2;Entry *expanded=realloc(entries,next*sizeof(Entry));
            if(!expanded) { closedir(dir);message(true,"Not enough memory to list this folder.");return false; }
            entries=expanded;capacity=next;
        }
        snprintf(entries[entry_count].name,sizeof(entries[entry_count].name),"%s",ent->d_name);
        entries[entry_count++].directory=isdir;
    }
    closedir(dir);qsort(entries,entry_count,sizeof(Entry),compare_entries);
    view.screen=SCREEN_FILES;return true;
}
static void browser_parent(void) {
    if(strcmp(directory,"sdmc:/")==0) { view.screen=SCREEN_HOME;return; }
    char *slash=strrchr(directory,'/');
    if(slash && slash>directory+5) *slash=0;else strcpy(directory,"sdmc:/");
    browser_load();
}
static bool prepare_text(void) {
    text_plan_free(&plan);TextError error;
    if(!text_plan(loaded_text,loaded_length,view.layout,view.cancel_autoindent,&plan,&error)) {
        char msg[384];
        if(error.invalid_utf8) snprintf(msg,sizeof(msg),"Invalid UTF-8 at byte %zu. Save the file as UTF-8 and retry. Nothing was sent.",error.byte_offset+1);
        else if(error.character) snprintf(msg,sizeof(msg),"Unsupported character U+%04lX in %s at position %zu. Nothing was sent.",
            (unsigned long)error.codepoint,view.layout==LAYOUT_FR?"AZERTY FR":"QWERTY US",error.character);
        else snprintf(msg,sizeof(msg),"Could not prepare the text in memory. Nothing was sent.");
        message(true,msg);return false;
    }
    if(!plan.count) { message(true,"The file is empty.");return false; }
    view.characters=plan.characters;view.lines=plan.lines;view.tabs=plan.tabs;
    size_t first=loaded_length>=3 && memcmp(loaded_text,"\xef\xbb\xbf",3)==0?3:0;
    size_t n=loaded_length-first;if(n>sizeof(view.preview)-1) n=sizeof(view.preview)-1;
    /* Do not cut a UTF-8 sequence at the end of the preview. */
    while(n && first+n<loaded_length && ((uint8_t)loaded_text[first+n]&0xc0)==0x80) n--;
    memcpy(view.preview,loaded_text+first,n);view.preview[n]=0;view.screen=SCREEN_CONFIRM;
    return true;
}
static void hello(void) {
    free(loaded_text);loaded_text=strdup("Hello");loaded_length=5;
    if(!loaded_text) { message(true,"Not enough memory.");return; }
    strcpy(view.name,"Hello test");prepare_text();
}
static void open_file(const char *path,const char *name) {
    FILE *file=fopen(path,"rb");
    if(!file) { message(true,"Could not open the file.");return; }
    if(fseek(file,0,SEEK_END)!=0) { fclose(file);message(true,"Could not determine the file size.");return; }
    long length=ftell(file);
    if(length<0 || length>262144) { fclose(file);message(true,"Unsupported file size. Maximum: 256 KiB.");return; }
    rewind(file);char *data=malloc((size_t)length+1);
    if(!data) { fclose(file);message(true,"Not enough memory to load the file.");return; }
    size_t n=fread(data,1,(size_t)length,file);bool read_error=ferror(file)!=0;fclose(file);
    if(read_error || n!=(size_t)length) { free(data);message(true,"Could not read the file.");return; }
    data[n]=0;free(loaded_text);loaded_text=data;loaded_length=n;
    snprintf(view.name,sizeof(view.name),"%.255s",name);prepare_text();
}
static void browser_open(void) {
    if(browser_index>=entry_count) return;
    const Entry *entry=&entries[browser_index];
    if(strcmp(entry->name,"..")==0) { browser_parent();return; }
    char full[1024];
    if(!join_path(full,sizeof(full),directory,entry->name)) { message(true,"The file path is too long.");return; }
    if(entry->directory) { snprintf(directory,sizeof(directory),"%s",full);browser_load(); }
    else open_file(full,entry->name);
}
static void abort_send(bool error,const char *reason,uint64_t now) {
    snprintf(abort_message,sizeof(abort_message),"%s",reason);abort_error=error;
    keyboard_log("SEND ABORT after %zu/%zu strokes: %s",sender.index,plan.count,reason);
    if(sender.phase!=TX_COUNTDOWN && keyboard_status()->ready) {
        keyboard_cancel_transfer();
        if(R_SUCCEEDED(keyboard_send(0,0,now))) { sender.phase=TX_ABORT;return; }
    }
    sender.phase=TX_NONE;appletSetMediaPlaybackState(false);message(error,reason);
}
static void caps_toggle_failed(uint64_t now) {
    keyboard_log("CAPS LOCK toggle failed");
    keyboard_cancel_transfer();
    if(keyboard_status()->ready && R_SUCCEEDED(keyboard_send(0,0,now))) {
        caps_toggle.phase=TX_ABORT;
    } else {
        caps_toggle.phase=TX_NONE;
        message(true,"Could not toggle Caps Lock. Check USB diagnostics with -.");
    }
}
static void caps_toggle_tick(uint64_t now) {
    const KeyboardStatus *usb=keyboard_status();
    if(caps_toggle.phase==TX_NONE) return;
    if(caps_toggle.phase==TX_ABORT) {
        if(!usb->transfer_pending || !usb->ready) {
            caps_toggle.phase=TX_NONE;
            message(true,"Could not toggle Caps Lock. Check USB diagnostics with -.");
        }
        return;
    }
    if(!usb->ready || usb->reports_failed!=caps_failure_base) { caps_toggle_failed(now);return; }
    Stroke next;
    if(typer_step(&caps_toggle,now,usb->transfer_pending,&next)) {
        if(R_FAILED(keyboard_send(next.modifier,next.key,now))) { caps_toggle_failed(now);return; }
    }
    if(caps_toggle.phase==TX_DONE) {
        caps_toggle.phase=TX_NONE;
        keyboard_log("CAPS LOCK key sent; host LEDs=%02x known=%u",usb->leds,usb->leds_known);
    }
}
static void begin_caps_toggle(uint64_t now) {
    const KeyboardStatus *usb=keyboard_status();
    if(sender.phase!=TX_NONE || caps_toggle.phase!=TX_NONE) return;
    if(!usb->ready) { message(true,"USB keyboard not ready. Check the connection or press - for diagnostics.");return; }
    keyboard_cancel_transfer();caps_failure_base=usb->reports_failed;
    typer_start(&caps_toggle,&caps_key,1,20,now);
    keyboard_log("CAPS LOCK toggle requested");
}
static void begin_send(uint64_t now) {
    const KeyboardStatus *usb=keyboard_status();
    if(caps_toggle.phase!=TX_NONE) return;
    if(!usb->ready) { message(true,"USB keyboard not ready. Check the connection or press - for diagnostics.");return; }
    if(!plan.count) return;
    keyboard_cancel_transfer();failure_base=usb->reports_failed;
    typer_start(&sender,plan.strokes,plan.count,pace_delay(view.pace),now+3000);view.screen=SCREEN_TRANSFER;
    sender.enter_pause_ms=(unsigned)view.enter_pause_ms;
    view.total=plan.count;view.done=0;view.sending=false;view.seconds=3;
    appletSetMediaPlaybackState(true);
    keyboard_log("SEND queued layout=%s chars=%zu strokes=%zu lines=%zu tabs=%zu pace=%d delay_ms=%u enter_pause_ms=%u autoindent=%d",
        view.layout==LAYOUT_FR?"FR":"US",plan.characters,plan.count,plan.lines,plan.tabs,view.pace,pace_delay(view.pace),sender.enter_pause_ms,view.cancel_autoindent);
}
static void sending_tick(uint64_t now) {
    const KeyboardStatus *usb=keyboard_status();
    if(sender.phase==TX_NONE) return;
    if(sender.phase==TX_ABORT) {
        if(!usb->transfer_pending || !usb->ready) { sender.phase=TX_NONE;appletSetMediaPlaybackState(false);message(abort_error,abort_message); }
        return;
    }
    if(!usb->ready) { abort_send(true,"USB disconnected. Sending stopped.",now);return; }
    if(usb->reports_failed!=failure_base) { abort_send(true,"USB transfer failed. Sending stopped. Press - for diagnostics.",now);return; }
    if(sender.phase==TX_COUNTDOWN) view.seconds=now<sender.due?(int)((sender.due-now+999)/1000):0;
    Stroke next;
    if(typer_step(&sender,now,usb->transfer_pending,&next)) {
        if(R_FAILED(keyboard_send(next.modifier,next.key,now))) { abort_send(true,"Could not send a keyboard report.",now);return; }
    }
    view.sending=sender.phase!=TX_COUNTDOWN;view.done=sender.index;
    if(sender.phase==TX_DONE) {
        sender.phase=TX_NONE;appletSetMediaPlaybackState(false);
        keyboard_log("SEND COMPLETE %zu strokes, %zu characters",plan.count,plan.characters);
        message(false,"USB transfer complete. Check the text on the target device.");
    }
}
static void update_view(void) {
    const KeyboardStatus *usb=keyboard_status();
    view.usb_initialized=usb->initialized;view.usb_ready=usb->ready;
    view.caps_state=usb->leds_known?((usb->leds&2)?1:0):-1;
    view.caps_pending=caps_toggle.phase!=TX_NONE;
    const char *states[]={"USB disconnected","USB attached","USB powered","USB connecting...","USB addressing...","USB keyboard connected","USB suspended"};
    snprintf(view.usb_label,sizeof(view.usb_label),"%s",usb->initialized?states[usb->state<=6?usb->state:0]:"USB unavailable");
    if(usb->initialized && usb->state==UsbState_Configured && !usb->ready)
        snprintf(view.usb_label,sizeof(view.usb_label),"Checking USB keyboard...");
    if(view.screen==SCREEN_FILES) {
        view.file_start=browser_index/8*8;view.selected=(int)(browser_index%8);view.file_count=entry_count;
        snprintf(view.path,sizeof(view.path),"%s",directory);view.file_rows=0;
        for(size_t i=view.file_start;i<entry_count && i<view.file_start+8;i++) {
            size_t row=i-view.file_start;
            snprintf(view.files[row],sizeof(view.files[row]),"%.255s",entries[i].name);
            view.directories[row]=entries[i].directory;view.file_rows++;
        }
    }
    if(view.screen==SCREEN_DIAGNOSTICS) {
        view.error=R_FAILED(usb->error);
        snprintf(view.diagnostics[0],144,"Last error: 0x%08lX  /  %s",(unsigned long)usb->error,usb->stage);
        snprintf(view.diagnostics[1],144,"USB configured: %s  /  keyboard ready: %s  /  state: %u  /  resets: %lu",usb->state==UsbState_Configured?"yes":"no",usb->ready?"yes":"no",usb->state,(unsigned long)usb->bus_resets);
        snprintf(view.diagnostics[2],144,"Requests: %lu  /  descriptor requests: %lu  /  acknowledged: %s",(unsigned long)usb->setup_count,(unsigned long)usb->report_requests,usb->descriptor_valid?"yes":"no");
        snprintf(view.diagnostics[3],144,"Last request: type %02X  /  request %02X  /  value %04X",usb->last_request_type,usb->last_request,usb->last_value);
        snprintf(view.diagnostics[4],144,"Input reports completed: %lu  /  errors: %lu  /  pending: %s",(unsigned long)usb->reports_completed,(unsigned long)usb->reports_failed,usb->transfer_pending?"yes":"no");
        snprintf(view.diagnostics[5],144,"Control IN/OUT: %lu/%lu  /  protocol: %s  /  idle: %u ms  /  LEDs: %02X",(unsigned long)usb->control_in_completed,(unsigned long)usb->control_out_completed,usb->protocol?"Report":"Boot",usb->idle*4,usb->leds);
        snprintf(view.diagnostics[6],144,"Layout: %s  /  delay: %u ms  /  Enter pause: %d ms",view.layout==LAYOUT_FR?"AZERTY FR":"QWERTY US",pace_delay(view.pace),view.enter_pause_ms);
        snprintf(view.diagnostics[7],144,"Horizon OS: %u.%u.%u  /  mode: %s",HOSVER_MAJOR(hosversionGet()),HOSVER_MINOR(hosversionGet()),HOSVER_MICRO(hosversionGet()),appletGetAppletType()==AppletType_LibraryApplet?"applet":"application");
        snprintf(view.diagnostics[8],144,"Boot keyboard  /  interface 0  /  endpoint 81  /  8-byte reports");
        snprintf(view.diagnostics[9],144,"Use a USB-A to USB-C data cable with the target dock.");
        snprintf(view.diagnostics[10],144,"If usb:ds is busy, close MTP or other USB apps and restart.");
        snprintf(view.diagnostics[11],144,"Press A to reset the USB connection.");
    }
}
static void perform(Action action,uint64_t now) {
    if(action==ACTION_NONE) return;
    if(caps_toggle.phase!=TX_NONE) {
        if(action==ACTION_QUIT) quit_requested=true;
        return;
    }
    if(sender.phase!=TX_NONE) {
        if(action==ACTION_BACK || action==ACTION_ABORT || action==ACTION_QUIT) {
            if(action==ACTION_QUIT) quit_requested=true;
            if(sender.phase!=TX_ABORT) abort_send(false,"Sending cancelled.",now);
        }
        return;
    }
    switch(action) {
        case ACTION_QUIT: quit_requested=true;break;
        case ACTION_THEME: view.dark=!view.dark;save_settings();break;
        case ACTION_PACE: view.pace=pace_next(view.pace);save_settings();break;
        case ACTION_ENTER_PAUSE: view.enter_pause_ms=enter_pause_next(view.enter_pause_ms);save_settings();break;
        case ACTION_AUTOINDENT:
            view.cancel_autoindent=!view.cancel_autoindent;save_settings();
            if(view.screen==SCREEN_CONFIRM) prepare_text();
            break;
        case ACTION_CAPS_LOCK: begin_caps_toggle(now);break;
        case ACTION_LAYOUT:
            view.layout=view.layout==LAYOUT_FR?LAYOUT_US:LAYOUT_FR;save_settings();
            if(view.screen==SCREEN_CONFIRM) prepare_text();
            break;
        case ACTION_DIAGNOSTICS: previous_screen=view.screen;view.screen=SCREEN_DIAGNOSTICS;break;
        case ACTION_HELLO: hello();break;
        case ACTION_FILES: browser_load();break;
        case ACTION_BACK:
            if(view.screen==SCREEN_FILES) browser_parent();
            else if(view.screen==SCREEN_DIAGNOSTICS) view.screen=previous_screen;
            else { view.screen=SCREEN_HOME;view.selected=0; }
            break;
        case ACTION_SEND:
            if(view.screen==SCREEN_CONFIRM) begin_send(now);
            else if(view.screen==SCREEN_DIAGNOSTICS) { keyboard_exit();keyboard_init(); }
            break;
        default:
            if(action>=ACTION_ROW0 && action<ACTION_ROW0+8 && view.screen==SCREEN_FILES) {
                browser_index=browser_index/8*8+(size_t)(action-ACTION_ROW0);browser_open();
            }
            break;
    }
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/plume-usb",0777);
    memset(&view,0,sizeof(view));view.enter_pause_ms=200;load_settings();
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);hidInitializeTouchScreen();
    Result rc=framebufferCreate(&framebuffer,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2);
    if(R_SUCCEEDED(rc)) rc=framebufferMakeLinear(&framebuffer);
    if(R_FAILED(rc)) { framebufferClose(&framebuffer);return 1; }
    if(!ui_init()) { framebufferClose(&framebuffer);return 2; }
    keyboard_init();bool was_touching=false;uint64_t repeat_at=0,next_render=0;
    while(appletMainLoop()) {
        uint64_t now=now_ms();keyboard_poll(now);caps_toggle_tick(now);sending_tick(now);
        if(quit_requested && sender.phase==TX_NONE && caps_toggle.phase==TX_NONE) break;
        padUpdate(&pad);uint64_t down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
        uint64_t navigation=HidNpadButton_Up|HidNpadButton_Down;
        if(down&navigation) repeat_at=now+350;
        else if((held&navigation) && now>=repeat_at) { down|=held&navigation;repeat_at=now+100; }
        if(sender.phase==TX_NONE && caps_toggle.phase==TX_NONE) {
            if(view.screen==SCREEN_HOME && (down&navigation)) view.selected=1-view.selected;
            if(view.screen==SCREEN_FILES && entry_count) {
                if(down&HidNpadButton_Down) browser_index=(browser_index+1)%entry_count;
                if(down&HidNpadButton_Up) browser_index=browser_index?browser_index-1:entry_count-1;
                if(down&HidNpadButton_R) { browser_index+=8;if(browser_index>=entry_count) browser_index=entry_count-1; }
                if(down&HidNpadButton_L) browser_index=browser_index>=8?browser_index-8:0;
            }
        }
        Action action=ACTION_NONE;
        if(down&HidNpadButton_Plus) action=ACTION_QUIT;
        else if(down&HidNpadButton_B) action=ACTION_BACK;
        else if(down&HidNpadButton_A) {
            if(view.screen==SCREEN_HOME) action=view.selected?ACTION_FILES:ACTION_HELLO;
            else if(view.screen==SCREEN_FILES && caps_toggle.phase==TX_NONE) browser_open();
            else if(view.screen==SCREEN_RESULT) action=ACTION_BACK;
            else if(view.screen!=SCREEN_TRANSFER) action=ACTION_SEND;
        } else if(down&HidNpadButton_R && view.screen==SCREEN_CONFIRM) action=ACTION_ENTER_PAUSE;
        else if(down&HidNpadButton_L && view.screen==SCREEN_CONFIRM) action=ACTION_AUTOINDENT;
        else if(down&HidNpadButton_Minus && view.screen!=SCREEN_DIAGNOSTICS) action=ACTION_DIAGNOSTICS;
        else if(down&HidNpadButton_X && view.screen!=SCREEN_DIAGNOSTICS) action=ACTION_CAPS_LOCK;
        else if(down&HidNpadButton_Y && view.screen!=SCREEN_DIAGNOSTICS) action=ACTION_LAYOUT;
        else if(down&HidNpadButton_ZL) action=ACTION_THEME;
        else if(down&HidNpadButton_ZR) action=ACTION_PACE;
        perform(action,now);update_view();
        HidTouchScreenState touch={0};hidGetTouchScreenStates(&touch,1);
        bool touching=touch.count>0;
        if(touching && !was_touching) perform(view_touch(&view,(int)touch.touches[0].x,(int)touch.touches[0].y),now);
        was_touching=touching;update_view();
        /* Honour SET_IDLE without allowing a background packet to overwrite a typed key. */
        if(sender.phase==TX_NONE && caps_toggle.phase==TX_NONE && keyboard_idle_due(now)) keyboard_send(0,0,now);
        /* Keep the USB pump running between frames. Fast transfers redraw at
           10 Hz instead of blocking every packet on a display refresh. */
        bool fast_stream=sender.phase!=TX_NONE && sender.phase!=TX_COUNTDOWN && sender.phase!=TX_ABORT && pace_delay(view.pace)<=10;
        if(now>=next_render) {
            u32 stride;uint32_t *pixels=framebufferBegin(&framebuffer,&stride);
            view_render(&view,pixels,(int)stride);framebufferEnd(&framebuffer);
            next_render=now_ms()+(fast_stream?100:16);
        }
        svcSleepThread(fast_stream?1000000:3000000);
    }
    /* Best effort final all-keys-up before relinquishing the USB controller. */
    if(keyboard_status()->ready) {
        keyboard_cancel_transfer();uint64_t now=now_ms();
        if(R_SUCCEEDED(keyboard_send(0,0,now))) {
            while(keyboard_status()->transfer_pending && now_ms()-now<200) { keyboard_poll(now_ms());svcSleepThread(2000000); }
        }
    }
    appletSetMediaPlaybackState(false);keyboard_exit();text_plan_free(&plan);free(loaded_text);free(entries);
    ui_shutdown();framebufferClose(&framebuffer);return 0;
}
