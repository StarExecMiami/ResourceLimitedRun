#define _GNU_SOURCE

#include <getopt.h>
#include <string.h>
#include <math.h>
#include <math.h>
#include <signal.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
//--------------------------------------------------------------------------------------------------
#define VERSION_NUMBER "1.0.0"

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
#define BOOLEAN_TO_STRING(Value) (Value ? "TRUE" : "FALSE")

#define BYTES_PER_MIB 1048576.0
#define DEFAULT_WC_DELAY_BEFORE_KILL 1.0
#define DEFAULT_WC_DELAY_BETWEEN_RESOURCE_MONITORING 0.5
#define DEFAULT_WC_DELAY_BETWEEN_RESOURCE_REPORTS 1.0

#define MAX_ARGS 256
#define MAX_STRING 1024
#define MAX_PIDS 1024
#define MAX_CORES 256

#define CGROUPS_DIR "/sys/fs/cgroup"

typedef char String[MAX_STRING];

typedef struct {
    String CGroupUser;
    int Verbosity;
    BOOLEAN ReportOptions;
    BOOLEAN ReportCPUArchitecture;
    int CPULimit;
    int WCLimit;
    int RAMLimit;
    BOOLEAN PhysicalCoreList;
    int CoresToUse[MAX_CORES];
    int NumberOfCoresToUse;
    BOOLEAN UseHyperThreading;
    double WCDelayBetweenResourceMonitoring;
    double WCDelayBetweenResourceUsageReports;
    double WCDelayBeforeKill;
    BOOLEAN TimeStamps;
    String ProgramOutputFileName;
    FILE* ProgramOutputFile;
    String RLROutputFileName;
    FILE* RLROutputFile;
    String VarFileName;
    String ProgramToControl;
} OptionsType;

typedef struct {
    String CGroupDir;
    String CGroupProcsFile;
    String CPUStatFile;
    String RAMStatFile;
    String CPUSetFile;
} CGroupFileNamesType;

#define MAX_THREADS 2
typedef struct {
    int NumberOfCPUs;
    int NumberOfCores;
    int NumberOfThreads;
    int CoreAndThreadNumbers[MAX_THREADS][MAX_CORES];
} CPUArchitectureType;

//--------------------------------------------------------------------------------------------------
static BOOLEAN GlobalInterrupted;

void CleanUp(OptionsType Options,CGroupFileNamesType * CGroupFileNames);
//--------------------------------------------------------------------------------------------------
void MyPrintf(OptionsType Options,int RequiredVerbosity,BOOLEAN AddPrefix,char * Format,...) {

    va_list ThingsToPrint;
    String FinalFormat;
    String TheOutput;

    if (Options.Verbosity >= RequiredVerbosity || Options.RLROutputFile != NULL) {
        strcpy(FinalFormat,Format);
        switch (RequiredVerbosity) {
            case VERBOSITY_ERROR:
                snprintf(FinalFormat,MAX_STRING,"ERROR: %s",Format);
                break;
            case VERBOSITY_RESOURCE_USAGE:
                if (AddPrefix) {
                    snprintf(FinalFormat,MAX_STRING,"%%%% %s",Format);
                }
                break;
            case VERBOSITY_RLR_ACTIONS:
            case VERBOSITY_BIG_STEPS:
                if (AddPrefix) {
                    snprintf(FinalFormat,MAX_STRING,"RLR says: %s",Format);
                }
                break;
            case VERBOSITY_ALL:
                if (AddPrefix) {
                    snprintf(FinalFormat,MAX_STRING,"ALL says: %s",Format);
                }
                break;
            default:
                break;
        }
        va_start(ThingsToPrint,Format);
        vsnprintf(TheOutput,MAX_STRING,FinalFormat,ThingsToPrint);
        va_end(ThingsToPrint);
        if (Options.Verbosity >= RequiredVerbosity) {
            printf("%s",TheOutput);
            fflush(stdout);
        }
        if (Options.RLROutputFile != NULL && RequiredVerbosity != VERBOSITY_STDOUT_ONLY &&
RequiredVerbosity != VERBOSITY_DEBUG) {
            fprintf(Options.RLROutputFile,"%s",TheOutput);
            fflush(Options.RLROutputFile);
        }
    }

//----If an error, stop everything
    if (RequiredVerbosity == VERBOSITY_ERROR) {
        CleanUp(Options,NULL);
        exit(EXIT_FAILURE);
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
void PrintExplanationOfRLR() {

    printf("\
ResourceLimitedRun (RLR) monitors the CPU, WC, and memory usage of processes that are \n\
started from running a supplied program with its arguments. \n\
+ The RLR process creates a cgroup named after its PID. \n\
+ The RLR process then starts a process that collects the stdout and stderr of the supplied \n\
  program and any processes it starts. \n\
  - The output monitor process echos the output with a timestamp if requested. \n\
  - The output monitor process starts a third process to execute the supplied program. \n\
+ The program execution process puts itself into the cgroup, then execs the supplied program. \n\
  - All processes that start as a result of running the supplied program are automatically \n\
    added to the cgroup. \n\
+ RLR regularly gets the CPU, WC, and memory usage of the cgroup from the cgroup files. \n\
  - If any exceed the specified limit, RLR signals the processes in the cgroup to stop. \n\
  - The first signal is gentle - SIXCPU or SIGALRM or SIGTERM. \n\
  - If the processes persist they are killed with SIGKILL. \n\
+ The CPU cores to use can be limited.\n");

}
//--------------------------------------------------------------------------------------------------
void ReportCPUArchitecture(OptionsType Options,CPUArchitectureType CPUArchitecture) {

    int CPUNumber,CoreSibling,ThreadNumber,CoreSiblingsPerCPU;

    MyPrintf(Options,VERBOSITY_NONE,TRUE,"Number of CPUs:     %2d\n",CPUArchitecture.NumberOfCPUs);
    MyPrintf(Options,VERBOSITY_NONE,TRUE,"Number of cores:    %2d\n",CPUArchitecture.NumberOfCores);
    MyPrintf(Options,VERBOSITY_NONE,TRUE,"Number of threads:  %2d\n",
CPUArchitecture.NumberOfThreads);

    CoreSiblingsPerCPU = 
CPUArchitecture.NumberOfCores/CPUArchitecture.NumberOfCPUs/CPUArchitecture.NumberOfThreads;
    MyPrintf(Options,VERBOSITY_NONE,TRUE,"Core configuration:\n");
    for (CPUNumber = 0;CPUNumber < CPUArchitecture.NumberOfCPUs;CPUNumber++) {
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"    CPU %2d: ",CPUNumber);
        for (CoreSibling = CPUNumber*CoreSiblingsPerCPU;
CoreSibling < (CPUNumber+1)*CoreSiblingsPerCPU;CoreSibling++) {
            for (ThreadNumber = 0;ThreadNumber < CPUArchitecture.NumberOfThreads; ThreadNumber++) {
                MyPrintf(Options,VERBOSITY_NONE,TRUE,"%2d",
CPUArchitecture.CoreAndThreadNumbers[ThreadNumber][CoreSibling]);
                if (CoreSibling < (CPUNumber+1)*CoreSiblingsPerCPU - 1||
ThreadNumber < CPUArchitecture.NumberOfThreads - 1) {
                    MyPrintf(Options,VERBOSITY_NONE,TRUE,",");
                }
            }
            MyPrintf(Options,VERBOSITY_NONE,TRUE," ");
        }
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"\n");
    }
}
//--------------------------------------------------------------------------------------------------
char * CoresToUseAsString(OptionsType Options,CPUArchitectureType CPUArchitecture,
char * CoreList) {

    int CoreNumber,ThreadNumber;
    String CommaCore;

    strcpy(CoreList,"");
    for (CoreNumber=0; CoreNumber < CPUArchitecture.NumberOfCores; CoreNumber++) {
        if (CoreNumber > 0) {
            strcat(CoreList,",");
        }
        sprintf(CommaCore,"%d",CPUArchitecture.CoreAndThreadNumbers[0][CoreNumber]);
        strcat(CoreList,CommaCore);
        if (Options.UseHyperThreading) {
            for (ThreadNumber = 1;ThreadNumber < CPUArchitecture.NumberOfThreads;ThreadNumber++) {
                sprintf(CommaCore,",%d",
CPUArchitecture.CoreAndThreadNumbers[ThreadNumber][CoreNumber]);
                strcat(CoreList,CommaCore);
            }
        }
    }
    return(CoreList);
}
//--------------------------------------------------------------------------------------------------
//----Needed here because it's used in ProcessOptions
int ExpandCoresToUse(char * Request,int * CoresToUse) {

    int NumberOfIds;
    char * CSV;
    char * Dash;
    int Start,End,Number;

    NumberOfIds = 0;
    CSV = strtok(Request,",");
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
                CoresToUse[NumberOfIds++] = Number;
            }
