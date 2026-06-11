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

void f(const RAII& a, const RAII& b) {
    putchar('F');
}

int main() {
    f(RAII(1), RAII(2));
    putchar('\n');
    return 0;
}
