_Static_assert(1, "Always true");

_Alignas(16) int aligned_var = 0;

_Noreturn void die() {
    while(1) {}
}

int main() {
    int align_val = _Alignof(int);
    int type_idx = _Generic(align_val, int: 1, float: 2, default: 0);
    return type_idx;
}