//----Otherwise take as is
        } else {
//DEBUG printf("adding plain value %d\n",atoi(CSV));
            CoresToUse[NumberOfIds++] = atoi(CSV);
        }
        CSV = strtok(NULL," ");
//DEBUG printf("next comma token is %s\n",CSV);
    }

    return(NumberOfIds);
}
//--------------------------------------------------------------------------------------------------
char * GenerateHelpLine(OptionsType Options,CPUArchitectureType CPUArchitecture,char Option,
char * HelpLine) {

    String TheString;

    strcpy(HelpLine,"No help for you!");
    switch(Option) {
        case 'u':
            MySnprintf(HelpLine,MAX_STRING,"Name of the user directory under %s.\n    Value: %s",
CGROUPS_DIR,Options.CGroupUser);
            break;
        case 'b':
            MySnprintf(HelpLine,MAX_STRING,"Verbosity.\n    %d = None\n    %d = Program stdout\n    %d = Resource usage\n    %d = Coarse progress\n    %d = Fine progress\n    %d = All\n    %d = Debug\n    Value: %d",
VERBOSITY_NONE,VERBOSITY_STDOUT_ONLY,VERBOSITY_RESOURCE_USAGE,VERBOSITY_BIG_STEPS,
VERBOSITY_RLR_ACTIONS,VERBOSITY_ALL,VERBOSITY_DEBUG,Options.Verbosity);
            break;
        case 'O':
            MySnprintf(HelpLine,MAX_STRING,"Report options.\n    Value: %s",
BOOLEAN_TO_STRING(Options.ReportOptions));
            break;
        case 'A':
            MySnprintf(HelpLine,MAX_STRING,"Report CPU architecture.\n    Value: %s",
BOOLEAN_TO_STRING(Options.ReportCPUArchitecture));
            break;
        case 'C':
            MySnprintf(HelpLine,MAX_STRING,"CPU time limit, in seconds.\n    Value: %.2fs",
Options.CPULimit);
            break;
        case 'W':
            MySnprintf(HelpLine,MAX_STRING,"WC time limit, in seconds.\n    Value: %.2fs",
Options.WCLimit);
            break;
        case 'M':
            MySnprintf(HelpLine,MAX_STRING,"Memory limit, in MiB.\n    Value: %.2fMiB",
Options.RAMLimit);
            break;
        case 'p':
            MySnprintf(HelpLine,MAX_STRING,"Core list is physical (not indexed).\n    Value: %s",
BOOLEAN_TO_STRING(Options.PhysicalCoreList));
            break;
        case 'c':
            MySnprintf(HelpLine,MAX_STRING,"Cores to use.\n    Value: %s",
ZZZZZZZZZZZZZ
CoresToUseAsString(Options,CPUArchitecture,TheString));
            break;
        case 'y':
            MySnprintf(HelpLine,MAX_STRING,"Use hyperthreading.\n    Value: %s",
BOOLEAN_TO_STRING(Options.UseHyperThreading));
            break;
        case 'm':
            MySnprintf(HelpLine,MAX_STRING,"Delay between checking resource usage, in seconds.\n    Value: %.2fs",
Options.WCDelayBetweenResourceMonitoring);
            break;
        case 'r':
            MySnprintf(HelpLine,MAX_STRING,"Delay between reporting resource usage, in seconds.\n    Value: %.2fs",
Options.WCDelayBetweenResourceUsageReports);
            break;
        case 'd':
            MySnprintf(HelpLine,MAX_STRING,"Delay between gentle and KILL signals, in seconds.\n    Value: %.2fs",
Options.WCDelayBeforeKill);
            break;
        case 't':
            MySnprintf(HelpLine,MAX_STRING,"Timestamp program output.\n    Value: %s",
BOOLEAN_TO_STRING(Options.TimeStamps));
            break;
        case 'o':
            MySnprintf(HelpLine,MAX_STRING,"File to echo program stdout.\n    Value: %s",
Options.ProgramOutputFile == NULL ? "None" : Options.ProgramOutputFile);
            break;
        case 'w':
            MySnprintf(HelpLine,MAX_STRING,"File to log activity.\n    Value: %s",
Options.RLROutputFile == NULL ? "None" : Options.RLROutputFile);
            break;
        case 'v':
            MySnprintf(HelpLine,MAX_STRING,"File to log activity.\n    Value: %s",
Options.RLROutputFile == NULL ? "None" : Options.RLROutputFile);
            break;
        case 'x':
            MySnprintf(HelpLine,MAX_STRING,
"Print an explanation of how this program works, then exit.");
            break;
        case 'h':
            MySnprintf(HelpLine,MAX_STRING,"Print this help, then exit.");
            break;
        default:
            break;
    }

    return(HelpLine);
}
//--------------------------------------------------------------------------------------------------
void PrintOptions(OptionsType Options,CPUArchitectureType CPUArchitecture,
struct option LongOptions[]) {

