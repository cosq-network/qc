extern "C" int putchar(int c);

class RAII {
    int id;
public:
    RAII(int i) : id(i) { putchar('C'); putchar(id + '0'); }
    ~RAII() { putchar('D'); putchar(id + '0'); }
};

void may_throw(int i) {
    if (i == 0) throw 1;
    putchar('T');
}

int main() {
    try {
        RAII r(1);
        may_throw(0);
        putchar('X');
    } catch (int e) {
        putchar('E');
    }
    putchar('\n');
    return 0;
}
