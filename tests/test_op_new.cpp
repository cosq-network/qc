extern "C" int puts(const char*);
extern "C" void* malloc(unsigned long);
extern "C" void free(void*);

struct CustomAlloc {
    int x;
    CustomAlloc(int val) {
        x = val;
        puts("CustomAlloc constructor");
    }
    
    static void* operator new(unsigned long size) {
        puts("Custom operator new");
        return malloc(size);
    }
    
    static void operator delete(void* ptr) {
        puts("Custom operator delete");
        free(ptr);
    }
};

int main() {
    CustomAlloc* p = new CustomAlloc(42);
    if (p->x == 42) {
        puts("p->x is 42");
    }
    delete p;
    return 0;
}