    int OptionIndex;
    String HelpLine;

    MyPrintf(Options,VERBOSITY_NONE,TRUE,"\nOptions are ...\n");
    OptionIndex = 0;
    while (LongOptions[OptionIndex].val != 0) {
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"  -%c --%-25s\n",LongOptions[OptionIndex].val,
LongOptions[OptionIndex].name);
        if (LongOptions[OptionIndex].val != LongOptions[OptionIndex+1].val) {
            MyPrintf(Options,VERBOSITY_NONE,TRUE,"    %s\n",
GenerateHelpLine(Options,CPUArchitecture,LongOptions[OptionIndex].val,HelpLine));
        }
        OptionIndex++;
    }
    if (strlen(Options.ProgramToControl) > 0) {
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"The program to control is ...\n    %s\n",
Options.ProgramToControl);
    }
}
//--------------------------------------------------------------------------------------------------
void PrintHelp(OptionsType Options,CPUArchitectureType CPUArchitecture,
struct option LongOptions[]) {

    MyPrintf(Options,VERBOSITY_NONE,TRUE,
"This ResourceLimitedRun version %s\n",VERSION_NUMBER);
    MyPrintf(Options,VERBOSITY_NONE,TRUE,
"Usage: ResourceLimitedRun [Options] 'Program and its arguments'\n");
    PrintOptions(Options,CPUArchitecture,LongOptions);
}
//--------------------------------------------------------------------------------------------------
OptionsType InitializeOptions() {

    OptionsType Options;

    if (getlogin_r(Options.CGroupUser,MAX_STRING) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not get user name\n");
    }
    Options.Verbosity = VERBOSITY_DEFAULT;
    Options.ReportOptions = FALSE;
    Options.ReportCPUArchitecture = FALSE;
    Options.CPULimit = -1;
    Options.WCLimit = -1;
    Options.RAMLimit = -1;
    Options.PhysicalCoreList = FALSE;
    Options.NumberOfCoresToUse = 0;
    Options.UseHyperThreading = FALSE;
    Options.WCDelayBeforeKill = DEFAULT_WC_DELAY_BEFORE_KILL;
    Options.WCDelayBetweenResourceMonitoring = DEFAULT_WC_DELAY_BETWEEN_RESOURCE_MONITORING;
    Options.WCDelayBetweenResourceUsageReports = DEFAULT_WC_DELAY_BETWEEN_RESOURCE_REPORTS;
    Options.TimeStamps = FALSE;
    Options.ProgramOutputFile = NULL;
    Options.RLROutputFile = NULL;
    strcpy(Options.VarFileName,"");
    strcpy(Options.ProgramToControl,"");

    return(Options);
}
//--------------------------------------------------------------------------------------------------
//----Process options and fills out the struct with user's command line arguemnts
OptionsType ProcessOptions(OptionsType Options,CPUArchitectureType CPUArchitecture,int argc, 
char* argv[]) {

    int Option;
    int OptionStartIndex;

    static struct option LongOptions[] = {
        {"user",                    required_argument, NULL, 'u'},
        {"verbosity",               required_argument, NULL, 'b'},
        {"report-options",          no_argument,       NULL, 'O'},
        {"report-cpu-architecture", no_argument,       NULL, 'A'},
        {"cpu-limit",               required_argument, NULL, 'C'},
        {"wall-clock-limit",        required_argument, NULL, 'W'},
        {"mem-soft-limit",          required_argument, NULL, 'M'},
        {"memory-limit",            required_argument, NULL, 'M'},
        {"physical_cores",          required_argument, NULL, 'p'},
        {"cores",                   required_argument, NULL, 'c'},
        {"use-hyperthreading",      no_argument,       NULL, 'y'},
        {"resource-monitor-delay",  required_argument, NULL, 'm'},
        {"resource-report-delay",   required_argument, NULL, 'r'},
        {"delay",                   required_argument, NULL, 'd'},
        {"kill_delay",              required_argument, NULL, 'd'},
        {"timestamp",               no_argument,       NULL, 't'},
        {"solver-data-file",        required_argument, NULL, 'o'},
        {"program-output-file",     required_argument, NULL, 'o'},
        {"watcher-data-file",       required_argument, NULL, 'w'},
        {"rlr-output-file",         required_argument, NULL, 'w'},
        {"var",                     required_argument, NULL, 'v'},
        {"explain-the-magic",       no_argument,       NULL, 'x'},
        {"help",                    no_argument,       NULL, 'h'},
        {NULL,0,NULL,0}
    };

    OptionStartIndex = 0;
    while ((Option = getopt_long(argc,argv,"u:b:OAC:W:M:p:c:ym:r:d:to:w:v:xh",LongOptions,
&OptionStartIndex)) != -1) {
        switch(Option) {
//---Flag options
            case 0:
                break;
//----Regular options
            case 'u':
                strcpy(Options.CGroupUser,optarg);
                break;
            case 'b':
                Options.Verbosity = atoi(optarg);
                break;
            case 'O':
                Options.ReportOptions = TRUE;
                break;
            case 'A':
                Options.ReportCPUArchitecture = TRUE;
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
            case 'p':
                Options.PhysicalCoreList = TRUE;
            case 'c':
                Options.NumberOfCoresToUse = ExpandCoresToUse(optarg,Options.CoresToUse);
                break;
            case 'y':
                Options.UseHyperThreading = TRUE;
                break;
            case 'm':
                Options.WCDelayBetweenResourceMonitoring= atof(optarg);
                break;
            case 'r':
                Options.WCDelayBetweenResourceUsageReports= atof(optarg);
                break;
            case 'd':
                Options.WCDelayBeforeKill= atof(optarg);
                break;
            case 't':
                Options.TimeStamps = TRUE;
                break;
            case 'o':
                strcpy(Options.ProgramOutputFileName,optarg);
                if ((Options.ProgramOutputFile = fopen(Options.ProgramOutputFileName,"w")) == 
NULL) {
                    MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for writing\n",
Options.ProgramOutputFileName);
                }
                break;
            case 'w':
                strcpy(Options.RLROutputFileName,optarg);
                if ((Options.RLROutputFile = fopen(Options.RLROutputFileName,"w")) == NULL) {
                    MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for writing\n",
Options.RLROutputFileName);
                }
                break;
            case 'v':
                strcpy(Options.VarFileName,optarg);
                break;
            case 'x':
                PrintExplanationOfRLR();
                CleanUp(Options,NULL);
                exit(EXIT_SUCCESS);
                break;
            case '?':  //----What optargs returns for invalid options
            case 'h':
                PrintHelp(Options,CPUArchitecture,LongOptions);
                CleanUp(Options,NULL);
                exit(EXIT_SUCCESS);
                break;
            default:
                MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Invalid option %c\n",Option);
                break;
        }
    }

