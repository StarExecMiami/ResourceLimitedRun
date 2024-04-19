#define _GNU_SOURCE

#include <getopt.h>
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
#include <sched.h>
//--------------------------------------------------------------------------------------------------
#define VERBOSITY_ERROR -1
#define VERBOSITY_NONE 0
#define VERBOSITY_STDOUT_ONLY 1
#define VERBOSITY_RESOURCE_USAGE 2
#define VERBOSITY_BIG_STEPS 3
#define VERBOSITY_RLR_ACTIONS 4
#define VERBOSITY_ALL 5
#define VERBOSITY_DEBUG 6
#define VERBOSITY_DEFAULT VERBOSITY_RESOURCE_USAGE

#define BOOLEAN int
#define TRUE 1
#define FALSE 0

#define MAX_ARGS 256
#define MAX_STRING 1024
#define MAX_PIDS 1024
#define MAX_CORES 256

#define CGROUPS_DIR "/sys/fs/cgroup/tptp"

typedef char String[MAX_STRING];

typedef struct {
    int Verbosity;
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    BOOLEAN TimeStamps;
    String ProgramToControl;
    FILE* ProgramOutputFile;
    BOOLEAN UseHyperThreading;
    BOOLEAN ReportCPUArchitecture;
} OptionsType;

#define MAX_THREADS 2
typedef struct {
    int NumberOfCPUs;
    int NumberOfCores;
    int NumberOfThreads;
    int CoreAndThreadNumbers[MAX_THREADS][MAX_CORES];
} CPUArchitectureType;

