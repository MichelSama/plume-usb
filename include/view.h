#pragma once
#include "text.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SCREEN_HOME,SCREEN_FILES,SCREEN_CONFIRM,SCREEN_TRANSFER,SCREEN_RESULT,SCREEN_DIAGNOSTICS } Screen;
typedef struct {
    Screen screen;
    bool dark,usb_initialized,usb_ready,error,sending;
    bool caps_pending,cancel_autoindent;
    int caps_state; /* -1 unknown, 0 off, 1 on; reported by the USB host */
    KeyboardLayout layout;
    int selected,pace,seconds,enter_pause_ms;
    size_t characters,lines,tabs,done,total,file_count,file_start;
    char path[1024],name[256],message[384],preview[1024],usb_label[128];
    char files[8][256];bool directories[8];int file_rows;
    char diagnostics[12][144];
} View;
typedef enum { ACTION_NONE,ACTION_HELLO,ACTION_FILES,ACTION_BACK,ACTION_SEND,ACTION_THEME,ACTION_LAYOUT,ACTION_PACE,ACTION_DIAGNOSTICS,ACTION_QUIT,ACTION_ABORT,ACTION_CAPS_LOCK,ACTION_ENTER_PAUSE,ACTION_AUTOINDENT,ACTION_ROW0=100 } Action;
void view_render(const View *view,uint32_t *pixels,int stride);
Action view_touch(const View *view,int x,int y);