//----The program to control must be next
    if (optind < argc) {
        strcpy(Options.ProgramToControl,argv[optind++]);
    } 
//----Can just report options to see defaults (or settings)
    if (Options.ReportOptions) {
        PrintOptions(Options,CPUArchitecture,LongOptions);
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"\n");
    }
//----Can just report CPU architecture
    if (Options.ReportCPUArchitecture) {
        ReportCPUArchitecture(Options,CPUArchitecture);
        MyPrintf(Options,VERBOSITY_NONE,TRUE,"\n");
    }
    if (strlen(Options.ProgramToControl) == 0) {
        if (Options.ReportOptions || Options.ReportCPUArchitecture) {
            CleanUp(Options,NULL);
            exit(EXIT_SUCCESS);
        } else {
            MyPrintf(Options,VERBOSITY_ERROR,TRUE,"No program to control\n");
        } 
    }

    return(Options);
}
//--------------------------------------------------------------------------------------------------
int GetSiblings(OptionsType Options,int CoreNumber,char * SiblingType,int * Siblings) {

    String FileName;
    FILE* CPUFile;
    String SiblingsLine;

    MySnprintf(FileName,MAX_STRING,"/sys/devices/system/cpu/cpu%d/topology/%s_siblings_list",
CoreNumber,SiblingType);
    if ((CPUFile = fopen(FileName,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for reading\n",FileName);
    }
    if (fgets(SiblingsLine,MAX_STRING,CPUFile) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not read line from %s\n",FileName);
    }
    fclose(CPUFile);
//DEBUG printf("for %s got line %s\n",SiblingType,SiblingsLine);
    return(ExpandCoresToUse(SiblingsLine,Siblings));
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
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not get core numbers\n");
    }
    CPUArchitecture.NumberOfCPUs = 0;
    CPUArchitecture.NumberOfThreads = 0;
    CPUArchitecture.NumberOfCores = CPU_COUNT(&AffinityMask);
    if (CPUArchitecture.NumberOfCores > MAX_CORES) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Number of cores %d exceed maximum %d\n",
CPUArchitecture.NumberOfCores,MAX_CORES);
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
                        MyPrintf(Options,VERBOSITY_ERROR,TRUE,
"Number of thread %d exceed maximum %d\n",CPUArchitecture.NumberOfThreads,MAX_THREADS);
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
        case SIGUSR1:
            return("SIGUSR1");
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
void ChildSaysGo(int TheSignal) {

    //printf("Child told parent to start monitoring\n");
}
//--------------------------------------------------------------------------------------------------
void UserInterrupt(int TheSignal) {

//DEBUG printf("User did ^C to %d\n",getpid());
    GlobalInterrupted = TRUE;
}
//--------------------------------------------------------------------------------------------------
void SetUpSignalHandling(OptionsType Options) {

    struct sigaction SignalHandling;

    GlobalInterrupted = FALSE;
    SignalHandling.sa_handler = UserInterrupt;
    SignalHandling.sa_flags = SA_RESTART;
    if (sigaction(SIGINT,&SignalHandling,NULL) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not wait for ^C signal");
    }
    SignalHandling.sa_handler = ChildSaysGo;
    SignalHandling.sa_flags = 0;
    if (sigaction(SIGCONT,&SignalHandling,NULL) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not wait for child signal");
    }
}
//--------------------------------------------------------------------------------------------------
CGroupFileNamesType InitializeCGroupFileNames() {

    CGroupFileNamesType CGroupFileNames;

    strcpy(CGroupFileNames.CGroupDir,"");
    strcpy(CGroupFileNames.CGroupProcsFile,"");
    strcpy(CGroupFileNames.CPUStatFile,"");
    strcpy(CGroupFileNames.RAMStatFile,"");
    strcpy(CGroupFileNames.CPUSetFile,"");

    return(CGroupFileNames);
}
//--------------------------------------------------------------------------------------------------
CGroupFileNamesType MakeCGroupFileNames(OptionsType Options,CGroupFileNamesType CGroupFileNames,
int ParentPID) {

    MySnprintf(CGroupFileNames.CGroupDir,MAX_STRING,"%s/%s/%d",CGROUPS_DIR,Options.CGroupUser,
ParentPID);
    MySnprintf(CGroupFileNames.CGroupProcsFile,MAX_STRING,"%s/%s",CGroupFileNames.CGroupDir,
"cgroup.procs");
    MySnprintf(CGroupFileNames.CPUStatFile,MAX_STRING,"%s/%s",CGroupFileNames.CGroupDir,
"cpu.stat");
    MySnprintf(CGroupFileNames.RAMStatFile,MAX_STRING,"%s/%s",CGroupFileNames.CGroupDir,
"memory.current");
    MySnprintf(CGroupFileNames.CPUSetFile,MAX_STRING,"%s/%s",CGroupFileNames.CGroupDir,
"cpuset.cpus");

    return(CGroupFileNames);
}
//--------------------------------------------------------------------------------------------------
void MakeCGroupDirectory(OptionsType Options,CGroupFileNamesType CGroupFileNames) {

    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Make cgroup directory %s\n",
CGroupFileNames.CGroupDir);
    if (mkdir(CGroupFileNames.CGroupDir,0755) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not create %s\n",CGroupFileNames.CGroupDir);
    }
}
//--------------------------------------------------------------------------------------------------
void RemoveCGroupDirectory(OptionsType Options,CGroupFileNamesType CGroupFileNames) {

    static BOOLEAN BeenHereBefore = FALSE;

    if (! BeenHereBefore) {
        BeenHereBefore = TRUE;
        if (strlen(CGroupFileNames.CGroupDir) > 0) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Remove cgroup directory %s\n",
CGroupFileNames.CGroupDir);
            if (rmdir(CGroupFileNames.CGroupDir) != 0) {
                MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not remove %s\n",
CGroupFileNames.CGroupDir);
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------
void LimitCores(OptionsType Options,CPUArchitectureType CPUArchitecture,
CGroupFileNamesType CGroupFileNames) {

    int CoreNumber,ThreadNumber;
    String CoreList;
    String CommaCore;
    FILE* CPUFile;

    if (Options.NumberOfCoresToUse > 0) {
        strcpy(CoreList,"");
        for (CoreNumber = 0;CoreNumber < Options.NumberOfCoresToUse;CoreNumber++) {
//----Check user has not asked for something impossible
            if (Options.CoresToUse[CoreNumber] > CPUArchitecture.NumberOfCores) {
                MyPrintf(Options,VERBOSITY_ERROR,TRUE,
"Request for core %d, but there are only %d cores\n",Options.CoresToUse[CoreNumber],
CPUArchitecture.NumberOfCores);
            }
            if (CoreNumber > 0) {
                strcat(CoreList,",");
            }
            sprintf(CommaCore,"%d", Options.PhysicalCoreList ? 
//----For physical cores use directly
Options.CoresToUse[CoreNumber] :
//----For indexed cores look up the physical core number
CPUArchitecture.CoreAndThreadNumbers[0][Options.CoresToUse[CoreNumber]]);
            strcat(CoreList,CommaCore);
//----For indexed cores I have to add the hyperthreaded cores
            if (! Options.PhysicalCoreList && Options.UseHyperThreading) {
                for (ThreadNumber = 1;ThreadNumber < CPUArchitecture.NumberOfThreads;
ThreadNumber++) {
                    sprintf(CommaCore,",%d",
CPUArchitecture.CoreAndThreadNumbers[ThreadNumber][Options.CoresToUse[CoreNumber]]);
                    strcat(CoreList,CommaCore);
                }
            }
        }
//----Limit cores in cgroup
        MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,"The physical cores to use are %s\n",
CoreList);

        if ((CPUFile = fopen(CGroupFileNames.CPUSetFile,"w")) == NULL) {
            MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for writing\n",
CGroupFileNames.CPUSetFile);
        }
        if (fputs(CoreList,CPUFile) == EOF) {
            MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not write to %s\n",
CGroupFileNames.CPUSetFile);
        }
        fclose(CPUFile);
    }
}
//--------------------------------------------------------------------------------------------------
//----Fill an array of PIDS from .procs
int NumberOfProcesses(OptionsType Options,char * CGroupProcsFile,int * PIDsInCGroup) {

    String ShellCommand;
    FILE* ShellFile;
    int NumberOfProccessesInCGroup;
    String ThePID,TheOutput;

    MySnprintf(ShellCommand,MAX_STRING,"cat %s",CGroupProcsFile);
    if ((ShellFile = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for reading\n",CGroupProcsFile);
    }
    NumberOfProccessesInCGroup = 0;
    strcpy(TheOutput,"The PIDs are now:        ");
    while (NumberOfProccessesInCGroup < MAX_PIDS &&
fscanf(ShellFile,"%d",&PIDsInCGroup[NumberOfProccessesInCGroup]) != EOF) {
        sprintf(ThePID," %d",PIDsInCGroup[NumberOfProccessesInCGroup]);
        strcat(TheOutput,ThePID);
        NumberOfProccessesInCGroup++;
    }
    strcat(TheOutput,"\n");
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,TheOutput);
    pclose(ShellFile);
    if (NumberOfProccessesInCGroup == MAX_PIDS) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Ran out of PID space for proccesses cgroup\n");
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
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for reading\n",RAMStatFile);
    }
    fscanf(ShellFile,"%ld",&Bytes);
    pclose(ShellFile);
    if (Bytes > MaxBytes) {
        MaxBytes = Bytes;
    }

