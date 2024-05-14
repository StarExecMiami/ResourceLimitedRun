#!/bin/tcsh

#----Run this under sudo to get everything set up. The first argument is the "user name"
#----and the second argument is the "user group name" for the cgroup directory under which
#----the cgroup for the programs that run are created, using PIDs to keep them separated.

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
#----Add the control groups so the lower group can too
echo "echo '+memory +cpu +cpuset' >> cgroup.subtree_control"
echo '+memory +cpu +cpuset' >> cgroup.subtree_control

#----Make the cgroup
echo "mkdir $CGroupUserName"
mkdir "$CGroupUserName"
#----Add the control groups
echo "echo '+memory +cpu +cpuset' >> $CGroupUserName/cgroup.subtree_control"
echo '+memory +cpu +cpuset' >> $CGroupUserName/cgroup.subtree_control
#----Make user own it all
echo "chown -R ${CGroupUserName}:${CGroupGroupName} $CGroupUserName"
chown -R "${CGroupUserName}:${CGroupGroupName}" $CGroupUserName

