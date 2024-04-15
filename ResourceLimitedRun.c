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
#define VERBOSITY_ERROR 0
#define VERBOSITY_NONE 1
#define VERBOSITY_RESOURCE_USAGE 2
#define VERBOSITY_BIG_STEPS 3
#define VERBOSITY_RLR_ACTIONS 4
#define VERBOSITY_ALL 5
#define VERBOSITY_DEBUG 6
#define VERBOSITY_DEFAULT VERBOSITY_RESOURCE_USAGE

#define BOOLEAN int
#define TRUE 1
#define FALSE 0

#define MAX_ARGS 20
#define MAX_STRING 1024
#define MAX_PIDS 1024

#define CGROUPS_DIR "/sys/fs/cgroup/tptp"

typedef char String[MAX_STRING];

typedef struct {
    int Verbosity;
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    BOOLEAN TimeStamps;
    String ProgramToControl;
} OptionsType;
//--------------------------------------------------------------------------------------------------
static BOOLEAN GlobalInterrupted;
//--------------------------------------------------------------------------------------------------
void MyPrintf(OptionsType Options,int RequiredVerbosity,char * Format,...) {

    va_list ThingsToPrint;
    int Result;
    String FinalFormat;

    if (Options.Verbosity >= RequiredVerbosity) {
        switch (RequiredVerbosity) {
            case VERBOSITY_ERROR:
                strcpy(FinalFormat,"ERROR: ");
                strcat(FinalFormat,Format);
                break;
            case VERBOSITY_RESOURCE_USAGE:
                strcpy(FinalFormat,"%% ");
                strcat(FinalFormat,Format);
                break;
            case VERBOSITY_RLR_ACTIONS:
            case VERBOSITY_BIG_STEPS:
                strcpy(FinalFormat,"RLR says: ");
                strcat(FinalFormat,Format);
                break;
            default:
                strcpy(FinalFormat,Format);
                break;
        }
        va_start(ThingsToPrint,Format);
        Result = vprintf(FinalFormat,ThingsToPrint);
        fflush(stdout);
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
        case SIGALRM:
            return("SIGALRM");
            break;
        case SIGTERM:
            return("SIGTERM");
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

    Options.Verbosity = VERBOSITY_DEFAULT;
    Options.CPULimit = -1;
    Options.WCLimit = -1;
    Options.RAMLimit = -1;
    Options.TimeStamps = FALSE;
    strcpy(Options.ProgramToControl,"");

    while ((option = getopt(argc, argv, "tv:C:W:M:P:")) != -1) {
        switch(option) {
            case 't':
                Options.TimeStamps = TRUE;
                break;
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
    MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,
"CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",Options.CPULimit,
Options.WCLimit,Options.RAMLimit,Options.ProgramToControl);
    return(Options);
}
//--------------------------------------------------------------------------------------------------
//----Fill an array of PIDS from .procs
int NumberOfProcesses(OptionsType Options,char * CGroupProcsFile,int * PIDsInCGroup) {

    String ShellCommand;
    FILE* FilePointer;
    int NumberOfProccessesInCGroup;

    MySnprintf(Options,ShellCommand,MAX_STRING,"cat %s",CGroupProcsFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    NumberOfProccessesInCGroup = 0;
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"The PIDs are now:");
    while (NumberOfProccessesInCGroup < MAX_PIDS &&
fscanf(FilePointer,"%d",&PIDsInCGroup[NumberOfProccessesInCGroup]) != EOF) {
//----Have to do by hand for pretty output
        if (Options.Verbosity >= VERBOSITY_RLR_ACTIONS) {
            printf(" %d",PIDsInCGroup[NumberOfProccessesInCGroup]);
        }
        NumberOfProccessesInCGroup++;
    }
    if (Options.Verbosity >= VERBOSITY_RLR_ACTIONS) {
        printf("\n");
    }
    pclose(FilePointer);
    if (NumberOfProccessesInCGroup == MAX_PIDS) {
        MyPrintf(Options,VERBOSITY_ERROR,"Ran out of PID space when counting NumberOfProcesses\n");
    }
    return(NumberOfProccessesInCGroup); 
}
//--------------------------------------------------------------------------------------------------
//----Get memory usage in MiB
double RAMUsage(OptionsType Options,char * RAMStatFile) {

    String ShellCommand;
    FILE* FilePointer;
    long Bytes;

    MySnprintf(Options,ShellCommand,MAX_STRING,"cat %s",RAMStatFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",RAMStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(FilePointer,"%ld",&Bytes);
    pclose(FilePointer);
    return(Bytes/1048576.0);
}
//--------------------------------------------------------------------------------------------------
//----Get WC usage 
double WCUsage(OptionsType Options) {

    static double Start = -1.0;
    double Now;
    struct timespec TheTime;
    double Seconds;

    clock_gettime(CLOCK_MONOTONIC,&TheTime);
    Now = TheTime.tv_sec + TheTime.tv_nsec/1000000000.0;
    if (Start < 0.0) {
        Start = Now;
        MyPrintf(Options,VERBOSITY_ALL,"WC offset is %.2fs\n",Start);
    }
    Seconds = Now-Start;
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"WCUsage is %.2fs\n",Seconds);
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
    static double StartMicroSeconds = -1.0;

    MySnprintf(Options,ShellCommand,MAX_STRING,"cat %s",CPUStatFile);
    if ((FilePointer = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",CPUStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(FilePointer,"usage_usec %ld",&MicroSeconds);
    pclose(FilePointer);
    if (StartMicroSeconds < 0.0) {
        StartMicroSeconds = MicroSeconds;
        MyPrintf(Options,VERBOSITY_ALL,"CPU offset is %.2fs\n",StartMicroSeconds/1000000.0);
    }
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"CPUUsage is %.2fs\n",
(MicroSeconds-StartMicroSeconds)/1000000.0);

    return(MicroSeconds/1000000.0);
}
//--------------------------------------------------------------------------------------------------
void KillProcesses(OptionsType Options,int NumberOfProccesses,int * PIDs,int WhichSignal) {

#define PID_ROW 0
#define SIGXCPU_ROW 1
#define SIGALRM_ROW 2
#define SIGTERM_ROW 3
#define SIGINT_ROW 4
#define SIGKILL_ROW 5
    static int SignalsSent[SIGKILL_ROW+1][MAX_PIDS]; 
    int PIDsindex;
    int SignalsSentRow;
    int SentIndex;
    BOOLEAN SendTheSignal;

    if (WhichSignal == SIGXCPU) {
        SignalsSentRow = SIGXCPU_ROW;
    } else if (WhichSignal == SIGALRM) {
        SignalsSentRow = SIGALRM_ROW;
    } else if (WhichSignal == SIGTERM) {
        SignalsSentRow = SIGTERM_ROW;
    } else if (WhichSignal == SIGINT) {
        SignalsSentRow = SIGINT_ROW;
    } else {
        SignalsSentRow = SIGKILL_ROW;
    }

    for (PIDsindex = 0; PIDsindex < NumberOfProccesses; PIDsindex++) {
//----See what we have sent before to this PID
        SentIndex = 0;
        while (SentIndex < MAX_PIDS && SignalsSent[PID_ROW][SentIndex] > 0 &&
SignalsSent[PID_ROW][SentIndex] != PIDs[PIDsindex]) {
            MyPrintf(Options,VERBOSITY_ALL,
"For PID %d already sent SIGXCPU %d and SIGALRM %d and SIGTERM %d and SIGKILL %d\n",
SignalsSent[PID_ROW][SentIndex],SignalsSent[SIGXCPU_ROW][SentIndex],
SignalsSent[SIGALRM_ROW][SentIndex],SignalsSent[SIGTERM_ROW][SentIndex],
SignalsSent[SIGKILL_ROW][SentIndex]);
            SentIndex++;
        }
        SendTheSignal = FALSE;
//----If PID has never been sent a signal, add a column for it
        if (SignalsSent[PID_ROW][SentIndex] == 0) {
            SignalsSent[PID_ROW][SentIndex] = PIDs[PIDsindex];
            SendTheSignal = TRUE;
        }
//----If have not sent a gentle signal yet, do it
        if (! SignalsSent[SignalsSentRow][SentIndex]) {
            SignalsSent[SignalsSentRow][SentIndex] = TRUE;
            SendTheSignal = TRUE;
//----If have sent a gentle signal before, KILL!
        } else if (! SignalsSent[SIGKILL_ROW][SentIndex]) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Upgrading signal from %s to %s\n",
SignalName(WhichSignal),SignalName(SIGKILL));
            WhichSignal = SIGKILL;
            SignalsSent[SIGKILL_ROW][SentIndex] = TRUE;
            SendTheSignal = TRUE;
        }
        if (SendTheSignal) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Killing PID %d with %s ...\n",
PIDs[PIDsindex],SignalName(WhichSignal));
            if (kill(PIDs[PIDsindex],WhichSignal) != 0) {
                MyPrintf(Options,VERBOSITY_ERROR,"Could not kill PID %d with %s\n",
PIDs[PIDsindex],SignalName(WhichSignal));
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------
void StartChildProgram(OptionsType Options,char * CGroupProcsFile,int PIDOfRLR) {

    int ChildPID;
    FILE* FilePointer;
    String ShellCommand;
    char *MyArgV[MAX_ARGS];
    int MyArgC;

    ChildPID = getpid();
    MyPrintf(Options,VERBOSITY_DEBUG,"In CHILD with PID %d\n",ChildPID);
    MySnprintf(Options,ShellCommand,MAX_STRING,"cat > %s",CGroupProcsFile);
    if ((FilePointer = popen(ShellCommand,"w")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for writing\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    fprintf(FilePointer,"%d\n",ChildPID);
    pclose(FilePointer);

    MyArgC = 0;
    MyArgV[MyArgC] = strtok(Options.ProgramToControl," ");
    while (MyArgV[MyArgC++] != NULL) {
        MyArgV[MyArgC] = strtok(NULL," ");
    }
//----Tell RLR the timing should start
    MyPrintf(Options,VERBOSITY_DEBUG,
"Child %d tells parent %d and grandparent %d to start monitoring\n",ChildPID,getppid(),PIDOfRLR);
    if (kill(getppid(),SIGCONT) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not signal parent %d to start timing\n",
getppid());
    }
    if (kill(PIDOfRLR,SIGCONT) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not signal grandparent %d to start timing\n",
PIDOfRLR);
    }

    MyPrintf(Options,VERBOSITY_DEBUG,"Child %d about to execvp %s\n",ChildPID,
Options.ProgramToControl);
//----Note all signal handling is reset
    execvp(MyArgV[0],MyArgV);
    MyPrintf(Options,VERBOSITY_ERROR,"Child %d could not execvp %s\n",ChildPID,
Options.ProgramToControl);
    exit(EXIT_FAILURE);
}
//--------------------------------------------------------------------------------------------------
void StartChildProcessing(OptionsType Options,char * CGroupProcsFile,char * CPUStatFile,
int PIDOfRLR) {

    int ChildPID;
    int Pipe[2];
    FILE* FilePointer;
    String ChildOutput;

    if (pipe(Pipe) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not create pipe to catch child output");
        exit(EXIT_FAILURE);
    }

    if ((ChildPID = fork()) == -1) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not fork() for child program");
        exit(EXIT_FAILURE);
    }

    if (ChildPID == 0) {
        close(Pipe[0]);
        dup2(Pipe[1],STDOUT_FILENO);
        dup2(Pipe[1],STDERR_FILENO);
        setbuf(stdout,NULL);
        StartChildProgram(Options,CGroupProcsFile,PIDOfRLR);
    } else {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,
"Wait for child %d to create and add itself to a cgroup\n",ChildPID);
        pause();
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Child %d has started\n",ChildPID);
//----Start the clocks, hopefully about the same time as the parent
        CPUUsage(Options,CPUStatFile);
        WCUsage(Options);
        close(Pipe[1]);
        FilePointer = fdopen(Pipe[0],"r");
        MyPrintf(Options,VERBOSITY_DEBUG,"Start reading from child %d\n",ChildPID);
        while (fgets(ChildOutput,MAX_STRING,FilePointer) != NULL) {
            if (Options.TimeStamps) {
//----Output WC/CPU
                MyPrintf(Options,VERBOSITY_NONE,"%6.2f/%6.2f\t",WCUsage(Options),
CPUUsage(Options,CPUStatFile));
            }
            MyPrintf(Options,VERBOSITY_NONE,"%s",ChildOutput);
        }
        MyPrintf(Options,VERBOSITY_DEBUG,"Finished reading from child %d\n",ChildPID);
        fclose(FilePointer);
        if (Options.TimeStamps) {
            MyPrintf(Options,VERBOSITY_NONE,"%6.2f/%6.2f\tEOF\n",WCUsage(Options),
CPUUsage(Options,CPUStatFile));
            fflush(stdout);
        }
    }
    exit(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------
void ChildSaysGo(int TheSignal) {

    //printf("Child told parent to start monitoring\n");
}
//--------------------------------------------------------------------------------------------------
void UserInterrupt(int TheSignal) {

//DEBUG printf("User did ^C to %d\n",getpid());
    GlobalInterrupted = TRUE;
}
//--------------------------------------------------------------------------------------------------
void MonitorDescendantProcesses(OptionsType Options,char * CGroupProcsFile,char * CPUStatFile,
char * RAMStatFile) {

    int NumberOfProccessesInCGroup;
    BOOLEAN DoneSomeKilling;
    int ReapedPID;
    int PIDsInCGroup[MAX_PIDS];
    double CPUUsed, WCUsed, RAMUsed;
    double WCLost;

    WCLost = 0.0;
//----Watch the processes
    NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupProcsFile,PIDsInCGroup);
    while (NumberOfProccessesInCGroup > 0) {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Number of processes is: %d\n",
NumberOfProccessesInCGroup);
        DoneSomeKilling = FALSE;
//----Always get resource usages for reporting, even if not limiting
        CPUUsed = CPUUsage(Options,CPUStatFile);
        WCUsed = WCUsage(Options);
        RAMUsed = RAMUsage(Options,RAMStatFile);
        if (Options.CPULimit > 0 && CPUUsed > Options.CPULimit) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGXCPU);
            DoneSomeKilling = TRUE;
        }
        if (Options.WCLimit > 0 && WCUsed > Options.WCLimit) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGALRM);
            DoneSomeKilling = TRUE;
        }
        if (Options.RAMLimit > 0 && RAMUsed > Options.RAMLimit) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGTERM);
            DoneSomeKilling = TRUE;
        }
        if (GlobalInterrupted) {
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGINT);
            DoneSomeKilling = TRUE;
        }
        MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,"CPU: %.2fs WC: %.2fs RAM: %.2fMiB\n",