//----Report in MiB
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"RAM usage (max) is:     %6.2fMiB (%.2fMiB)\n",
Bytes/BYTES_PER_MIB,MaxBytes/BYTES_PER_MIB);

    if (ReportMax) {
        return(MaxBytes/BYTES_PER_MIB);
    } else {
        return(Bytes/BYTES_PER_MIB);
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
        MyPrintf(Options,VERBOSITY_ALL,TRUE,"WC offset is:     %12.2fs\n",Start);
    }
    Seconds = Now-Start;
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"WC usage is:            %6.2fs\n",Seconds);
    return(Seconds);
}
//--------------------------------------------------------------------------------------------------
//----Get CPU usage from cpu.stat
double CPUUsage(OptionsType Options,char * CPUStatFile,double * CPUUsedByUser,
double * CPUUsedBySystem) {

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
    long MicroSeconds,MicroSecondsUser,MicroSecondsSystem;
    double Seconds,SecondsUser,SecondsSystem;
    static double StartMicroSeconds = -1.0;
    static double StartMicroSecondsUser = -1.0;
    static double StartMicroSecondsSystem = -1.0;

    MySnprintf(ShellCommand,MAX_STRING,"cat %s",CPUStatFile);
    if ((ShellFile = popen(ShellCommand,"r")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for reading\n",CPUStatFile);
    }
    fscanf(ShellFile,"usage_usec %ld\n",&MicroSeconds);
    fscanf(ShellFile,"user_usec %ld\n",&MicroSecondsUser);
    fscanf(ShellFile,"system_usec %ld\n",&MicroSecondsSystem);
//DEBUG printf("ms %ld, msu %ld, mss %ld\n",MicroSeconds,MicroSecondsUser,MicroSecondsSystem);
    pclose(ShellFile);

    if (StartMicroSeconds < 0.0) {
        StartMicroSeconds = MicroSeconds;
        MyPrintf(Options,VERBOSITY_ALL,TRUE,"CPU offset is:          %6.2fs\n",
StartMicroSeconds/1000000.0);
        StartMicroSecondsUser = MicroSecondsUser;
        MyPrintf(Options,VERBOSITY_ALL,TRUE,"CPU offset user is:     %6.2fs\n",
StartMicroSecondsUser/1000000.0);
        StartMicroSecondsSystem = MicroSecondsSystem;
        MyPrintf(Options,VERBOSITY_ALL,TRUE,"CPU offset system is:   %6.2fs\n",
StartMicroSecondsSystem/1000000.0);
    }

    Seconds = (MicroSeconds-StartMicroSeconds)/1000000.0;
    SecondsUser = (MicroSecondsUser-StartMicroSecondsUser)/1000000.0;
    SecondsSystem = (MicroSecondsSystem-StartMicroSecondsSystem)/1000000.0;

    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"CPU usage (usr+sys) is: %6.2fs (%.2fs+%.2fs)\n",
Seconds,SecondsUser,SecondsSystem);
    if (CPUUsedByUser != NULL) {
        *CPUUsedByUser = SecondsUser;
    }
    if (CPUUsedBySystem != NULL) {
        *CPUUsedBySystem = SecondsSystem;
    }
    return(Seconds);
}
//--------------------------------------------------------------------------------------------------
void KillProcesses(OptionsType Options,int NumberOfProccesses,int * PIDsToKill,int WhichSignal,
double WCUsed) {

#define PID_ROW 0
#define SIGXCPU_ROW 1
#define SIGALRM_ROW 2
#define SIGTERM_ROW 3
#define SIGINT_ROW 4
#define SIGUSR1_ROW 5
#define SIGKILL_ROW 6
    static int SignalsSent[SIGKILL_ROW+1][MAX_PIDS]; 
    int PIDsindex;
    int SignalsSentRow;
    int SentIndex;
    BOOLEAN SendTheSignal;
    int SignalToSend;

    if (WhichSignal == SIGXCPU) {
        SignalsSentRow = SIGXCPU_ROW;
    } else if (WhichSignal == SIGALRM) {
        SignalsSentRow = SIGALRM_ROW;
    } else if (WhichSignal == SIGTERM) {
        SignalsSentRow = SIGTERM_ROW;
    } else if (WhichSignal == SIGINT) {
        SignalsSentRow = SIGINT_ROW;
    } else if (WhichSignal == SIGUSR1) {
        SignalsSentRow = SIGUSR1_ROW;
    } else {
        SignalsSentRow = SIGKILL_ROW;
    }

    for (PIDsindex = 0; PIDsindex < NumberOfProccesses; PIDsindex++) {
//----See what we have sent before to this PID
        SentIndex = 0;
        SignalToSend = WhichSignal;
        while (SentIndex < MAX_PIDS && SignalsSent[PID_ROW][SentIndex] > 0 &&
SignalsSent[PID_ROW][SentIndex] != PIDsToKill[PIDsindex]) {
            MyPrintf(Options,VERBOSITY_DEBUG,TRUE,
"For PID %d already sent SIGXCPU %d and SIGALRM %d and SIGTERM %d and SIGUSR1 %d and SIGKILL %d\n",
SignalsSent[PID_ROW][SentIndex],SignalsSent[SIGXCPU_ROW][SentIndex],
SignalsSent[SIGALRM_ROW][SentIndex],SignalsSent[SIGTERM_ROW][SentIndex],
SignalsSent[SIGUSR1_ROW][SentIndex],SignalsSent[SIGKILL_ROW][SentIndex]);
            SentIndex++;
        }
        SendTheSignal = FALSE;
//----If PID has never been sent a signal, add a column for it
        if (SignalsSent[PID_ROW][SentIndex] == 0) {
            SignalsSent[PID_ROW][SentIndex] = PIDsToKill[PIDsindex];
            SendTheSignal = TRUE;
        }
//----If have not sent a gentle signal yet, do it
        if (SignalsSent[SignalsSentRow][SentIndex] == 0) {
            if ((int)WCUsed == 0) {
                SignalsSent[SignalsSentRow][SentIndex] = 1;
            } else {
                SignalsSent[SignalsSentRow][SentIndex] = (int)WCUsed;
            }
            SendTheSignal = TRUE;
//----If have sent a gentle signal before, KILL!
        } else {
//DEBUG printf("Sent SignalName(WhichSignal) %.2fs ago\n",WCUsed - SignalsSent[SignalsSentRow][SentIndex]);
            if (! SignalsSent[SIGKILL_ROW][SentIndex] && 
(WCUsed - SignalsSent[SignalsSentRow][SentIndex] >= Options.WCDelayBeforeKill)) {
                MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Upgrading signal from %s to %s\n",
SignalName(WhichSignal),SignalName(SIGKILL));
                SignalToSend = SIGKILL;
                SignalsSent[SIGKILL_ROW][SentIndex] = TRUE;
                SendTheSignal = TRUE;
            }
        }
        if (SendTheSignal) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Killing PID %d with %s\n",
PIDsToKill[PIDsindex],SignalName(WhichSignal));
            if (kill(PIDsToKill[PIDsindex],SignalToSend) != 0) {
                MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not kill PID %d with %s\n",
PIDsToKill[PIDsindex],SignalName(SignalToSend));
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------
void StartChildProgram(OptionsType Options,char * CGroupProcsFile,int PIDOfRLR) {

    int ChildPID;
    FILE* ShellFile;
    String ShellCommand;
    char *MyArgV[MAX_ARGS];
    int MyArgC;

    ChildPID = getpid();
    MyPrintf(Options,VERBOSITY_DEBUG,TRUE,"In CHILD with PID %d\n",ChildPID);
    MySnprintf(ShellCommand,MAX_STRING,"cat > %s",CGroupProcsFile);
    if ((ShellFile = popen(ShellCommand,"w")) == NULL) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not open %s for writing\n",CGroupProcsFile);
    }
    fprintf(ShellFile,"%d\n",ChildPID);
    pclose(ShellFile);

    MyArgC = 0;
    MyArgV[MyArgC] = strtok(Options.ProgramToControl," ");
    while (MyArgV[MyArgC++] != NULL) {
        MyArgV[MyArgC] = strtok(NULL," ");
    }
