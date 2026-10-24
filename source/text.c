#include "text.h"
#include <stdlib.h>
#include <string.h>

#define SHIFT 0x02
#define ALTGR 0x40

bool utf8_next(const char *s, size_t n, size_t *pos, uint32_t *cp) {
    if (*pos >= n) return false;
    size_t i = *pos;
    uint8_t c = (uint8_t)s[i++];
    uint32_t v;
    unsigned extra;
    if (c < 0x80) { *cp = c; *pos = i; return true; }
    if (c >= 0xc2 && c <= 0xdf) { v = c & 31; extra = 1; }
    else if (c >= 0xe0 && c <= 0xef) { v = c & 15; extra = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { v = c & 7; extra = 3; }
    else return false;
    if (i + extra > n) return false;
    for (unsigned k = 0; k < extra; k++) {
        c = (uint8_t)s[i++];
        if ((c & 0xc0) != 0x80) return false;
        v = (v << 6) | (c & 63);
    }
    if ((extra == 1 && v < 0x80) || (extra == 2 && v < 0x800) ||
        (extra == 3 && v < 0x10000) || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return false;
    *cp = v; *pos = i; return true;
}

static bool single(CharacterKeys *out, uint8_t modifier, uint8_t key) {
    out->keys[0] = (Stroke){modifier,key}; out->count = 1; return true;
}

bool character_keys(uint32_t cp, KeyboardLayout layout, CharacterKeys *out) {
    memset(out, 0, sizeof(*out));
    if (cp == '\n' || cp == '\r') return single(out, 0, 0x28);
    if (cp == '\t') return single(out, 0, 0x2b);
    if (cp == ' ') return single(out, 0, 0x2c);
    bool upper = cp >= 'A' && cp <= 'Z';
    uint32_t letter = upper ? cp + 32 : cp;
    if (letter >= 'a' && letter <= 'z') {
        uint8_t key = (uint8_t)(4 + letter - 'a');
        if (layout == LAYOUT_FR) {
            switch (letter) {
                case 'a': key = 0x14; break; case 'q': key = 0x04; break;
                case 'w': key = 0x1d; break; case 'z': key = 0x1a; break;
                case 'm': key = 0x33; break;
            }
        }
        return single(out, upper ? SHIFT : 0, key);
    }
    if (cp >= '0' && cp <= '9') return single(out, layout == LAYOUT_FR ? SHIFT : 0,
        cp == '0' ? 0x27 : (uint8_t)(0x1e + cp - '1'));
    if (layout == LAYOUT_US) {
        const char *plain = "-=[]\\;'`,./";
        const uint8_t keys[] = {0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
        const char *shift = "_+{}|:\"~<>?";
        for (size_t i=0; i<sizeof(keys); i++) {
            if (cp == (uint8_t)plain[i]) return single(out,0,keys[i]);
            if (cp == (uint8_t)shift[i]) return single(out,SHIFT,keys[i]);
        }
        const char *numbers = "!@#$%^&*()";
        for (unsigned i=0; i<10; i++) if (cp == (uint8_t)numbers[i])
            return single(out,SHIFT,i==9?0x27:(uint8_t)(0x1e + i));
        return false;
    }
    /* Traditional French AZERTY. ISO '< >' is usage 0x64 (non-US backslash). */
    switch (cp) {
        case '!': return single(out,0,0x38); case '"': return single(out,0,0x20);
        case '#': return single(out,ALTGR,0x20); case '$': return single(out,0,0x30);
        case '%': return single(out,SHIFT,0x34); case '&': return single(out,0,0x1e);
        case '\'': return single(out,0,0x21); case '(': return single(out,0,0x22);
        case ')': return single(out,0,0x2d); case '*': return single(out,0,0x31);
        case '+': return single(out,SHIFT,0x2e); case ',': return single(out,0,0x10);
        case '-': return single(out,0,0x23); case '.': return single(out,SHIFT,0x36);
        case '/': return single(out,SHIFT,0x37); case ':': return single(out,0,0x37);
        case ';': return single(out,0,0x36); case '<': return single(out,0,0x64);
        case '=': return single(out,0,0x2e); case '>': return single(out,SHIFT,0x64);
        case '?': return single(out,SHIFT,0x10); case '@': return single(out,ALTGR,0x27);
        case '[': return single(out,ALTGR,0x22); case '\\': return single(out,ALTGR,0x25);
        case ']': return single(out,ALTGR,0x2d); case '_': return single(out,0,0x25);
        case '{': return single(out,ALTGR,0x21); case '|': return single(out,ALTGR,0x23);
        case '}': return single(out,ALTGR,0x2e);
        case 0xe9: return single(out,0,0x1f); case 0xe8: return single(out,0,0x24);
        case 0xe7: return single(out,0,0x26); case 0xe0: return single(out,0,0x27);
        case 0xf9: return single(out,0,0x34); case 0xb0: return single(out,SHIFT,0x2d);
        case 0xa3: return single(out,SHIFT,0x30); case 0xb5: return single(out,SHIFT,0x31);
        case 0x20ac: return single(out,ALTGR,0x08);
        /* Dead accents must be followed by a base letter (or space for the accent alone). */
        case '^': out->keys[0]=(Stroke){0,0x2f}; out->keys[1]=(Stroke){0,0x2c}; out->count=2; return true;
        case '`': out->keys[0]=(Stroke){ALTGR,0x24}; out->keys[1]=(Stroke){0,0x2c}; out->count=2; return true;
        case '~': out->keys[0]=(Stroke){ALTGR,0x1f}; out->keys[1]=(Stroke){0,0x2c}; out->count=2; return true;
    }
    const uint32_t accented[] = {0xe2,0xea,0xee,0xf4,0xfb,0xe4,0xeb,0xef,0xf6,0xfc};
    const char *base = "aeiouaeiou";
    for (unsigned i=0; i<10; i++) if (cp == accented[i]) {
        out->keys[0]=(Stroke){i>=5?SHIFT:0,0x2f};
        out->keys[1]=(Stroke){0,base[i]=='a'?0x14:(uint8_t)(4+base[i]-'a')}; out->count=2; return true;
    }
    return false;
}

bool text_plan(const char *text, size_t n, KeyboardLayout layout, bool cancel_autoindent, TextPlan *plan, TextError *error) {
    memset(plan,0,sizeof(*plan)); memset(error,0,sizeof(*error));
    if (n > 262144) return false;
    /* When cancelling auto-indent we append up to one backspace per character
       (leading spaces/tabs), so allocate n*3 to stay safe; otherwise n*2 suffices
       for two-stroke dead keys in the FR layout. */
    size_t alloc = cancel_autoindent ? n*3 : n*2;
    plan->strokes = malloc((alloc ? alloc : 1)*sizeof(Stroke));
    if (!plan->strokes) return false;
    size_t pos=0;
    if (n>=3 && memcmp(text,"\xef\xbb\xbf",3)==0) pos=3;
    size_t line_indent_characters=0; bool at_line_start=true;
    while (pos<n) {
        size_t start=pos; uint32_t cp=0;
        error->byte_offset=start; error->character=plan->characters+1;
        if (!utf8_next(text,n,&pos,&cp)) { error->invalid_utf8=true; goto fail; }
        error->codepoint=cp;
        if (cp=='\r') { if (pos<n && text[pos]=='\n') pos++; cp='\n'; }
        CharacterKeys keys;
        if (!character_keys(cp,layout,&keys)) goto fail;
        for (unsigned j=0;j<keys.count;j++) plan->strokes[plan->count++]=keys.keys[j];
        plan->characters++;
        if (cp=='\n') {
            plan->lines++;
            if (cancel_autoindent) {
                /* Assumes literal space/tab copying and one character erased
                   per Backspace. These internal key presses are not accepted
                   as control characters in input files. */
                Stroke bs={0,0x2a};
                for (size_t i=0; i<line_indent_characters; i++) plan->strokes[plan->count++]=bs;
            }
            line_indent_characters=0; at_line_start=true;
        } else {
            if (at_line_start && (cp==' ' || cp=='\t')) line_indent_characters++;
            else at_line_start=false;
            if (cp=='\t') plan->tabs++;
        }
    }
    return true;
fail:
    text_plan_free(plan); return false;
}

void text_plan_free(TextPlan *plan) { free(plan->strokes); memset(plan,0,sizeof(*plan)); }
