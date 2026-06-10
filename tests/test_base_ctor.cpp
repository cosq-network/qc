extern "C" int puts(const char*);

struct Base {
    Base() {
        puts("Base::Base()");
    }
};

struct Derived : public Base {
    Derived() {
        puts("Derived::Derived()");
    }
};

int main() {
    Derived* d = new Derived();
    delete d;
    return 0;
}
