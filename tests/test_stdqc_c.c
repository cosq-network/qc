#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void test_types() {
    int8_t a = 1;
    uint32_t b = 2;
    size_t c = 3;
    bool d = true;
    if (a + b + c > 0 && d) {
        puts("C Types: OK");
    }
}

void test_malloc() {
    void* p1 = malloc(100);
    void* p2 = malloc(200);
    if (p1 && p2 && p1 != p2) {
        puts("C Malloc: OK");
    } else {
        puts("C Malloc: FAILED");
        exit(1);
    }
}

int main() {
    puts("Starting stdqc C tests...");
    test_types();
    test_malloc();
    puts("All C tests passed!");
    return 0;
}