//----Tell RLR the timing should start
    MyPrintf(Options,VERBOSITY_DEBUG,TRUE,
"Child %d tells parent %d and grandparent %d to start monitoring\n",ChildPID,getppid(),PIDOfRLR);
    if (kill(getppid(),SIGCONT) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not signal parent %d to start timing\n",
getppid());
    }
    if (kill(PIDOfRLR,SIGCONT) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not signal grandparent %d to start timing\n",
PIDOfRLR);
    }

    MyPrintf(Options,VERBOSITY_DEBUG,TRUE,"Child %d about to execvp %s\n",ChildPID,
Options.ProgramToControl);
//----Note all signal handling is reset
    execvp(MyArgV[0],MyArgV);
    MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Child %d could not execvp %s\n",ChildPID,
Options.ProgramToControl);
}
//--------------------------------------------------------------------------------------------------
void StartChildProcessing(OptionsType Options,char * CGroupProcsFile,char * CPUStatFile,
int PIDOfRLR) {

    struct sigaction SignalHandling;
    int ChildPID;
    int Pipe[2];
    FILE* PipeReader;
    String ChildOutput;
    String TimeStamp;

//----Ignore user interrupts, let RLR kill the cgroup with SIGUSR1 so this ends naturally
    SignalHandling.sa_handler = SIG_IGN;
    if (sigaction(SIGINT,&SignalHandling,NULL) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not ignore ^C signal");
    }

    if (pipe(Pipe) != 0) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not create pipe to catch child output");
    }

    if ((ChildPID = fork()) == -1) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not fork() for child program");
    }

    if (ChildPID == 0) {
        close(Pipe[0]);
        dup2(Pipe[1],STDOUT_FILENO);
        dup2(Pipe[1],STDERR_FILENO);
        setbuf(stdout,NULL);
        StartChildProgram(Options,CGroupProcsFile,PIDOfRLR);
    } else {
        strcpy(TimeStamp,"");
        close(Pipe[1]);
        PipeReader = fdopen(Pipe[0],"r");
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,
"Wait for child %d to add itself to the cgroup %s\n",ChildPID,CGroupProcsFile);
        pause();
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Child %d has started\n",ChildPID);
//----Start the clocks, hopefully about the same time as the parent - both signalled by job
        CPUUsage(Options,CPUStatFile,NULL,NULL);
        WCUsage(Options);
        MyPrintf(Options,VERBOSITY_DEBUG,TRUE,"Start reading from child %d\n",ChildPID);
        while (fgets(ChildOutput,MAX_STRING,PipeReader) != NULL) {
            if (Options.TimeStamps) {
//----Output WC/CPU
                MySnprintf(TimeStamp,MAX_STRING,"%6.2f/%6.2f\t",WCUsage(Options),
CPUUsage(Options,CPUStatFile,NULL,NULL));
            }
            MyPrintf(Options,VERBOSITY_STDOUT_ONLY,TRUE,"%s%s",TimeStamp,ChildOutput);
            if (Options.ProgramOutputFile != NULL) {
                fprintf(Options.ProgramOutputFile,"%s%s",TimeStamp,ChildOutput);
            }
        }
        MyPrintf(Options,VERBOSITY_DEBUG,TRUE,"Finished reading from child %d\n",ChildPID);
        fclose(PipeReader);
        if (Options.TimeStamps) {
            MySnprintf(TimeStamp,MAX_STRING,"%6.2f/%6.2f\t",WCUsage(Options),
CPUUsage(Options,CPUStatFile,NULL,NULL));
            MyPrintf(Options,VERBOSITY_STDOUT_ONLY,TRUE,"%sEOF\n",TimeStamp);
            fflush(stdout);
            if (Options.ProgramOutputFile != NULL) {
                fprintf(Options.ProgramOutputFile,"%sEOF\n",TimeStamp);
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------
void ReportResourceUsage(OptionsType Options,CGroupFileNamesType CGroupFileNames,double WCLost) {

    double CPUUsed, WCUsed, RAMUsed, CPUUsedByUser, CPUUsedBySystem;
    FILE* VarFile;

    CPUUsed = CPUUsage(Options,CGroupFileNames.CPUStatFile,&CPUUsedByUser,&CPUUsedBySystem);
//----WC might be 1s too high due to sleep in loop to allow processes to die
    WCUsed = WCUsage(Options) - WCLost;
    RAMUsed = RAMUsage(Options,CGroupFileNames.RAMStatFile,TRUE);

    MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,"Final CPU usage: %6.2fs\n",CPUUsed);
    MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,"Final WC  usage: %6.2fs\n",WCUsed);
    MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,"Final RAM usage: %6.2fMiB\n",RAMUsed);

    if (strlen(Options.VarFileName) > 0) {
        if ((VarFile = fopen(Options.VarFileName,"w")) == NULL) {
            MyPrintf(Options,VERBOSITY_ERROR,TRUE,
"Could not open %s for writing\n",Options.VarFileName);
        }

        fprintf(VarFile,"# WCTIME: wall clock time in seconds\n");
        fprintf(VarFile,"WCTIME=%.2f\n",WCUsed);
        fprintf(VarFile,"# CPUTIME: CPU time in seconds\n");
        fprintf(VarFile,"CPUTIME=%.2f\n",CPUUsed);
        fprintf(VarFile,"# USERTIME: CPU time spent in user mode in seconds\n");
        fprintf(VarFile,"USERTIME=%.2f\n",CPUUsedByUser);
        fprintf(VarFile,"# SYSTEMTIME: CPU time spent in system mode in seconds\n");
        fprintf(VarFile,"SYSTEMTIME=%.2f\n",CPUUsedBySystem);
        fprintf(VarFile,"# CPUUSAGE: CPUTIME/WCTIME in percent\n");
        fprintf(VarFile,"CPUUSAGE=%.1f\n",100.0*CPUUsed/(WCUsed != 0 ? WCUsed : 1));
        fprintf(VarFile,"# MAXVM: maximum virtual memory used in MiB\n");
        fprintf(VarFile,"MAXVM=%.2f\n",RAMUsed);

        fclose(VarFile);
    }
}
//--------------------------------------------------------------------------------------------------
void MonitorDescendantProcesses(OptionsType Options,CGroupFileNamesType CGroupFileNames) {

    int NumberOfProccessesInCGroup;
    BOOLEAN DoneSomeKilling;
    int PIDsInCGroup[MAX_PIDS];
    double CPUUsed, WCUsed, RAMUsed;
    double LastWCUsed;
    struct timespec SleepRequired;
    double WCLost;

    WCLost = 0.0;
    LastWCUsed = 0.0;
    SleepRequired.tv_sec = (int)Options.WCDelayBetweenResourceMonitoring;
    SleepRequired.tv_nsec = (int)(fmod(Options.WCDelayBetweenResourceMonitoring,1.0) * 1000000000);
//----Watch the processes
    NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupFileNames.CGroupProcsFile,
PIDsInCGroup);
    while (NumberOfProccessesInCGroup > 0) {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Number of processes is:   %d\n",
NumberOfProccessesInCGroup);
        DoneSomeKilling = FALSE;
