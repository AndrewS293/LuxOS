#ifndef STRING_H
#define STRING_H

#include "common.h"

int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, int n);
const char* cmd_args(const char* input, const char* command);
int strlen(const char* s);
void strcpy(char* dest, const char* src);
void strcat(char* dest, const char* src);

#endif
