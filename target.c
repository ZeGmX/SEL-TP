#include <stdio.h>

int target_function() {
    int a;
    scanf("%d", &a);
    return a;
}

int other_target(int a) {
    int b = a*2;
    return b+5;
}

int main(int argc, char** argv) {
    int var;
    while(1) {
        var = target_function();
    }

    return 0;
}

