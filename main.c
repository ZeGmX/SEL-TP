#include <stdio.h>
#define CHALLENGE test

extern int run(int argc, char** argv);

int main(int argc, char** argv) {
    printf("CHALLENGE");
    return run(argc, argv); 
}

