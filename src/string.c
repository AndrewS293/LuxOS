#include "../include/string.h"

//string functions, important to understand, we may kinda cover them but not much

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, int n) {
    if (n <= 0) return 0;
    while (n-- > 0) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0; 
    }
    return 0;
}

int strlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

void strcat(char* dest, const char* src) {
    while (*dest) dest++;
    while ((*dest++ = *src++));
}


const char* cmd_args(const char* input, const char* command) {
    int i = 0;
    while (command[i]) {
        if (input[i] != command[i]) return (const char*)0;
        i++;
    }
    const char* p = input + i;
    if (*p != ' ' && *p != '\0' && *p != '\n' && *p != '\r') return (const char*)0;
    while (*p == ' ') p++;
    return p;
}

