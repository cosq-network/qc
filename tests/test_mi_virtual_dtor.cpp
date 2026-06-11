extern "C" int putchar(int c);

class A {
public:
    A() {}
    virtual ~A() { putchar('A'); }
};

class B {
public:
    B() {}
    virtual ~B() { putchar('B'); }
};

class C : public A, public B {
public:
    C() {}
    ~C() { putchar('C'); }
};

extern "C" void* malloc(unsigned long size);
extern "C" void free(void* ptr);
extern "C" void* op_new(unsigned long size) { return malloc(size); }
extern "C" void op_delete(void* ptr) { free(ptr); }

int main() {
    A* a = new C();
    delete a; // Should print CBA
    putchar('\n');
    return 0;
}
