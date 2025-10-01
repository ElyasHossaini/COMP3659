/* #include <unistd.h> */

void write(int, const char *, int);
void _exit(int);

const char *message = "hello, world!\n";

int main()
{
    write(1, message, 14);
    _exit(0);
}
