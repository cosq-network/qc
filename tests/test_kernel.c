// Simple freestanding kernel entry point

void outb(unsigned short port, unsigned char val) {
    // We would normally use inline assembly here, but qc doesn't support inline assembly yet.
    // So we just define a dummy function to test linking and compilation.
    (void)port;
    (void)val;
}

void print_char(char c) {
    // Assuming a simple serial port at 0x3F8
    outb(0x3F8, c);
}

void _start() {
    print_char('O');
    print_char('S');
    print_char('\n');
    
    // Infinite loop
    while (1) {}
}
