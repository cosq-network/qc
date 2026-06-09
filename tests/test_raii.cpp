int putchar(int c);

void print_int(int n) {
    if (n < 0) { putchar('-'); n = -n; }
    if (n / 10) print_int(n / 10);
    putchar((n % 10) + '0');
}

class RAII {
    int id;
public:
    RAII(int i) {
        id = i;
        putchar('C');
        print_int(id);
        putchar('\n');
    }

    ~RAII() {
        putchar('D');
        print_int(id);
        putchar('\n');
    }
};

void test() {
    RAII a(1);
    {
        RAII b(2);
    }
    RAII c(3);
}

int main() {
    test();
    return 0;
}
