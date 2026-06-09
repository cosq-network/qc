int putchar(int c);

void print_int(int n) {
    if (n < 0) { putchar('-'); n = -n; }
    if (n / 10) print_int(n / 10);
    putchar((n % 10) + '0');
}

int main() {
    print_int(12345);
    putchar('\n');
    return 0;
}
