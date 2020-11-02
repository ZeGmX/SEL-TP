#include <stdio.h>

extern int run(int argc, char** argv);

int main(int argc, char** argv) {
    printf("CHALLENGE %d\n", PART);
    return run(argc, argv);
}

// make PART=2
