#define HELLO 1

#ifdef HELLO
int a = 1;
#else
int a = 0;
#endif

#ifndef HELLO
int b = 1;
#else
int b = 0;
#endif

int main() {
    return a + b;
}
