extern "C" int puts(const char*);

struct A {
    virtual void sayA() { puts("A::sayA()"); }
};

struct B {
    virtual void sayB() { puts("B::sayB()"); }
};

struct Derived : public A, public B {
    void sayA() override { puts("Derived::sayA()"); }
    void sayB() override { puts("Derived::sayB()"); }
};

int main() {
    Derived* d = new Derived();
    
    A* a = d;
    a->sayA();
    
    B* b = d;
    b->sayB();
    
    delete d;
    return 0;
}
