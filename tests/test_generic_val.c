_Static_assert(_Generic(0, int: 1, default: 0) == 1, "0 should match int");
_Static_assert(_Generic(0.0, double: 1, default: 0) == 1, "0.0 should match double");

int main() {
    return 0;
}