//--------------------------------------------------------------------------------------------------
static BOOLEAN GlobalInterrupted;
//--------------------------------------------------------------------------------------------------
void MyPrintf(OptionsType Options,int RequiredVerbosity,char * Format,...) {

    va_list ThingsToPrint;
    String FinalFormat;

    if (Options.Verbosity >= RequiredVerbosity) {
        switch (RequiredVerbosity) {
            case VERBOSITY_ERROR:
                snprintf(FinalFormat,MAX_STRING,"ERROR: %s",Format);
                break;
            case VERBOSITY_RESOURCE_USAGE:
                snprintf(FinalFormat,MAX_STRING,"%%%% %s",Format);
                break;
            case VERBOSITY_RLR_ACTIONS:
            case VERBOSITY_BIG_STEPS:
                snprintf(FinalFormat,MAX_STRING,"RLR says: %s",Format);
                break;
            default:
                strcpy(FinalFormat,Format);
                break;
        }
        va_start(ThingsToPrint,Format);
        vprintf(FinalFormat,ThingsToPrint);
        fflush(stdout);
        va_end(ThingsToPrint);
    }
}
//--------------------------------------------------------------------------------------------------
void MySnprintf(char * PrintIntoHere,int LengthOfHere,char * Format,...) {

    va_list ThingsToPrint;

    va_start(ThingsToPrint,Format);
    vsnprintf(PrintIntoHere,LengthOfHere,Format,ThingsToPrint);
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

    static struct option LongOptions[] = {
        {"output",                  required_argument, NULL, 'o'},
        {"timestamp",               no_argument,       NULL, 't'},
        {"verbosity",               required_argument, NULL, 'b'},
        {"cpu-limit",               required_argument, NULL, 'C'},
        {"wall-clock-limit",        required_argument, NULL, 'W'},
        {"mem-soft-limit",          required_argument, NULL, 'M'},
        {"use-hyperthreading",      no_argument,       NULL, 'y'},
        {"report-cpu-architecture", no_argument,       NULL, 'a'},
        {"help",                    no_argument,       NULL, 'h'},
        {NULL,0,NULL,0}
    };
    int OptionStartIndex = 0;

//----Defaults
    Options.Verbosity = VERBOSITY_DEFAULT;
    Options.CPULimit = -1;
    Options.WCLimit = -1;
    Options.RAMLimit = -1;
    Options.TimeStamps = FALSE;
    strcpy(Options.ProgramToControl,"");
    Options.ProgramOutputFile = NULL;
    Options.UseHyperThreading = FALSE;
    Options.ReportCPUArchitecture = FALSE;

    while ((option = getopt_long(argc,argv,"yao:tb:C:W:M:?h",LongOptions,
&OptionStartIndex)) != -1) {
        switch(option) {
//---Flag options
            case 0:
                break;
            case 'y':
                Options.UseHyperThreading = TRUE;
                break;
            case 'a':
                Options.ReportCPUArchitecture = TRUE;
                break;
            case 'o':
                if ((Options.ProgramOutputFile = fopen(optarg,"w")) == NULL) {
                    MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 't':
                Options.TimeStamps = TRUE;
                break;
            case 'b':
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
            case '?':
            case 'h':
                MyPrintf(Options,VERBOSITY_NONE,"HELP\n");
                exit(EXIT_SUCCESS);
                break;
            default:
                MyPrintf(Options,VERBOSITY_ERROR,"Invalid option %c\n",option);
                exit(EXIT_FAILURE);
                break;
        }
    }
//----The program to control must be next
    if (optind > argc) {
        strcpy(Options.ProgramToControl,argv[optind++]);
        MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,
"CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",Options.CPULimit,
Options.WCLimit,Options.RAMLimit,Options.ProgramToControl);
    } else {
        if (!Options.ReportCPUArchitecture) {
            MyPrintf(Options,VERBOSITY_ERROR,"No program to control\n",option);
            exit(EXIT_FAILURE);
        }
    }

    return(Options);
}
//--------------------------------------------------------------------------------------------------
void ReportCPUArchitecture(OptionsType Options,CPUArchitectureType CPUArchitecture) {

    int CPUNumber,CoreSibling,ThreadNumber,CoreSiblingsPerCPU;

    MyPrintf(Options,VERBOSITY_NONE,"Number of CPUs:     %2d\n",CPUArchitecture.NumberOfCPUs);
    MyPrintf(Options,VERBOSITY_NONE,"Number of cores:    %2d\n",CPUArchitecture.NumberOfCores);
    MyPrintf(Options,VERBOSITY_NONE,"Number of threads:  %2d\n",CPUArchitecture.NumberOfThreads);

    CoreSiblingsPerCPU = 
CPUArchitecture.NumberOfCores/CPUArchitecture.NumberOfCPUs/CPUArchitecture.NumberOfThreads;
    MyPrintf(Options,VERBOSITY_NONE,"Core configuration:\n");
    for (CPUNumber = 0;CPUNumber < CPUArchitecture.NumberOfCPUs;CPUNumber++) {
        MyPrintf(Options,VERBOSITY_NONE,"    CPU %2d: ",CPUNumber);
        for (CoreSibling = CPUNumber*CoreSiblingsPerCPU;
CoreSibling < (CPUNumber+1)*CoreSiblingsPerCPU;CoreSibling++) {
            for (ThreadNumber = 0;ThreadNumber < CPUArchitecture.NumberOfThreads; ThreadNumber++) {
                MyPrintf(Options,VERBOSITY_NONE,"%2d",
CPUArchitecture.CoreAndThreadNumbers[ThreadNumber][CoreSibling]);
                if (CoreSibling < (CPUNumber+1)*CoreSiblingsPerCPU - 1||
ThreadNumber < CPUArchitecture.NumberOfThreads - 1) {
                    MyPrintf(Options,VERBOSITY_NONE,",");
                }
            }
            MyPrintf(Options,VERBOSITY_NONE," ");
        }
        MyPrintf(Options,VERBOSITY_NONE,"\n");
    }
}
//--------------------------------------------------------------------------------------------------
int GetSiblings(OptionsType Options,int CoreNumber,char * SiblingType,int * Siblings) {

    String FileName;
    FILE* CPUFile;
    String SiblingsLine;
    int NumberOfSiblings;
    char * CSV;
    char * Dash;
    int Start,End,Number;

    MySnprintf(FileName,MAX_STRING,"/sys/devices/system/cpu/cpu%d/topology/%s_siblings_list",
CoreNumber,SiblingType);
    if ((CPUFile = fopen(FileName,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",FileName);
        exit(EXIT_FAILURE);
    }
    if (fgets(SiblingsLine,MAX_STRING,CPUFile) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not read line from %s\n",FileName);
        exit(EXIT_FAILURE);
    }
    fclose(CPUFile);
//DEBUG printf("for %s got line %s\n",SiblingType,SiblingsLine);
    NumberOfSiblings = 0;
    CSV = strtok(SiblingsLine,",");
//DEBUG printf("first comma token is %s\n",CSV);
    while (CSV != NULL) {
//----If a range expand
        if ((Dash = strchr(CSV,'-')) != NULL) {
//DEBUG printf("dash range found\n");
            End = atoi(Dash+1);
            Dash = '\0';
            Start = atoi(CSV);
            for (Number = Start; Number <= End; Number++) {
//DEBUG printf("adding %d\n",Number);
                Siblings[NumberOfSiblings++] = Number;
            }
//----Otherwise take as is
        } else {
//DEBUG printf("adding plain value %d\n",atoi(CSV));
            Siblings[NumberOfSiblings++] = atoi(CSV);
        }
        CSV = strtok(NULL," ");
//DEBUG printf("next comma token is %s\n",CSV);
    }

    return(NumberOfSiblings);
}
//--------------------------------------------------------------------------------------------------
CPUArchitectureType GetCPUArchitecture(OptionsType Options) {

    CPUArchitectureType CPUArchitecture;
    cpu_set_t AffinityMask;
    int CoreNumber;
    int CoreThreadGroup;
    int CoreSiblings[MAX_CORES];
    int NumberOfCoreSiblings;
    int CoreSiblingNumber;
    int ThreadSiblings[MAX_CORES];
    int ThreadSiblingNumber;

    if (sched_getaffinity(0,sizeof(cpu_set_t),&AffinityMask) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not get core numbers\n");
        exit(EXIT_FAILURE);
    }
    CPUArchitecture.NumberOfCPUs = 0;
    CPUArchitecture.NumberOfThreads = 0;
    CPUArchitecture.NumberOfCores = CPU_COUNT(&AffinityMask);
    if (CPUArchitecture.NumberOfCores > MAX_CORES) {
        MyPrintf(Options,VERBOSITY_ERROR,"Number of cores %d exceed maximum %d\n",
CPUArchitecture.NumberOfCores,MAX_CORES);
        exit(EXIT_FAILURE);
    }
//DEBUG printf("There are %d cores\n",CPUArchitecture.NumberOfCores);
    CoreThreadGroup = 0;
    for (CoreNumber= 0;CoreNumber < CPUArchitecture.NumberOfCores;CoreNumber++) {
//----If the core is in this process's set
        if (CPU_ISSET(CoreNumber,&AffinityMask)) {
            CPUArchitecture.NumberOfCPUs++;
//DEBUG printf("Core number %d is available\n",CoreNumber);
            NumberOfCoreSiblings = GetSiblings(Options,CoreNumber,"core",CoreSiblings);
//DEBUG printf("Core %d has %d core siblines\n",CoreNumber,NumberOfCoreSiblings);
            for (CoreSiblingNumber = 0;CoreSiblingNumber < NumberOfCoreSiblings;
CoreSiblingNumber++) {
//DEBUG printf("Dealing with core sibling of %d, sibling is %d\n",CoreNumber,CoreSiblings[CoreSiblingNumber]);
//----If this group of threads has not been dealt with yet
                if (CPU_ISSET(CoreSiblings[CoreSiblingNumber],&AffinityMask)) {
                    CPUArchitecture.NumberOfThreads = GetSiblings(Options,
CoreSiblings[CoreSiblingNumber],"thread",ThreadSiblings);
//DEBUG printf("Core sibling %d has %d thread siblings\n",CoreSiblings[CoreSiblingNumber],CPUArchitecture.NumberOfThreads);
                    if (CPUArchitecture.NumberOfThreads > MAX_THREADS) {
                        MyPrintf(Options,VERBOSITY_ERROR,"Number of thread %d exceed maximum %d\n",
CPUArchitecture.NumberOfThreads,MAX_THREADS);
                        exit(EXIT_FAILURE);
                    }
                    for (ThreadSiblingNumber = 0;
ThreadSiblingNumber < CPUArchitecture.NumberOfThreads;ThreadSiblingNumber++) {
                        CPUArchitecture.CoreAndThreadNumbers[ThreadSiblingNumber]
[CoreThreadGroup] = ThreadSiblings[ThreadSiblingNumber];
                        CPU_CLR(ThreadSiblings[ThreadSiblingNumber],&AffinityMask);
//DEBUG printf("Core %d stored at thread row %d for core column %d\n",ThreadSiblings[ThreadSiblingNumber],ThreadSiblingNumber,CoreThreadGroup);
                    }
                    CoreThreadGroup++;
                } else {
//DEBUG printf("already dealt with core %d\n",CoreSiblings[CoreSiblingNumber]);
                }
            }
        }
    }

    return(CPUArchitecture);
}
//--------------------------------------------------------------------------------------------------
//----Fill an array of PIDS from .procs
int NumberOfProcesses(OptionsType Options,char * CGroupProcsFile,int * PIDsInCGroup) {

    String ShellCommand;
    FILE* ShellFile;
    int NumberOfProccessesInCGroup;

    MySnprintf(ShellCommand,MAX_STRING,"cat %s",CGroupProcsFile);
    if ((ShellFile = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    NumberOfProccessesInCGroup = 0;
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,"The PIDs are now:");
    while (NumberOfProccessesInCGroup < MAX_PIDS &&
fscanf(ShellFile,"%d",&PIDsInCGroup[NumberOfProccessesInCGroup]) != EOF) {
//----Have to do by hand for pretty output
        if (Options.Verbosity >= VERBOSITY_RLR_ACTIONS) {
            printf(" %d",PIDsInCGroup[NumberOfProccessesInCGroup]);
        }
        NumberOfProccessesInCGroup++;
    }
    if (Options.Verbosity >= VERBOSITY_RLR_ACTIONS) {
        printf("\n");
    }
    pclose(ShellFile);
    if (NumberOfProccessesInCGroup == MAX_PIDS) {
        MyPrintf(Options,VERBOSITY_ERROR,"Ran out of PID space when counting NumberOfProcesses\n");
    }
    return(NumberOfProccessesInCGroup); 
}
//--------------------------------------------------------------------------------------------------
//----Get memory usage in MiB
double RAMUsage(OptionsType Options,char * RAMStatFile,BOOLEAN ReportMax) {

    String ShellCommand;
    FILE* ShellFile;
    long Bytes;
    static long MaxBytes = 0;

    MySnprintf(ShellCommand,MAX_STRING,"cat %s",RAMStatFile);
    if ((ShellFile = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",RAMStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(ShellFile,"%ld",&Bytes);
    pclose(ShellFile);
    if (Bytes > MaxBytes) {
        MaxBytes = Bytes;
    }
    if (ReportMax) {
        return(MaxBytes/1048576.0);
    } else {
        return(Bytes/1048576.0);
    }
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
    FILE* ShellFile;
    long MicroSeconds;
    static double StartMicroSeconds = -1.0;

    MySnprintf(ShellCommand,MAX_STRING,"cat %s",CPUStatFile);
    if ((ShellFile = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for reading\n",CPUStatFile);
        exit(EXIT_FAILURE);
    }
    fscanf(ShellFile,"usage_usec %ld",&MicroSeconds);
    pclose(ShellFile);
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
//--------------------------------------------------------------------------------------------------
void StartChildProgram(OptionsType Options,char * CGroupProcsFile,int PIDOfRLR) {

    int ChildPID;
    FILE* ShellFile;
    String ShellCommand;
    char *MyArgV[MAX_ARGS];
    int MyArgC;

    ChildPID = getpid();
    MyPrintf(Options,VERBOSITY_DEBUG,"In CHILD with PID %d\n",ChildPID);
    MySnprintf(ShellCommand,MAX_STRING,"cat > %s",CGroupProcsFile);
    if ((ShellFile = popen(ShellCommand,"w")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,"Could not open %s for writing\n",CGroupProcsFile);
        exit(EXIT_FAILURE);
    }
    fprintf(ShellFile,"%d\n",ChildPID);
    pclose(ShellFile);

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
    FILE* PipeReader;
    String ChildOutput;
    String TimeStamp;

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
        PipeReader = fdopen(Pipe[0],"r");
        MyPrintf(Options,VERBOSITY_DEBUG,"Start reading from child %d\n",ChildPID);
        strcpy(TimeStamp,"");
        while (fgets(ChildOutput,MAX_STRING,PipeReader) != NULL) {
            if (Options.TimeStamps) {
//----Output WC/CPU
                MySnprintf(TimeStamp,MAX_STRING,"%6.2f/%6.2f\t",WCUsage(Options),
CPUUsage(Options,CPUStatFile));
            }
            MyPrintf(Options,VERBOSITY_STDOUT_ONLY,"%s%s",TimeStamp,ChildOutput);
            if (Options.ProgramOutputFile != NULL) {
                fprintf(Options.ProgramOutputFile,"%s%s",TimeStamp,ChildOutput);
            }
        }
        MyPrintf(Options,VERBOSITY_DEBUG,"Finished reading from child %d\n",ChildPID);
        fclose(PipeReader);
        if (Options.TimeStamps) {
            MySnprintf(TimeStamp,MAX_STRING,"%6.2f/%6.2f\t",WCUsage(Options),
CPUUsage(Options,CPUStatFile));
            MyPrintf(Options,VERBOSITY_STDOUT_ONLY,"%sEOF\n",TimeStamp);
            fflush(stdout);
            if (Options.ProgramOutputFile != NULL) {
                fprintf(Options.ProgramOutputFile,"%sEOF\n",TimeStamp);
            }
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
        RAMUsed = RAMUsage(Options,RAMStatFile,FALSE);
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
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final CPU usage: %.2fs\n",
CPUUsage(Options,CPUStatFile));
//----WC might be 1s too high due to sleep in loop to allow processes to die
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final WC  usage: %.2fs\n",WCUsage(Options) - WCLost);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"Final RAM usage: %.2fMiB\n",RAMUsage(Options,RAMStatFile,
TRUE));

}
//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    OptionsType Options;
    String CGroupDir;
    String CGroupProcsFile;
    String CPUStatFile;
    String RAMStatFile;
    String ShellCommand;
    CPUArchitectureType CPUArchitecture;
    int CoreNumbers[MAX_CORES];
    int ParentPID;
    int ChildPID;
    struct sigaction SignalHandling;

    Options = ProcessOptions(argc,argv);

    CPUArchitecture = GetCPUArchitecture(Options);
    if (Options.ReportCPUArchitecture) {
        ReportCPUArchitecture(Options,CPUArchitecture);
//----If only reporting, exit now
        if (strlen(Options.ProgramToControl) == 0) {
            exit(EXIT_SUCCESS);
        }
    }

    ParentPID = getpid();
    MySnprintf(CGroupDir,MAX_STRING,"%s/%d",CGROUPS_DIR,ParentPID);
    MySnprintf(CGroupProcsFile,MAX_STRING,"%s/%s",CGroupDir,"cgroup.procs");
    MySnprintf(CPUStatFile,MAX_STRING,"%s/%s",CGroupDir,"cpu.stat");
    MySnprintf(RAMStatFile,MAX_STRING,"%s/%s",CGroupDir,"memory.current");

    MySnprintf(ShellCommand,MAX_STRING,"mkdir %s",CGroupDir);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"About to do %s\n",ShellCommand);
    system(ShellCommand);
    MySnprintf(ShellCommand,MAX_STRING,"chown -R %d:%d %s",getuid(),getgid(),CGroupDir);
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

    if (Options.ProgramOutputFile != NULL) {
        fclose(Options.ProgramOutputFile);
    }
    MySnprintf(ShellCommand,MAX_STRING,"rmdir %s",CGroupDir);
    MyPrintf(Options,VERBOSITY_BIG_STEPS,"About to do %s\n",ShellCommand);
    system(ShellCommand);

    return(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------
