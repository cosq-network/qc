#include <stdio.h>

void print_str(const char* s) {
    fputs(s, stdout);
}

void print_int(int n) {
    printf("%d", n);
}
