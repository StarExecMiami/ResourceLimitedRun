#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
//-----------------------------------------------------------------------------
long Fibonacci(int Index) {
    
    if (Index <= 1) {
        return(Index);
    } else {
        return(Fibonacci(Index-1) + Fibonacci(Index-2));
    }
}
//-----------------------------------------------------------------------------
int main(int argc,char *argv[]) {

    int numberToCompute;
    
    numberToCompute = atoi(argv[1]);
    printf("In Fibonacci with PID %d computing Fibonacci(%d)\n",getpid(),
numberToCompute);
    printf("Fibonacci of %d is %ld\n",numberToCompute,Fibonacci(numberToCompute)); 
    return(EXIT_SUCCESS);
}
//-----------------------------------------------------------------------------
