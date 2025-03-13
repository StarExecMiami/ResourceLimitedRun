To make it work without needing sudo ...
+ cd /sys/fs/cgroup/
+ sudo chmod 666 cgroup.procs
+ sudo mkdir tptp
+ sudo chown -R tptp:tptp tptp
+ echo "+cpu +cpuset" | sudo tee cgroup.subtree_control   // Also puts cpu in tptp/cgroup.subtree_control
+ cd tptp ; echo "+memory +cpu + cupset" >> cgroup.subtree_control   NEED TO MAKE THIS DEFAULT
+ sudo chown tptp:tptp *   // Make tptp own the new stuff
+ In ResourceLimitedRun.c
      #define CGROUPS_DIR "/sys/fs/cgroup/tptp"

To build
+ gcc -o ResourceLimitedRun ResourceLimitedRun.c

Example run
+ ./ResourceLimitedRun -W 10 "Fibonacci 44"   
  will compute the 44th Fibonacci number within 10s limit

+ ./ResourceLimitedRun -W 10 "Fibonacci 100"   
  will not compute the 100th Fibonacci number within 10s limit

Other options
+ Right now, read the source code
