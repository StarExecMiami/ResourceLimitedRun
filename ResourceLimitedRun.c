#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <time.h>
//--------------------------------------------------------------------------------------------------
#define MAX_ARGS 20
#define MAX_STRING 1024
#define CGROUPS_DIR "/sys/fs/cgroup"

typedef char String[MAX_STRING];

typedef struct {
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    String programToExecute;
} OptionsType;
//--------------------------------------------------------------------------------------------------
//----Process options and fills out the struct with user's command line arguemnts
OptionsType ProcessOptions(int argc, char* argv[]) {

    int option;
    OptionsType inputOptions;

    inputOptions.CPULimit = -1;
    inputOptions.WCLimit = -1;
    inputOptions.RAMLimit = -1;
    strcpy(inputOptions.programToExecute,"");

    while ((option = getopt(argc, argv, "C:W:M:P:")) != -1) {
        switch(option) {
            case 'C':
                inputOptions.CPULimit = atoi(optarg);
                break;
            case 'W':
                inputOptions.WCLimit = atoi(optarg);
                break;
            case 'M':
                inputOptions.RAMLimit= atoi(optarg);
                break;
            case 'P':
                strcpy(inputOptions.programToExecute,optarg);
                break;
        }
    }
printf("CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",inputOptions.CPULimit,
inputOptions.WCLimit,inputOptions.RAMLimit,inputOptions.programToExecute);
    return inputOptions;
}
//--------------------------------------------------------------------------------------------------
void StartChildProgram(OptionsType Options,char * CGroupProcsFile) {

    int ChildPID;
    FILE* FilePointer;
    String ShellCommand;
    char *MyArgV[MAX_ARGS];
    int MyArgC;

    ChildPID = getpid();
    printf("IN CHILD with PID %d\n",ChildPID);
    sprintf(ShellCommand,"cat > %s",CGroupProcsFile);
//get PID of child to this file and add to cgroup.procs so it's 1st proc there
    if ((FilePointer = popen(ShellCommand,"w")) == NULL) {
        printf("ERROR: Could not open %s for writing\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    fprintf(FilePointer,"%d\n",ChildPID);
    fclose(FilePointer);

    MyArgC = 0;
    MyArgV[MyArgC] = strtok(Options.programToExecute," ");
    while (MyArgV[MyArgC++] != NULL) {
        MyArgV[MyArgC] = strtok(NULL," ");
    }
    execvp(MyArgV[0],MyArgV);
    perror("ERROR: Could not exec %s\n",Options.programToExecute);
    exit(EXIT_FAILURE);

}
//--------------------------------------------------------------------------------------------------
//----Fill an array of 100 PIDS from .procs
int NumberOfProcesses(char * CGroupProcsFile,int * PIDsInCGroup) {

    String ShellCommand;
    FILE* FilePointer;
    int NumberOfProccessesInCGroup;

    sprintf(ShellCommand,"cat %s",CGroupProcsFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        printf("ERROR: Could not open %s for reading\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    NumberOfProccessesInCGroup = 0;
    while (fscanf(FilePointer, "%d", &PIDsInCGroup[NumberOfProccessesInCGroup]) != EOF) {
//DEBUG printf("cgroup.procs contains: %d\n", PIDsInCGroup[NumberOfProccessesInCGroup]);
        NumberOfProccessesInCGroup++;
    }
    pclose(FilePointer);
    return NumberOfProccessesInCGroup; 
}
//--------------------------------------------------------------------------------------------------
//----Get CPU usage from cpu.stat
double CPUUsage(char * CPUStatFile) {

/*usage_usec 63559383
user_usec 63316412
system_usec 242971
core_sched.force_idle_usec 0
nr_periods 1276
nr_throttled 1235
throttled_usec 61601516
nr_bursts 0
burst_usec 0*/

    String ShellCommand;
    FILE* FilePointer;
    long MicroSeconds;

    sprintf(ShellCommand,"cat %s",CPUStatFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        printf("ERROR: Could not open %s for reading\n",CPUStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(FilePointer,"usage_usec %ld",&MicroSeconds);
    fclose(FilePointer);
    printf("CPUUsage is: %.2f\n",MicroSeconds/1000000.0);

    return MicroSeconds/1000000.0;
}
//--------------------------------------------------------------------------------------------------
void KillProcesses(int NumberOfProccesses,int * PIDs) {

    int PIDsindex;

    for (PIDsindex = 0; PIDsindex < NumberOfProccessesInCGroup; PIDsindex++) {
        printf("PARENT says: Killing process %d...\n",PIDsInCGroup[PIDsindex]);
        if (kill(PIDsInCGroup[PIDsindex],SIGKILL) != 0) {
            printf("ERROR: Could not kill PID %d\n",PIDsInCGroup[PIDsindex]);
        }
//----Reap zombies
//        while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
//            printf("PARENT says: Reaped process %d\n",ReapedPID);
//        }
    }
}
//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    String CGroupDir;
    String CGroupProcsFile;
    String CPUStatFile;
    String ShellCommand;
    int ParentPID;
    int ReapedPID;
    int NumberOfProccessesInCGroup;
    FILE* FilePointer;
    OptionsType Options;
    double CPUSeconds;

    printf("PROCESS THE OPTIONS\n");
    Options = ProcessOptions(argc,argv);
    int PIDsInCGroup[100];

    ParentPID = getpid();
    sprintf(CGroupDir,"%s/%d",CGROUPS_DIR,ParentPID);
    sprintf(CGroupProcsFile,"%s/%s",CGroupDir,"cgroup.procs");
    sprintf(CPUStatFile,"%s/%s",CGroupDir,"cpu.stat");

    sprintf(ShellCommand,"mkdir %s",CGroupDir);
printf("About to do %s\n",ShellCommand);
    system(ShellCommand);
    sprintf(ShellCommand,"chown -R %d:%d %s",getuid(),getgid(),CGroupDir);
printf("About to do %s\n",ShellCommand);
    system(ShellCommand);

    if ((ChildPID = fork()) == -1) {
        perror("Could not fork");
        exit(EXIT_FAILURE);
    }

    if (ChildPID == 0) {
        StartChildProgram(Options,CGroupProcsFile);
    } else {
        printf("IN PARENT with PID %d\n",ParentPID);
        printf("Extra sleep 1s\n");
        sleep(1);
//DEBUG printf("Initial ps in parent:\n");
//DEBUG system("ps"); 
        
        NumberOfProccessesInCGroup = NumberOfProcesses(CGroupProcsFile,PIDsInCGroup);
        while (NumberOfProccessesInCGroup > 0) {
            printf("PARENT says: The PIDs are now: ");
            for (PIDsindex = 0; PIDsindex < NumberOfProccessesInCGroup; PIDsindex++) {
                    printf("%d ",PIDsInCGroup[PIDsindex]);
            }
            printf("\n");
//----Check CPU: if over limit, kill cgroup, else sleep
            CPUSeconds = CPUUsage(CPUStatFile);
            if (Options.CPULimit > 0 && CPUSeconds > Options.CPULimit) {
                KillProcesses(NumberOfProccessesInCGroup,PIDsInCGroup);
            } else {
                sleep(1);
            }
            NumberOfProccessesInCGroup = NumberOfProcesses(CGroupProcsFile,PIDsInCGroup);
//DEBUG printf("PARENT says: Number of processes is: %d\n", NumberOfProccessesInCGroup);
        }
        printf("No processes left\n");
//----Reap zombies child (should not exist)
        while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
            printf("PARENT says: Should not have reaped process %d\n",ReapedPID);
        }
//DEBUG printf("ps after waitPID:\n");
//DEBUG system("ps");

//----Once cgroup.procs file is empty, print out the CPU usage
        printf("PARENT says: Final CPU usage: %.2f\n",CPUUsage(CPUStatFile));
    }

    sprintf(ShellCommand,"rmdir %s",CGroupDir);
printf("About to do %s\n",ShellCommand);
    system(ShellCommand);

    return(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------

