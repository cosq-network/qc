#define FOO
#define BAR

#if defined(FOO) && defined(BAR)
int both = 1;
#endif

#if defined(BAZ)
int baz = 1;
#else
int no_baz = 1;
#endif

#if !defined(BAZ)
int not_baz = 1;
#endif

#if defined(FOO) || defined(BAZ)
int foo_or_baz = 1;
#endif

int main() {
    return 0;
}
