#pragma once
/* optable.h — the ASM op name table, transcribed from the reference `OpType` enum.
   `byName` in the reference is a case-insensitive first-match over declaration
   order, so order and duplicates are part of the behaviour, not noise — keep them
   verbatim if this table is ever revised.

   There is no generator for this file. An earlier version of this comment said
   "generated … regenerate with tools/gen_optable.py"; that script has never existed
   in the repository (never committed, absent from a clean clone), so the file was
   hand-maintained while claiming not to be. Edits here are made directly — check
   them against the reference `OpType` declaration order rather than regenerating. */

/** One ASM op name, in reference declaration order. */
typedef struct op_name_t
{
  const char* name;
  int opcode;
  int param_count;
} op_name_t;

static const op_name_t OP_NAMES[] = {
  {"yield", 0, 0},       {"rewind", 1, 0},        {"wait", 2, 1},
  {"wait_cmp", 3, 2},    {"wait_cmp", 4, 1},      {"rewind", 5, 0},
  {"rewind", 6, 0},      {"rewind", 7, 0},        {"mov", 8, 2},
  {"swap_broken", 9, 2}, {"memcpy", 10, 3},       {"rewind", 11, 0},
  {"mov", 12, 1},        {"rewind", 13, 0},       {"rewind", 14, 0},
  {"rewind", 15, 0},     {"and", 16, 2},          {"or", 17, 2},
  {"xor", 18, 2},        {"andor", 19, 3},        {"not", 20, 1},
  {"shl", 21, 2},        {"shr", 22, 2},          {"add", 24, 2},
  {"sub", 25, 2},        {"sub_rev", 26, 2},      {"incr", 27, 1},
  {"decr", 28, 1},       {"neg", 29, 1},          {"abs", 30, 1},
  {"mul", 32, 2},        {"div", 33, 2},          {"div_rev", 34, 2},
  {"mod", 35, 2},        {"mod_rev", 36, 2},      {"mul_12", 40, 2},
  {"div_12", 41, 2},     {"div_12_rev", 42, 2},   {"mod", 43, 2},
  {"mod_rev", 44, 2},    {"sqrt", 48, 2},         {"rand", 49, 2},
  {"sin_12", 50, 2},     {"cos_12", 51, 2},       {"atan2_12", 52, 3},
  {"call", 56, 0},       {"jmp", 64, 1},          {"jmp_cmp", 65, 3},
  {"jmp_cmp", 66, 2},    {"while", 67, 2},        {"jmp_table", 68, 2},
  {"gosub", 72, 1},      {"return", 73, 0},       {"gosub_table", 74, 2},
  {"deallocate", 80, 0}, {"deallocate", 82, 0},   {"deallocate_other", 83, 1},
  {"fork", 86, 3},       {"fork_reenter", 87, 3}, {"consume", 88, 0},
  {"debug96", 96, 2},    {"debug97", 97, 0},      {"debug98", 98, 1},
  {"depth", 99, 1},      {"noop", 100, 0},        {"push", 101, 1},
  {"pop", 102, 1},       {"cmp", 103, 3},
};

#define OP_NAME_COUNT ((int)(sizeof(OP_NAMES) / sizeof(OP_NAMES[0])))
