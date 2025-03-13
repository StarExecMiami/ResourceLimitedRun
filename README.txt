To make it work ...

sudo ./InstallCGroup.bash
make
./ResourceLimitedRun -x
./ResourceLimitedRun -h

To build
+ gcc -o ResourceLimitedRun ResourceLimitedRun.c

Example run
+ ./ResourceLimitedRun -W 10 "Fibonacci 44"   
  will compute the 44th Fibonacci number within 10s limit

+ ./ResourceLimitedRun -W 10 "Fibonacci 100"   
  will not compute the 100th Fibonacci number within 10s limit

Other options
+ Right now, read the source code
