extern "C" int putchar(int c);

class RAII {
    int id;
public:
    RAII(int i) {
        id = i;
        putchar('C');
        putchar(id + '0');
    }
    ~RAII() {
        putchar('D');
        putchar(id + '0');
    }
};

void test() {
    int i = 0;
    while (i < 2) {
        RAII a(i);
        if (i == 1) break;
        i++;
    }
    putchar('\n');
}

int main() {
    test();
    return 0;
}
