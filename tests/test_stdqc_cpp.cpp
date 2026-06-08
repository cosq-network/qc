#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

int main() {
    puts("Starting stdqc C++ tests...");
    test_traits();
    test_utility();
    test_new();
    puts("All C++ tests passed!");
    return 0;
}
