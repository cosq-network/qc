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
    int get() const { return id; }
};

int use(const RAII& r) {
    return r.get();
}

int main() {
    putchar('1');
    use(RAII(2));
    putchar('3');
    putchar('\n');
    return 0;
}
