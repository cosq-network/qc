#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include <string>

// External declarations for stdqc functions
extern "C" int puts(const char* s);
extern "C" void exit(int status);

void test_traits() {
    static_assert(std::is_same_v<int, int>, "is_same failed");
    static_assert(!std::is_same_v<int, float>, "is_same failed");
    
    typedef std::remove_reference_t<int&> int_type;
    static_assert(std::is_same_v<int_type, int>, "remove_reference failed");
    
    puts("C++ Traits: OK");
}

void test_utility() {
    int x = 42;
    int&& y = std::move(x);
    if (y == 42) {
        puts("C++ Utility (move): OK");
    }
}

struct Foo {
    int x;
    Foo() : x(123) {}
    Foo(int val) : x(val) {}
};

void test_new() {
    Foo* f = new Foo();
    if (f && f->x == 123) {
        puts("C++ New: OK");
    } else {
        puts("C++ New: FAILED");
        exit(1);
    }
    delete f;
}

void test_vector() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    if (v.size() == 3 && v[0] == 1 && v[1] == 2 && v[2] == 3) {
        puts("std::vector (int): OK");
    } else {
        puts("std::vector (int): FAILED");
        exit(1);
    }

    std::vector<Foo> vf;
    vf.push_back(Foo(10));
    vf.push_back(Foo(20));
    if (vf.size() == 2 && vf[0].x == 10 && vf[1].x == 20) {
        puts("std::vector (struct): OK");
    } else {
        puts("std::vector (struct): FAILED");
        exit(1);
    }
}

void test_string() {
    std::string s = "Hello";
    s += " World";
    if (s.size() == 11 && s[0] == 'H' && s[10] == 'd') {
        puts("std::string: OK");
    } else {
        puts("std::string: FAILED");
        exit(1);
    }
}

int main() {
    puts("Starting stdqc C++ tests...");
    test_traits();
    test_utility();
    test_new();
    test_vector();
    test_string();
    puts("All C++ tests passed!");
    return 0;
}
