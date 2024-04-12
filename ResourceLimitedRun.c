#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <stdio.h>
#include <time.h>
//--------------------------------------------------------------------------------------------------
#define TRUE 1
#define FALSE 0
#define MAX_ARGS 20
#define MAX_STRING 1024
#define MAX_PIDS 1024
#define CGROUPS_DIR "/sys/fs/cgroup"

typedef char String[MAX_STRING];

typedef struct {
    int Verbosity;
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    String ProgramToControl;
} OptionsType;
//--------------------------------------------------------------------------------------------------
void MyPrintf(OptionsType Options,int RequiredVerbosity,char * Format,...) {

    va_list ThingsToPrint;
    int Result;

    if (Options.Verbosity >= RequiredVerbosity) {
        va_start(ThingsToPrint,Format);
        Result = vprintf(Format,ThingsToPrint);
        va_end(ThingsToPrint);
    }
}
//--------------------------------------------------------------------------------------------------
void MySnprintf(OptionsType Options,char * PrintIntoHere,int LengthOfHere,char * Format,...) {

    va_list ThingsToPrint;
    int Result;

    va_start(ThingsToPrint,Format);
    Result = vsnprintf(PrintIntoHere,LengthOfHere,Format,ThingsToPrint);
    va_end(ThingsToPrint);
}
//--------------------------------------------------------------------------------------------------
char * SignalName(int Signal) {

    switch (Signal) {
        case SIGINT:
            return("SIGINT");
            break;
        case SIGXCPU:
            return("SIGXCPU");
            break;
        case SIGKILL:
            return("SIGKILL");
            break;
        default:
            return("UNKNOWN");
            break;
    }
}
//--------------------------------------------------------------------------------------------------
//----Process options and fills out the struct with user's command line arguemnts
OptionsType ProcessOptions(int argc, char* argv[]) {

    int option;
    OptionsType Options;

    Options.Verbosity = 2;
    Options.CPULimit = -1;
    Options.WCLimit = -1;
    Options.RAMLimit = -1;
    strcpy(Options.ProgramToControl,"");

    while ((option = getopt(argc, argv, "v:C:W:M:P:")) != -1) {
        switch(option) {
            case 'v':
                Options.Verbosity = atoi(optarg);
                break;
            case 'C':
                Options.CPULimit = atoi(optarg);
                break;
            case 'W':
                Options.WCLimit = atoi(optarg);
                break;
            case 'M':
                Options.RAMLimit= atoi(optarg);
                break;
            case 'P':
                strcpy(Options.ProgramToControl,optarg);
                break;
        }
    }
    MyPrintf(Options,2,"CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",Options.CPULimit,
Options.WCLimit,Options.RAMLimit,Options.ProgramToControl);
    return(Options);
}
//--------------------------------------------------------------------------------------------------
void StartChildProgram(OptionsType Options,char * CGroupProcsFile) {

    int ChildPID;
    FILE* FilePointer;
    String ShellCommand;
    String ErrorMessage;
    char *MyArgV[MAX_ARGS];
    int MyArgC;

    ChildPID = getpid();
    MyPrintf(Options,3,"In CHILD with PID %d\n",ChildPID);
    MySnprintf(Options,ShellCommand,MAX_STRING,"cat > %s",CGroupProcsFile);
    if ((FilePointer = popen(ShellCommand,"w")) == NULL) {
        MyPrintf(Options,0,"ERROR: Could not open %s for writing\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    fprintf(FilePointer,"%d\n",ChildPID);
    fclose(FilePointer);

    MyArgC = 0;
    MyArgV[MyArgC] = strtok(Options.ProgramToControl," ");
    while (MyArgV[MyArgC++] != NULL) {
        MyArgV[MyArgC] = strtok(NULL," ");
    }
    execvp(MyArgV[0],MyArgV);
    MyPrintf(Options,0,"ERROR: Could not exec %s\n",Options.ProgramToControl);
    exit(EXIT_FAILURE);
}
//--------------------------------------------------------------------------------------------------
//----Fill an array of PIDS from .procs
int NumberOfProcesses(OptionsType Options,char * CGroupProcsFile,int * PIDsInCGroup) {

    String ShellCommand;
    FILE* FilePointer;
    int NumberOfProccessesInCGroup;

    MySnprintf(Options,ShellCommand,MAX_STRING,"cat %s",CGroupProcsFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,0,"ERROR: Could not open %s for reading\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    NumberOfProccessesInCGroup = 0;
    MyPrintf(Options,3,"RLR says: The PIDs are now:");
    while (NumberOfProccessesInCGroup < MAX_PIDS &&
fscanf(FilePointer,"%d",&PIDsInCGroup[NumberOfProccessesInCGroup]) != EOF) {
        MyPrintf(Options,3," %d",PIDsInCGroup[NumberOfProccessesInCGroup]);
        NumberOfProccessesInCGroup++;
    }
    MyPrintf(Options,3,"\n");
    pclose(FilePointer);
    if (NumberOfProccessesInCGroup == MAX_PIDS) {
        MyPrintf(Options,0,"ERROR: Ran out of space for PIDs when counting NumberOfProcesses\n");
    }
    return(NumberOfProccessesInCGroup); 
}
//--------------------------------------------------------------------------------------------------
//----Get WC usage 
double WCUsage(OptionsType Options) {

    static double Start = 0;
    double Now;
    struct timespec TheTime;
    double Seconds;

    clock_gettime(CLOCK_MONOTONIC,&TheTime);
    Now = TheTime.tv_sec + TheTime.tv_nsec/1000000000.0;
    if (Start == 0) {
        Start = Now;
    }
    Seconds = Now-Start;
    MyPrintf(Options,3,"RLR says: WCUsage is %.2fs\n",Seconds);
    return(Seconds);
}
//--------------------------------------------------------------------------------------------------
//----Get CPU usage from cpu.stat
double CPUUsage(OptionsType Options,char * CPUStatFile) {

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

    MySnprintf(Options,ShellCommand,MAX_STRING,"cat %s",CPUStatFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,0,"ERROR: Could not open %s for reading\n",CPUStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(FilePointer,"usage_usec %ld",&MicroSeconds);
    fclose(FilePointer);
    MyPrintf(Options,3,"RLR says: CPUUsage is %.2fs\n",MicroSeconds/1000000.0);

    return(MicroSeconds/1000000.0);
}
//--------------------------------------------------------------------------------------------------
void KillProcesses(OptionsType Options,int NumberOfProccesses,int * PIDs,int WhichSignal) {

    int PIDsindex;
    static int SignalsSent[4][MAX_PIDS]; //----0 for PID, 1 for SIGINT, 2 for SIGXCPU, 3 for SIGKILL
    int SignalsSentRow;
    int SentIndex;
    int SendTheSignal;

    if (WhichSignal == SIGINT) {
        SignalsSentRow = 1;
    } else if (WhichSignal == SIGXCPU) {
        SignalsSentRow = 2;
    } else {
        SignalsSentRow = 3;
    }

    for (PIDsindex = 0; PIDsindex < NumberOfProccesses; PIDsindex++) {
//----See what we have sent before to this PID
        SentIndex = 0;
        while (SentIndex < MAX_PIDS && SignalsSent[0][SentIndex] > 0 &&
SignalsSent[0][SentIndex] != PIDs[PIDsindex]) {
        MyPrintf(Options,4,
"For PID %d already sent SIGINT %d and SIGXCPU %d and SIGKILL %d\n",SignalsSent[0][SentIndex],
SignalsSent[1][SentIndex],SignalsSent[2][SentIndex],SignalsSent[3][SentIndex]);
            SentIndex++;
        }
        SendTheSignal = FALSE;
        if (SignalsSent[0][SentIndex] == 0) {
            SignalsSent[0][SentIndex] = PIDs[PIDsindex];
            SendTheSignal = TRUE;
        }
//----If have not sent a gentle signal yet, do it
        if (! SignalsSent[SignalsSentRow][SentIndex]) {
            SignalsSent[SignalsSentRow][SentIndex] = TRUE;
            SendTheSignal = TRUE;
//----If have sent a gentle signal before, KILL!
        } else if (! SignalsSent[3][SentIndex]) {
            MyPrintf(Options,2,"RLR says: Upgrading signal from %s to %s\n",
SignalName(WhichSignal),SignalName(SIGKILL));
            WhichSignal = SIGKILL;
            SignalsSent[3][SentIndex] = TRUE;
            SendTheSignal = TRUE;
        }
        if (SendTheSignal) {
            MyPrintf(Options,2,"RLR says: Killing PID %d with %s ...\n",PIDs[PIDsindex],
SignalName(WhichSignal));
            if (kill(PIDs[PIDsindex],WhichSignal) != 0) {
                MyPrintf(Options,0,"ERROR: Could not kill PID %d with %s\n",PIDs[PIDsindex],
SignalName(WhichSignal));
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------
void MonitorDescendantProcesses(OptionsType Options,char * CGroupProcsFile,char * CPUStatFile) {

    int NumberOfProccessesInCGroup;
    int DoneSomeKilling;
    int ReapedPID;
    int PIDsInCGroup[MAX_PIDS];

//DEBUG printf("Initial ps in parent:\n");
//DEBUG system("ps"); 
//----Start the clock
    WCUsage(Options);
//----Watch the processes
    NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupProcsFile,PIDsInCGroup);
    while (NumberOfProccessesInCGroup > 0) {
        MyPrintf(Options,2,"RLR says: Number of processes is: %d\n",NumberOfProccessesInCGroup);
        DoneSomeKilling = FALSE;
        if (Options.CPULimit > 0 && CPUUsage(Options,CPUStatFile) > Options.CPULimit) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGINT);
            DoneSomeKilling = TRUE;
        }
        if (Options.WCLimit > 0 && WCUsage(Options) > Options.WCLimit) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGXCPU);
            DoneSomeKilling = TRUE;
        }
//----Reap zombies
        sleep(1);
        if (DoneSomeKilling) {
            while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
                MyPrintf(Options,2,"RLR says: Reaped killed or exited process %d\n",ReapedPID);
            }
        }
        NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupProcsFile,PIDsInCGroup);
    }
    MyPrintf(Options,2,"RLR says: No processes left\n");
//----Reap zombies child (should not exist)
    while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
        MyPrintf(Options,2,"RLR says: Reaped exited process %d\n",ReapedPID);
    }
//DEBUG printf("ps after waitPID:\n");
//DEBUG system("ps");
}
//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    OptionsType Options;
    String CGroupDir;
    String CGroupProcsFile;
    String CPUStatFile;
    String ShellCommand;
    int ParentPID;
    int ChildPID;

    Options = ProcessOptions(argc,argv);

    ParentPID = getpid();
    MySnprintf(Options,CGroupDir,MAX_STRING,"%s/%d",CGROUPS_DIR,ParentPID);
    MySnprintf(Options,CGroupProcsFile,MAX_STRING,"%s/%s",CGroupDir,"cgroup.procs");
    MySnprintf(Options,CPUStatFile,MAX_STRING,"%s/%s",CGroupDir,"cpu.stat");

    MySnprintf(Options,ShellCommand,MAX_STRING,"mkdir %s",CGroupDir);
    MyPrintf(Options,3,"RLR says: About to do %s\n",ShellCommand);
    system(ShellCommand);
    MySnprintf(Options,ShellCommand,MAX_STRING,"chown -R %d:%d %s",getuid(),getgid(),CGroupDir);
    MyPrintf(Options,3,"RLR says: About to do %s\n",ShellCommand);
    system(ShellCommand);

    if ((ChildPID = fork()) == -1) {
        MyPrintf(Options,0,"ERROR: Could not fork()");
        exit(EXIT_FAILURE);
    }

    if (ChildPID == 0) {
        StartChildProgram(Options,CGroupProcsFile);
    } else {
        MyPrintf(Options,3,"In RLR with PID %d\n",ParentPID);
        MyPrintf(Options,3,
"RLR says: Sleep 1s to allow child %d to create and add itself to a cgroup\n",ChildPID);
    sleep(1);
        MonitorDescendantProcesses(Options,CGroupProcsFile,CPUStatFile);
        MyPrintf(Options,1,"RLR says: Final CPU usage: %.2f\n",CPUUsage(Options,CPUStatFile));
//----WC is 1s too high due to sleep in loop to allow processes to die
        MyPrintf(Options,1,"RLR says: Final WC  usage: %.2f\n",WCUsage(Options) - 1.0);
    }

    MySnprintf(Options,ShellCommand,MAX_STRING,"rmdir %s",CGroupDir);
    MyPrintf(Options,3,"RLR says: About to do %s\n",ShellCommand);
    system(ShellCommand);

    return(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------
