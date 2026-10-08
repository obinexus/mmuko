#include <mmuko/program.h>

#include <stdio.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    return mmuko_program_main();
}
