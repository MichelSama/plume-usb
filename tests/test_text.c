#include "text.h"
#include "view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_key(uint32_t cp,KeyboardLayout layout,int mod,int key) {
    CharacterKeys got;assert(character_keys(cp,layout,&got));assert(got.count==1);
    assert(got.keys[0].modifier==mod);assert(got.keys[0].key==key);
}
int main(void) {
    TextPlan plan;TextError error;
    for(int layout=0;layout<2;layout++) {
        assert(text_plan("Hello",5,(KeyboardLayout)layout,false,&plan,&error));
        assert(plan.count==5 && plan.characters==5 && plan.lines==0);
        const int keys[]={11,8,15,15,18};
        for(int i=0;i<5;i++) { assert(plan.strokes[i].key==keys[i]);assert(plan.strokes[i].modifier==(i==0?2:0)); }
        text_plan_free(&plan);
        for(unsigned c=32;c<127;c++) { CharacterKeys out;assert(character_keys(c,(KeyboardLayout)layout,&out)); }
    }
    check_key('a',LAYOUT_FR,0,0x14);check_key('q',LAYOUT_FR,0,4);
    check_key('m',LAYOUT_FR,0,0x33);check_key('M',LAYOUT_FR,2,0x33);
    check_key('z',LAYOUT_FR,0,0x1a);check_key('w',LAYOUT_FR,0,0x1d);
    check_key('<',LAYOUT_FR,0,0x64);check_key('>',LAYOUT_FR,2,0x64);
    check_key(0xe9,LAYOUT_FR,0,0x1f);check_key(0xe0,LAYOUT_FR,0,0x27);
    check_key('@',LAYOUT_FR,0x40,0x27);check_key('0',LAYOUT_FR,2,0x27);
    CharacterKeys accent;assert(character_keys(0xe2,LAYOUT_FR,&accent));
    assert(accent.count==2 && accent.keys[0].key==0x2f && accent.keys[1].key==0x14);
    const char crlf[]="\xef\xbb\xbf" "a\r\nb\rc\n\t";
    assert(text_plan(crlf,sizeof(crlf)-1,LAYOUT_FR,false,&plan,&error));
    assert(plan.characters==7 && plan.count==7 && plan.lines==3 && plan.tabs==1);text_plan_free(&plan);
    assert(!text_plan("ok\xf0\x9f\x98\x80",6,LAYOUT_FR,false,&plan,&error));
    assert(!plan.strokes && plan.count==0 && error.codepoint==0x1f600 && error.character==3 && error.byte_offset==2);
    const char *bad[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\x80"};
    for(int i=0;i<5;i++) { assert(!text_plan(bad[i],strlen(bad[i]),LAYOUT_FR,false,&plan,&error));assert(error.invalid_utf8 && !plan.strokes); }
    assert(!text_plan("a\0b",3,LAYOUT_US,false,&plan,&error));assert(error.character==2);
    assert(text_plan("",0,LAYOUT_FR,false,&plan,&error));assert(!plan.count);text_plan_free(&plan);
    assert(!text_plan("\xc3\xa9",2,LAYOUT_US,false,&plan,&error));assert(error.codepoint==0xe9);
    View v={0};assert(view_touch(&v,100,300)==ACTION_HELLO);assert(view_touch(&v,100,450)==ACTION_FILES);
    assert(view_touch(&v,700,678)==ACTION_CAPS_LOCK);
    assert(view_touch(&v,950,678)==ACTION_DIAGNOSTICS);
    v.screen=SCREEN_CONFIRM;
    assert(view_touch(&v,100,678)==ACTION_CAPS_LOCK);
    assert(view_touch(&v,450,678)==ACTION_AUTOINDENT);
    assert(view_touch(&v,720,678)==ACTION_DIAGNOSTICS);
    assert(view_touch(&v,900,678)==ACTION_ENTER_PAUSE);
    v.screen=SCREEN_TRANSFER;assert(view_touch(&v,100,580)==ACTION_ABORT);assert(view_touch(&v,100,300)==ACTION_NONE);
    v.screen=SCREEN_FILES;v.file_rows=2;assert(view_touch(&v,200,275)==ACTION_ROW0+1);assert(view_touch(&v,200,350)==ACTION_NONE);
    puts("PASS: text encoding, UTF-8 rejection, complete preflight, layouts, CRLF, dead keys, touch routing.");return 0;
}
