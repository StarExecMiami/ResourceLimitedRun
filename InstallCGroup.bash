#!/bin/bash

#----Run this under sudo to get everything set up. The first argument is the "user name"
#----and the second argument is the "user group name" for the cgroup directory under which
#----the cgroup for the programs that run are created, using PIDs to keep them separated.

if [ "$#" -lt 2 ]; then
    CGroupUserName=`id -u -n`
    CGroupGroupName=`id -g -n`
else
    CGroupUserName="$1"
    CGroupGroupName="$2"
fi
echo "CGroupUserName is $CGroupUserName and CGroupGroupName is $CGroupGroupName"

cd /sys/fs/cgroup/ || exit 1
#----The top level cgroup.procs needs to be writable because the PIDs put in the lower cgroups
#----get propagated upwards
echo "chmod 666 cgroup.procs"
chmod 666 cgroup.procs || exit 1

#----Make the cgroup
echo "mkdir $CGroupUserName"
mkdir "$CGroupUserName" || exit 1
echo "chown -R ${CGroupUserName}:${CGroupGroupName} $CGroupUserName"
chown -R "${CGroupUserName}:${CGroupGroupName}" "$CGroupUserName" || exit 1

echo "cd $CGroupUserName"
cd "$CGroupUserName" || exit 1
#----Add the control groups
echo "echo '+memory +cpu +cpuset' >> cgroup.subtree_control"
echo '+memory +cpu +cpuset' >> cgroup.subtree_control || exit 1
#----Make user own the new stuff
echo "chown ${CGroupUserName}:${CGroupGroupName} *"
chown "${CGroupUserName}:${CGroupGroupName}" * || exit 1