//----Always get resource usages for reporting, even if not limiting
        CPUUsed = CPUUsage(Options,CGroupFileNames.CPUStatFile,NULL,NULL);
        WCUsed = WCUsage(Options);
        RAMUsed = RAMUsage(Options,CGroupFileNames.RAMStatFile,FALSE);
        if (Options.CPULimit > 0 && CPUUsed > Options.CPULimit) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"CPU limit reached, killing processes\n");
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGXCPU,WCUsed);
            DoneSomeKilling = TRUE;
        }
        if (Options.WCLimit > 0 && WCUsed > Options.WCLimit) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"WC limit reached, killing processes\n");
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGALRM,WCUsed);
            DoneSomeKilling = TRUE;
        }
        if (Options.RAMLimit > 0 && RAMUsed > Options.RAMLimit) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"RAM limit reached, killing processes\n");
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGTERM,WCUsed);
            DoneSomeKilling = TRUE;
        }
        if (GlobalInterrupted) {
            MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"User interrupt, killing processes\n");
            KillProcesses(Options,NumberOfProccessesInCGroup,PIDsInCGroup,SIGUSR1,WCUsed);
            DoneSomeKilling = TRUE;
        }
        if (WCUsed - LastWCUsed > Options.WCDelayBetweenResourceUsageReports) {
            LastWCUsed = WCUsed;
            MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,"CPU: %.2fs WC: %.2fs RAM: %.2fMiB\n",
CPUUsed,WCUsed,RAMUsed);
        }
        nanosleep(&SleepRequired,NULL);
        WCLost = SleepRequired.tv_sec + SleepRequired.tv_nsec/1000000000.0;
