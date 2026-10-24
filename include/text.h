#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { LAYOUT_FR, LAYOUT_US } KeyboardLayout;
typedef struct { uint8_t modifier, key; } Stroke;
typedef struct { Stroke keys[2]; unsigned count; } CharacterKeys;
typedef struct {
    Stroke *strokes;
    size_t count, characters, lines, tabs;
} TextPlan;
typedef struct { size_t byte_offset, character; uint32_t codepoint; bool invalid_utf8; } TextError;

bool utf8_next(const char *text, size_t length, size_t *offset, uint32_t *cp);
bool character_keys(uint32_t cp, KeyboardLayout layout, CharacterKeys *out);
bool text_plan(const char *text, size_t length, KeyboardLayout layout, bool cancel_autoindent, TextPlan *plan, TextError *error);
void text_plan_free(TextPlan *plan);
