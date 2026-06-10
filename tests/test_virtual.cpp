extern "C" int puts(const char*);

struct Base {
    Base() {
        puts("Base constructor");
    }
    virtual void sayHello() {
        puts("Hello from Base");
    }
    virtual ~Base() {}
};

struct Derived : public Base {
    Derived() {
        puts("Derived constructor");
    }
    void sayHello() override {
        puts("Hello from Derived");
    }
};

int main() {
    Base* b = new Derived();
    b->sayHello();
    delete b;
    return 0;
}