//----Reap zombies
//        if (DoneSomeKilling) {
//            while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
//                MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,
//"Reaped killed or exited process %d\n",ReapedPID);
//            }
//        }
        NumberOfProccessesInCGroup = NumberOfProcesses(Options,CGroupFileNames.CGroupProcsFile,
PIDsInCGroup);
    }
    MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"No processes left\n");
//----Reap zombie child (should not exist)
//    while ((ReapedPID = waitpid(-1,NULL,WNOHANG)) > 0) {
//        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Reaped exited process %d\n",ReapedPID);
//    }

//----Final report, including VarFile
    ReportResourceUsage(Options,CGroupFileNames,WCLost);
}
//--------------------------------------------------------------------------------------------------
void CleanUp(OptionsType Options,CGroupFileNamesType * CGroupFileNames) {

    static CGroupFileNamesType * CGroupFileNamesPtr = NULL;

//----If no saved CGroupFileNamesPtr, use the one supplied (hopefully not NULL)
    if (CGroupFileNamesPtr == NULL) {
        CGroupFileNamesPtr = CGroupFileNames;
        return;
    }

//----Use new CGroupFileNames if provided
    if (CGroupFileNames != NULL) {
        CGroupFileNamesPtr = CGroupFileNames;
    }

    if (Options.ProgramOutputFile != NULL) {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Program output saved in %s\n",
Options.ProgramOutputFileName);
        fclose(Options.ProgramOutputFile);
    }

//----If no CGroupFileNames, do nothing (should never happen)
    if (CGroupFileNamesPtr == NULL) {
        printf("DANGER DANGER cannot CleanUp CGroup\n");
    } else {
        RemoveCGroupDirectory(Options,*CGroupFileNamesPtr);
    }

    if (Options.RLROutputFile != NULL) {
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"RLR output saved in %s\n",
Options.RLROutputFileName);
        fclose(Options.RLROutputFile);
    }
}
//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    OptionsType Options;
    CGroupFileNamesType CGroupFileNames;
    CPUArchitectureType CPUArchitecture;
    int ParentPID;
    int ChildPID;
    int ChildStatus;

    CGroupFileNames = InitializeCGroupFileNames();
    Options = InitializeOptions();
//----Save a pointer to the CGroupFileNames
    CleanUp(Options,&CGroupFileNames);
    CPUArchitecture = GetCPUArchitecture(Options);
    Options = ProcessOptions(Options,CPUArchitecture,argc,argv);

    SetUpSignalHandling(Options);
    ParentPID = getpid();
    CGroupFileNames = MakeCGroupFileNames(Options,CGroupFileNames,ParentPID);
    MakeCGroupDirectory(Options,CGroupFileNames);
    LimitCores(Options,CPUArchitecture,CGroupFileNames);
    MyPrintf(Options,VERBOSITY_RESOURCE_USAGE,TRUE,
"CPU limit %d, WC limit %d, RAM limit %d, Program %s\n",Options.CPULimit,Options.WCLimit,
Options.RAMLimit,Options.ProgramToControl);

    if ((ChildPID = fork()) == -1) {
        MyPrintf(Options,VERBOSITY_ERROR,TRUE,"Could not fork() for child processing");
    }

    if (ChildPID == 0) {
        StartChildProcessing(Options,CGroupFileNames.CGroupProcsFile,CGroupFileNames.CPUStatFile,
ParentPID);
        exit(EXIT_SUCCESS);
    } else {
        MyPrintf(Options,VERBOSITY_DEBUG,TRUE,"In RLR with PID %d\n",ParentPID);
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,
"Wait for child of %d to add itself to the cgroup %s\n",ChildPID,CGroupFileNames.CGroupProcsFile);
        pause();
        MyPrintf(Options,VERBOSITY_RLR_ACTIONS,TRUE,"Child of %d has started\n",ChildPID);
//----Start the clocks
        CPUUsage(Options,CGroupFileNames.CPUStatFile,NULL,NULL);
        WCUsage(Options);
        MonitorDescendantProcesses(Options,CGroupFileNames);
        waitpid(ChildPID,&ChildStatus,0);
    }
    CleanUp(Options,&CGroupFileNames);

    return(EXIT_SUCCESS);
}
//--------------------------------------------------------------------------------------------------
