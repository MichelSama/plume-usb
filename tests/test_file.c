#include "text.h"
#include "typer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Reads an optional external file as data only; never executes its contents. */
int main(int argc,char **argv) {
    if(argc!=2) { puts("Usage: test_file path-to-utf8.txt");return 1; }
    FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>=0 && length<=262144);
    rewind(file);char *data=malloc((size_t)length+1);assert(data);
    assert(fread(data,1,(size_t)length,file)==(size_t)length);fclose(file);
    size_t breaks=0;
    for(long i=0;i<length;i++) {
        if(data[i]=='\r') { breaks++;if(i+1<length && data[i+1]=='\n')i++; }
        else if(data[i]=='\n') breaks++;
    }
    const unsigned delays[]={0,2,10};
    for(int layout=0;layout<2;layout++) {
        TextPlan plan;TextError error;
        assert(text_plan(data,(size_t)length,(KeyboardLayout)layout,false,&plan,&error));
        assert(plan.lines==breaks);
        for(unsigned speed=0;speed<3;speed++) {
            Typer t;typer_start(&t,plan.strokes,plan.count,delays[speed],0);t.enter_pause_ms=200;
            size_t emitted=0,enters=0;unsigned held=0;
            uint64_t ack_at=0,enter_release_ack=0,enter_down_ack=0;
            for(uint64_t now=0;now<10000000 && t.phase!=TX_DONE;now++) {
                Stroke next;
                if(typer_step(&t,now,now<ack_at,&next)) {
                    assert(now>=ack_at);
                    if(next.key) {
                        assert(!held && emitted<plan.count);
                        assert(next.key==plan.strokes[emitted].key && next.modifier==plan.strokes[emitted].modifier);
                        if(enter_release_ack) assert(now-enter_release_ack>=200);
                        enter_release_ack=0;emitted++;
                        if(next.key==0x28) { enters++;assert(now-ack_at>=50);enter_down_ack=now+7; }
                        held=next.key;
                    } else if(held==0x28) {
                        assert(now-enter_down_ack>=50);enter_release_ack=now+7;
                    }
                    if(!next.key) held=0;
                    ack_at=now+7; /* Simulated transport latency per report. */
                }
                if(t.phase==TX_DONE && enter_release_ack) assert(now-enter_release_ack>=200);
            }
            assert(t.phase==TX_DONE && emitted==plan.count && enters==breaks && !held);
            printf("PASS: layout=%s delay=%u ms strokes=%zu Enter=%zu, ordered press/release and Enter timing.\n",layout?"US":"FR",delays[speed],emitted,enters);
        }
        text_plan_free(&plan);
    }
    free(data);return 0;
}
