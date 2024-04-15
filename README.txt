To make it work without needing sudo ...
+ cd /sys/fs/cgroup/
+ sudo chmod 666 cgroup.procs
+ sudo mkdir tptp
+ sudo chown -R tptp:tptp tptp
+ cd tptp ; echo "+memory" >> cgroup.subtree_control   NEED TO MAKE THIS DEFAULT
+ In ResourceLimitedRun.c
      #define CGROUPS_DIR "/sys/fs/cgroup/tptp"

