#include "text.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Model an editor which copies leading space/tab characters literally on
   Enter and erases one character per Backspace. Compare its resulting text
   with the normalized input, rather than just mirroring the planner's loop. */
static void check_editor(const char *input,KeyboardLayout layout,bool compensate) {
    TextPlan plan;TextError error;
    assert(text_plan(input,strlen(input),layout,compensate,&plan,&error));
    char result[4096]={0},expected[4096]={0};size_t used=0,length=0;
    const char *start=input;
    if(!strncmp(input,"\xef\xbb\xbf",3)) start+=3;
    for(const char *p=start;*p;p++) {
        char c=*p;
        if(c=='\r') { if(p[1]=='\n')p++;c='\n'; }
        expected[length++]=c;
    }
    for(size_t i=0;i<plan.count;i++) {
        Stroke key=plan.strokes[i];
        assert(key.modifier==0 && used+length+1<sizeof(result));
        if(key.key==0x28) {
            size_t line_start=used;
            while(line_start && result[line_start-1]!='\n')line_start--;
            size_t indent=0;
            while(line_start+indent<used && (result[line_start+indent]==' ' || result[line_start+indent]=='\t'))indent++;
            result[used++]='\n';
            if(compensate) { memcpy(result+used,result+line_start,indent);used+=indent; }
        } else if(key.key==0x2a) {
            assert(compensate && used && result[used-1]!='\n');used--;
        } else {
            char c=key.key==0x2c?' ':key.key==0x2b?'\t':
                key.key==0x1b?'x':key.key==0x05?'b':key.key==0x06?'c':0;
            assert(c);result[used++]=c;
        }
    }
    assert(used==length && !memcmp(result,expected,length));
    assert(plan.lines==0 || plan.count>=plan.characters);
    text_plan_free(&plan);
}

int main(void) {
    const char *cases[]={
        "", "x", "\n", "  x\nb", "\tx\nb", "  \tx\nb", "\t  \tx\n\tb\nc",
        "  x\n    b\nc", "  x\n\nb", " \t\nb", "x \t\nb",
        "\tx\n", "\tx\r\nb\rc", "\xef\xbb\xbf\t x\nb"
    };
    for(int layout=0;layout<2;layout++) {
        for(int enabled=0;enabled<2;enabled++) {
            for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
                check_editor(cases[i],(KeyboardLayout)layout,enabled!=0);
            /* All unsupported control characters still fail complete preflight,
               including after an indented line generated internal Backspaces. */
            for(unsigned cp=0;cp<=0x7f;cp++) {
                if(cp>=0x20 && cp!=0x7f)continue;
                if(cp=='\t' || cp=='\n' || cp=='\r')continue;
                const char input[]={' ','x','\n',(char)cp};
                TextPlan plan;TextError error;
                assert(!text_plan(input,sizeof(input),(KeyboardLayout)layout,enabled!=0,&plan,&error));
                assert(!plan.strokes && !plan.count && error.codepoint==cp && error.character==4 && error.byte_offset==3);
            }
        }
        CharacterKeys keys;assert(!character_keys('\b',(KeyboardLayout)layout,&keys));
    }
    /* Exercise the largest permitted file with a long mixed indentation. */
    size_t n=262144;char *input=malloc(n);assert(input);
    for(size_t i=0;i<n-2;i++)input[i]=i%2?' ':'\t';
    input[n-2]='x';input[n-1]='\n';
    TextPlan plan;TextError error;
    assert(text_plan(input,n,LAYOUT_US,true,&plan,&error));
    assert(plan.characters==n && plan.lines==1 && plan.count==2*n-2);
    for(size_t i=n;i<plan.count;i++)assert(plan.strokes[i].key==0x2a && !plan.strokes[i].modifier);
    text_plan_free(&plan);free(input);
    puts("PASS: auto-indent editor results, spaces/tabs/mixed indentation, blank lines, dedents, BOM/CRLF, on/off, control rejection and 256 KiB input.");
    return 0;
}
