extern "C" int putchar(int c);
extern "C" void* malloc(unsigned long size);
extern "C" void free(void* ptr);

extern "C" void* op_new(unsigned long size) {
    return malloc(size);
}

extern "C" void op_delete(void* ptr) {
    free(ptr);
}

class Base {
public:
    Base() {}
    virtual ~Base() {
        putchar('B');
    }
};

class Derived : public Base {
public:
    Derived() {}
    ~Derived() {
        putchar('D');
    }
};

int main() {
    Base* b = (Base*)op_new(sizeof(Derived));
    // Manual construction since we don't have placement new easily here
    // but qc should handle 'new Derived()' correctly if we had a proper op_new.
    // Let's just use 'new' and see if it links now that we have op_new.
    delete new Derived(); // Should print DB
    putchar('\n');
    return 0;
}
