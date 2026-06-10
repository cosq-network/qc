extern "C" int puts(const char*);

struct A {
    int a;
    A() {
        a = 1;
        puts("A::A()");
    }
};

struct B {
    int b;
    B() {
        b = 2;
        puts("B::B()");
    }
};

struct Derived : public A, public B {
    int d;
    Derived() {
        d = 3;
        puts("Derived::Derived()");
    }
};

int main() {
    Derived* obj = new Derived();
    if (obj->a == 1 && obj->b == 2 && obj->d == 3) {
        puts("Fields OK");
    } else {
        puts("Fields FAILED");
    }
    delete obj;
    return 0;
}
