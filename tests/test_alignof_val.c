_Static_assert(_Alignof(int) == 4, "int alignment should be 4");
_Static_assert(_Alignof(char) == 1, "char alignment should be 1");
_Static_assert(_Alignof(long long) == 8, "long long alignment should be 8");

int main() {
    return 0;
}