CPUUsed,WCUsed,RAMUsed);
//----Reap zombies
        sleep(1);
        WCLost = 1.0;
        if (DoneSomeKilling) {
            while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
                MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Reaped killed or exited process %d\n",
ReapedPID);
            }
        }
        NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupProcsFile,PIDsInCGroup);
    }
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"No processes left\n");
//----Reap zombies child (should not exist)
    while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Reaped exited process %d\n",ReapedPID);
    }
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final CPU usage: %.2f\n",
CPUUsage(Options,CPUStatFile));
//----WC might be 1s too high due to sleep in loop to allow processes to die
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final WC  usage: %.2f\n",WCUsage(Options) - WCLost);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final RAM usage: %.2f\n",RAMUsage(Options,RAMStatFile));

}
//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    OptionsType Options;
    String CGroupDir;
    String CGroupProcsFile;
    String CPUStatFile;
    String RAMStatFile;
    String ShellCommand;
    int ParentPID;
    int ChildPID;
    struct sigaction SignalHandling;

    Options = ProcessOptions(argc,argv);

    ParentPID = getpid();
    MySnprintf(Options,CGroupDir,MAX_STRING,"%s/%d",CGROUPS_DIR,ParentPID);
    MySnprintf(Options,CGroupProcsFile,MAX_STRING,"%s/%s",CGroupDir,"cgroup.procs");
    MySnprintf(Options,CPUStatFile,MAX_STRING,"%s/%s",CGroupDir,"cpu.stat");
    MySnprintf(Options,RAMStatFile,MAX_STRING,"%s/%s",CGroupDir,"memory.current");

    MySnprintf(Options,ShellCommand,MAX_STRING,"mkdir %s",CGroupDir);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"About to do %s\n",ShellCommand);
    system(ShellCommand);
    MySnprintf(Options,ShellCommand,MAX_STRING,"chown -R %d:%d %s",getuid(),getgid(),CGroupDir);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"About to do %s\n",ShellCommand);
    system(ShellCommand);

    GlobalInterrupted = FALSE;
    SignalHandling.sa_handler = UserInterrupt;
    SignalHandling.sa_flags = SA_RESTART;
    if (sigaction(SIGINT,&SignalHandling,NULL) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not wait for ^C signal");
        exit(EXIT_FAILURE);
    }
    SignalHandling.sa_handler = ChildSaysGo;
    SignalHandling.sa_flags = 0;
    if (sigaction(SIGCONT,&SignalHandling,NULL) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not wait for child signal");
        exit(EXIT_FAILURE);
    }
    
    if ((ChildPID = fork()) == -1) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not fork() for child processing");
        exit(EXIT_FAILURE);
    }

    if (ChildPID == 0) {
        StartChildProcessing(Options,CGroupProcsFile,CPUStatFile,ParentPID);
    } else {
        MyPrintf(Options,VERBOSITY_DEBUG,"In RLR with PID %d\n",ParentPID);
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,
"Wait for child of %d to create and add itself to a cgroup\n",ChildPID);
        pause();
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"Child of %d has started\n",ChildPID);
//----Start the clocks
        CPUUsage(Options,CPUStatFile);
        WCUsage(Options);
        MonitorDescendantProcesses(Options,CGroupProcsFile,CPUStatFile,RAMStatFile);
    }

    MySnprintf(Options,ShellCommand,MAX_STRING,"rmdir %s",CGroupDir);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"About to do %s\n",ShellCommand);
    system(ShellCommand);

    return(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------
