extern "C" int putchar(int c);

class RAII {
    int id;
public:
    RAII(int i) {
        id = i;
        putchar('C');
        putchar(id + '0');
    }
    RAII(const RAII& other) {
        id = other.id + 1;
        putchar('K'); // Copy
        putchar(id + '0');
    }
    ~RAII() {
        putchar('D');
        putchar(id + '0');
    }
    int get() const { return id; }
};

int main() {
    RAII a(1);
    RAII b = a;
    putchar('\n');
    return 0;
}
