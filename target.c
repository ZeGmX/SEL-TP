#include <stdio.h>

int target_function2(int i) {
  printf("You have visited the target_function2 function, well done!\n");
  printf("Your argument was %d\n", i);
  return 69;
}

int target_function() {
    printf("Going in!\n");
    int a;
    scanf("%d", &a);
    printf("Going out!\n");
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
        printf("main %d\n", var);
    }

    return 0;
}
