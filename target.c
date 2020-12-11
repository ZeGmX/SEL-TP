#include <unistd.h>
#include <stdio.h>

int target_function2(int i) {
  printf("You have visited the target_function2 function, well done!\n");
  printf("Your argument was %d\n", i);
  return 43;
}

int target_function() {
    printf("Going in!\n");
    int a = 5;
    scanf("%d", &a);
    printf("Going out!\n");
    return a;
}

void target_function3(int* a) {
    printf("You have visited the target_function3 function, well done!\n");
    printf("Address: %p, value\n", a);
    *a = 42;
}

int optimised() {
    return 1;
}

int main(int argc, char** argv) {
    int var;
    while(1) {
        var = target_function();
        printf("Main here\n");
        printf("%d\n", var);
    }

    return 0;
}
