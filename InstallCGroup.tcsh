#!/bin/tcsh

#----Run this under sudo to get everything set up. The single optional argument is the "user name"
#----for the cgroup directory under which the cgroup for the programs that run are greated, using
#----PIDs to keep them separated.

if ($#argv < 2) then
    set CGroupUserName=`id -u -n`
    set CGroupGroupName=`id -g -n`
else
    set CGroupUserName = "$1"
    set CGroupGroupName = "$2"
endif
echo "CGroupUserName is $CGroupUserName and CGroupGroupName is $CGroupGroupName"

cd /sys/fs/cgroup/
#----The top level cgroup.procs needs to be writable because the PIDs put in the lower cgroups
#----get propagated upwards
echo "chmod 666 cgroup.procs"
chmod 666 cgroup.procs

#----Make the cgroup
echo "mkdir $CGroupUserName"
mkdir "$CGroupUserName"
echo "chown -R ${CGroupUserName}:${CGroupGroupName} $CGroupUserName"
chown -R "${CGroupUserName}:${CGroupGroupName}" "$CGroupUserName"

echo "cd $CGroupUserName"
cd "$CGroupUserName"
#----Add the control groups
echo "echo '+memory +cpu +cpuset' >> cgroup.subtree_control"
echo '+memory +cpu +cpuset' >> cgroup.subtree_control
#----Make user own the new stuff
echo "chown ${CGroupUserName}:${CGroupGroupName} *"
chown "${CGroupUserName}:${CGroupGroupName}" *   

