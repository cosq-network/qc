#include "qbios/memory.h"
#include "qbios/string.h"

// Minimal testing framework for bare-metal/freestanding
#define ASSERT(cond, msg) \
    if (!(cond)) { \
        print_str("ASSERTION FAILED: "); \
        print_str(msg); \
        print_str("\n"); \
        test_failed = 1; \
    }

int test_failed = 0;

// Host-provided or system-provided routines for test output
extern void print_str(const char* s);
extern void print_int(int n);

void test_strlen() {
    ASSERT(strlen("") == 0, "strlen empty");
    ASSERT(strlen("hello") == 5, "strlen hello");
}

void test_strcpy() {
    char buf[10];
    strcpy(buf, "test");
    ASSERT(buf[0] == 't' && buf[1] == 'e' && buf[2] == 's' && buf[3] == 't' && buf[4] == '\0', "strcpy test");
}

void test_strcmp() {
    ASSERT(strcmp("abc", "abc") == 0, "strcmp equal");
    ASSERT(strcmp("abc", "abd") < 0, "strcmp less");
    ASSERT(strcmp("abd", "abc") > 0, "strcmp greater");
    ASSERT(strcmp("abc", "ab") > 0, "strcmp diff length");
}

void test_memset() {
    char buf[5] = {1, 2, 3, 4, 5};
    memset(buf, 0, 3);
    ASSERT(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 4 && buf[4] == 5, "memset partial");
}

void test_memcpy() {
    char src[5] = {1, 2, 3, 4, 5};
    char dst[5] = {0};
    memcpy(dst, src, 5);
    ASSERT(dst[0] == 1 && dst[4] == 5, "memcpy full");
}

void test_memmove() {
    char buf[10] = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};
    
    // Non-overlapping
    memmove(buf + 5, buf, 5);
    ASSERT(buf[5] == 1 && buf[9] == 5, "memmove non-overlapping");

    // Overlapping (dst > src)
    char buf2[10] = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};
    memmove(buf2 + 2, buf2, 5);
    ASSERT(buf2[0] == 1 && buf2[2] == 1 && buf2[3] == 2 && buf2[6] == 5, "memmove overlapping right");

    // Overlapping (src > dst)
    char buf3[10] = {0, 0, 1, 2, 3, 4, 5, 0, 0, 0};
    memmove(buf3, buf3 + 2, 5);
    ASSERT(buf3[0] == 1 && buf3[4] == 5 && buf3[6] == 5, "memmove overlapping left");
}

void test_memcmp() {
    char b1[5] = {1, 2, 3, 4, 5};
    char b2[5] = {1, 2, 3, 4, 5};
    char b3[5] = {1, 2, 3, 5, 5};
    
    ASSERT(memcmp(b1, b2, 5) == 0, "memcmp equal");
    ASSERT(memcmp(b1, b3, 5) < 0, "memcmp less");
    ASSERT(memcmp(b3, b1, 5) > 0, "memcmp greater");
}

int main() {
    test_strlen();
    test_strcpy();
    test_strcmp();
    test_memset();
    test_memcpy();
    test_memmove();
    test_memcmp();

    if (test_failed) {
        print_str("Some qbios tests FAILED.\n");
        return 1;
    } else {
        print_str("All qbios tests PASSED.\n");
        return 0;
    }
}
