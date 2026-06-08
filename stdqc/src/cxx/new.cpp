#include <cstddef>

extern "C" void* malloc(std::size_t size);
extern "C" void free(void* ptr);

void* operator new(std::size_t size) {
    return malloc(size);
}

void* operator new[](std::size_t size) {
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    free(ptr);
}
