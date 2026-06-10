extern "C" int puts(const char*);

struct Shape {
    virtual void draw() = 0;
    virtual ~Shape() {}
};

struct Circle : public Shape {
    void draw() override {
        puts("Circle::draw()");
    }
};

int main() {
    // Shape s; // Error: abstract
    Shape* s = new Circle();
    s->draw();
    delete s;
    return 0;
}
