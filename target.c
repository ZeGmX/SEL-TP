#include <unistd.h>
#include <stdio.h>

int target_function() {
    printf("Entering original target_function!\n");
    int a = 5;
    return a;
}

// used for a previous challenge
int target_function2(int i) {
  printf("You have visited the target_function2 function, well done!\n");
  printf("Argument : %d\n", i);
  return 43;
}

// used for a previous challenge
void target_function3(int* a) {
    printf("You have visited the target_function3 function, well done!\n");
    printf("Address: %p, value\n", a);
    *a = 42;
}

/* "Optimized" function we wish to run. The actual binary code is in part4/part.c.
int optimised() {
    return 1;
}
*/

// By default, the program executes target_function
// User input is asked to avoid busy waiting
int main(int argc, char** argv) {
    int var = 0;
    printf("This program will run target_function every 5 seconds.\n");
    
    while(1) {
        printf("Running main loop...\n");
        sleep(5);
        var = target_function();
        printf("Return value: %d\n", var);
    }

    return 0;
}
