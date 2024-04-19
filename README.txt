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

