#include <stdio.h>

int target_function2(int i) {
  printf("You have visited the target_function2 function, well done!\n");
  printf("Your argument was %d\n", i);
  return 0;
}

int target_function() {
    int a;
    scanf("%d", &a);
    return a;
}

int main(int argc, char** argv) {
    int var;
    while(1) {
        var = target_function();
    }

    return 0;
}
