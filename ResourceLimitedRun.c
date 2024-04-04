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

#define MAX_ARGS 20

#define MAX_STRING 1024

//struct for each of user's command line arguments
typedef char String[MAX_STRING];

typedef struct {
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    String programToExecute;
} OptionsType;

//function that reads the number of processes in processes in .procs by checking pids.current
//make a parameter an array of 100 integers that will be filled with PIDS from .procs
int NumberOfProcesses(int* arrayOfPIDs) {

    FILE* filePointer;
    int numberOfProcesses;

//DEBUG printf("pids.current contains: ");
//DEBUG system("cat /sys/fs/cgroup/mygrp/pids.current");
    filePointer = popen("cat /sys/fs/cgroup/mygrp/cgroup.procs", "r");
    if (filePointer == NULL) {
        printf("Failed to run command\n" );
        exit(EXIT_FAILURE);
    }
//DEBUG printf("Take a look at cgroup.procs:\n");
//DEBUG system("cat /sys/fs/cgroup/mygrp/cgroup.procs");
    numberOfProcesses = 0;
    while (fscanf(filePointer, "%d", &arrayOfPIDs[numberOfProcesses]) != EOF) {
//DEBUG printf("cgroup.procs contains: %d\n", arrayOfPIDs[numberOfProcesses]);
        numberOfProcesses++;
    }
    pclose(filePointer);
    return numberOfProcesses; 
}

//function that gets CPU usage from cpu.stat
double CPUUsage() {

/*usage_usec 63559383
user_usec 63316412
system_usec 242971
core_sched.force_idle_usec 0
nr_periods 1276
nr_throttled 1235
throttled_usec 61601516
nr_bursts 0
burst_usec 0*/

    FILE* filePointer;
    long MicroSeconds;

    filePointer = popen(" cat /sys/fs/cgroup/mygrp/cpu.stat", "r");
    fscanf(filePointer, "usage_usec %ld", &MicroSeconds);
    fclose(filePointer);
    printf("CPUUsage is: %.2f\n", MicroSeconds/1000000.0);

    return MicroSeconds/1000000.0;
}

//function that gets name of the file the user want to run 
void GetProgramToExecute(int argc, char* argv[], OptionsType* Options) {

    int bytesWritten = 0;
    char* executionProgram = (char*)(Options->programToExecute);
    int i;
    for (i = 1; i<argc; i++) {
        if (i<argc-1) {
            strcpy(executionProgram+bytesWritten, argv[i+1]);
            bytesWritten = bytesWritten + strlen(argv[i+1]) + 1;
            executionProgram[bytesWritten] =  ' ';
        }
    }
}

//function that processes options and fills out the struct with user's command line arguemnts
//NOT READING INPUT CORRECTLY
OptionsType ProcessOptions(int argc, char* argv[]) {

    int option;
    int i = 1;
    OptionsType inputOptions;

    inputOptions.CPULimit = -1;
    inputOptions.WCLimit = -1;
    inputOptions.RAMLimit = -1;
    strcpy(inputOptions.programToExecute,"");

    while(((option = getopt(argc, argv, "C:W:M:P:")) != -1) && i < argc) {
        switch(option) {
            case 'C':
                inputOptions.CPULimit = atoi(/*argv[i+1]*/optarg);
                break;
            case 'W':
                inputOptions.WCLimit = atoi(argv[i+1]);
                break;
            case 'M':
                inputOptions.RAMLimit= atoi(argv[i+1]);
                break;
            case 'P':
                strcpy(inputOptions.programToExecute, optarg);
                break;
        }
        i++;
    }
printf("CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",inputOptions.CPULimit,
inputOptions.WCLimit,inputOptions.RAMLimit,inputOptions.programToExecute);
    return inputOptions;
}

int main(int argc, char* argv[]) {

    int thisPID;
    pid_t ChildPID;
    int numberOfProcesses;
    char *MyArgV[MAX_ARGS];
    int MyArgC;
    FILE* file;
    OptionsType Options;
    double CPUSeconds;

    printf("PROCESS THE OPTIONS\n");
    Options = ProcessOptions(argc,argv);
    int arrayOfPIDs[100];
    int PIDsindex;

    if ((ChildPID = fork()) == -1) {
        perror("Could not fork");
        exit(EXIT_FAILURE);
    }

    if(ChildPID == 0) {
        thisPID = getpid();
        printf("IN CHILD with PID %d\n",thisPID);
//get PID of child to this file and add to cgroup.procs so it's 1st proc there
        file = popen("cat > /sys/fs/cgroup/mygrp/cgroup.procs", "w");
        if (file == NULL) {
            perror("Could not open file");
            exit(EXIT_FAILURE);
        }
        fprintf(file, "%d\n", thisPID);
        fclose(file);

//DEBUG printf("CHILD check cgroup.procs: \n");
//DEBUG system("cat /sys/fs/cgroup/mygrp/cgroup.procs");
//DEBUG printf("This is ps in the child before exec\n");
//DEBUG system("ps");
        
        MyArgC = 0;
        MyArgV[MyArgC] = strtok(Options.programToExecute," ");
        while (MyArgV[MyArgC++] != NULL) {
            MyArgV[MyArgC] = strtok(NULL," ");
        }
        execvp(MyArgV[0],MyArgV);
        perror("Error in exec");
        exit(EXIT_FAILURE);
    } else {
        printf("IN PARENT with PID %d\n",getpid());
        printf("Extra sleep:\n");
        sleep(1);
//DEBUG printf("Initial ps in parent:\n");
//DEBUG system("ps"); 
        
//DEBUG printf("Check cgroup.procs IN PARENT 2: \n");
//DEBUG system("cat /sys/fs/cgroup/mygrp/cgroup.procs"); //CGROUP.PROCS IS EMPTY IN PARENT
        
        numberOfProcesses = NumberOfProcesses(arrayOfPIDs);
        while (numberOfProcesses > 0) {
            printf("PARENT says: The PIDs are now: ");
            for (PIDsindex = 0; PIDsindex < numberOfProcesses; PIDsindex++) {
                    printf("%d ",arrayOfPIDs[PIDsindex]);
            }
            printf("\n");
//----Check CPU: if over limit, kill cgroup, else sleep
            CPUSeconds = CPUUsage();
            if (Options.CPULimit > 0 && CPUSeconds > Options.CPULimit) {
//----Kill cgroup
                for (PIDsindex = 0; PIDsindex < numberOfProcesses; PIDsindex++) {
                    printf("PARENT says: Killing process %d...\n", arrayOfPIDs[PIDsindex]);
                // FIX THIS
                }
            } else {
                sleep(1);
            }
//----Reap zombies
            while ((thisPID = waitpid(-1,NULL,WNOHANG)) > 0) {
                printf("PARENT says: Reaped process %d\n",thisPID);
            }
            numberOfProcesses = NumberOfProcesses(arrayOfPIDs);
//DEBUG printf("PARENT says: Number of processes is: %d\n", numberOfProcesses);
        }
        printf("No processes left\n");
//----Reap zombies 
        while ((thisPID = waitpid(-1,NULL,WNOHANG)) > 0) {
            printf("PARENT says: Should not have reaped process %d\n",thisPID);
        }
//DEBUG printf("Ps command after waitPID:\n");
//DEBUG system("ps");

//once cgroup.procs file is empty, print out the CPU usage
        printf("PARENT says: Final CPU usage: %.2f\n",CPUUsage());
        system("cat /sys/fs/cgroup/mygrp/cpu.stat");
    }

    return(EXIT_SUCCESS);
}


