#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>

#include <sys/time.h>


#include "FallbackLock.hpp"
#include "Timer.hpp"
#include "TransRegion.hpp"

#define MAX_THREADS 8
#define MAX_RETRIES 4


#define ALIGN64 __attribute__ ((aligned (64)))

#define CACHE_LINE_BYTES 64

typedef unsigned long long ULong;
int DumpLevel  = 1;
int MaxRetries = 5;
int LockType   = 1; // By default, use Mutex Lock
int SharedMode = 1;
bool TxSucc    = false;


#ifdef USE_RTM
#define TX_BEGIN(fbLock, nAborts, maxRetries)  {  \
    TransRegion tx((fbLock), (nAborts), (TxSucc), \
                   (maxRetries), (DumpLevel));
#define TX_END(fbLock)                         } 
#else
#define TX_BEGIN(fbLock, nAborts, maxRetries)  {  \
    (fbLock)->lock();
#define TX_END(fbLock)                            \
    (fbLock)->unlock();                           \
                                               }
#endif

typedef void* (*ThreadWorker_t)(void *);

// volatile HLock_t lock1 ALIGN64;

int NumIters 	= (0x1 << 12);
int NumThreads 	= 2;
int VerboseFlag = 0;
int RTMEnabled 	= 1;

ULong SharedSum    ALIGN64;
ULong SharedIndex  ALIGN64 = 0;

int* SharedArray ALIGN64 = NULL;

int* SharedArrayPtr = NULL;

typedef struct {
	char _padding[60];
	int data;
} CLData_t;


pthread_t *ThreadArray;
pthread_mutex_t PmxLock = PTHREAD_MUTEX_INITIALIZER;
pthread_spinlock_t PspLock;

FallbackLock* GLock;

#define RetryRTM(status) \
    (((status) != 0) && ((status) & RTM_RETRY))

#ifdef STATS_CODE
void
RecordRTMStats(RTM_Status st, int tid)
{
    int i = 0;
    for (; (st != 0); ++i, st = st >> 1) {
        RTMStats[tid][i] += (st & 0x1);
    }
}

void
ReportRTMStats(int tid)
{
    int i = 0;
    printf(" RTM Abort Statistics: \n");
    for (; i < 7; ++i) {
        printf(" %s -> %d \n", RTM_StatusStrings[i], RTMStats[tid][i]);
    }
}
#endif // STATS_CODE

void *
executeThreadLoopShared(void* data)
{
    int k, txr = 0;
    int threadID = (ULong) data;

    printf("Thread ID: %d\n", threadID);
    for (k = 0; k < NumIters; ++k) {
	unsigned int nAborts = 0;
	TX_BEGIN(GLock, nAborts, MaxRetries)

        SharedArray[1] += threadID;
        // *sptr = ((threadID-1) * NumIters) + k;

	TX_END(GLock)
    }
    return NULL;
}

void *
executeThreadLoopPrivate(void* data)
{
    int k, txr = 0;
    int threadID = (ULong) data;

    printf("Thread ID: %d\n", threadID);
	int aIndex = (threadID-1) * 16;
    for (k = 0; k < NumIters; ++k) {
	unsigned int nAborts = 0;
	TX_BEGIN(GLock, nAborts, MaxRetries)

        SharedArray[aIndex] += threadID;
        // *sptr = ((threadID-1) * NumIters) + k;

	TX_END(GLock)
    }
    return NULL;
}

void
createLoopThreads(int numThreads, ThreadWorker_t tWorker)
{
    int i, tStatus = 0;

    SharedSum = 0;

    ThreadArray = (pthread_t*) malloc((numThreads+1) * sizeof(pthread_t*));

    for (i = 1; i <= numThreads; i++)
    {
        tStatus = pthread_create(&ThreadArray[i], NULL, tWorker, (void*)i);
        if (tStatus) {
            printf("return code from pthread_create() is %d\n", tStatus);
            exit(-1);
        }
    }
}

int
waitForThreads(int numThreads)
{
    int i, fStatus = 0;

    for (i = 1; i <= numThreads; i++)
    {
        if (pthread_join(ThreadArray[i], NULL)) {
            printf("pthread_join failed for %d\n", i);
            exit(-1);
        } else {
            printf("pthread %d finished\n", i);
        }
    }
    return fStatus;
}

void
printCheckSumOfArray(int numThreads, int numIters)
{
    int i;
    unsigned long sum = 0;

    for (i = 1, sum = SharedArray[1]; i <= numThreads * numIters; i++, sum += SharedArray[i]) {
        // printf("%d ", SharedArray[i]);
    }

    printf("\nCheck-Sum of Array: %lu\n", sum);
    printf("Shared-Sum : %llu\n", SharedSum);
}

void
printSharedArray(int numThreads, int numIters)
{
    int i;

    printf("Assembled Array \n");
    for (i = 0; i < numThreads * numIters; i++) {
        printf(" %llu", SharedArray[i]);
    }
    printf("\n");
}

int
main(int argc, char *argv[])
{
    int c;
    ThreadWorker_t worker = NULL;

    while (1) {
        static struct option longOptions[] = {
            {"verbose",   no_argument,       &VerboseFlag, 1},
            {"iters",     required_argument, 0,            'i'},
            {"threads",   required_argument, 0,            't'},
            {"dumplevel", required_argument, 0,            'd'},
            {"numretries",required_argument, 0,            'r'},
            {"locktype",  required_argument, 0,            'l'},
            {"sharedmode",required_argument, 0,            's'},
            {0, 0, 0, 0}
        };
        int optionIndex = 0;
        c = getopt_long(argc, argv, "i:t:d:r:l:s:", longOptions, &optionIndex);

        if (c == -1)
            break;

        switch (c) {
            case 0:
                if (longOptions[optionIndex].flag != 0)
                    break;
                printf("option %s\n", longOptions[optionIndex].name);
                break;
            case 'i':
                NumIters = atoi(optarg);
                break;
            case 't':
                NumThreads = atoi(optarg);
                break;
            case 'd':
                DumpLevel = atoi(optarg);
                break;
            case 'r':
                MaxRetries = atoi(optarg);
                break;
            case 'l':
                LockType = atoi(optarg);
                break;
            case 's':
                SharedMode = atoi(optarg);
                break;
            default:
                abort();
        };
    }
    
    printf(" NumIters = %d \n", NumIters);


    if (LockType == FallbackLock::MUTEX_LOCK) {
        GLock = new MutexFallbackLock(PmxLock);
        cout << "Allocated Pthread Mutex lock" << endl;
    } else if (LockType == FallbackLock::SPIN_LOCK) {
        GLock = new SpinFallbackLock(PspLock);
        cout << "Allocated Pthread Spin lock" << endl;
    } else if (LockType == FallbackLock::HLE_LOCK) {
        GLock = new HLELock();
        cout << "Allocated HLE Spin lock" << endl;
    } else if (LockType == FallbackLock::CUSTOM_LOCK) {
        GLock = new CustomSpinLock();
        cout << "Allocated Custom Spin lock" << endl;
    } else {
        cerr << "Error: invalid lock type specified, exiting..." << LockType;
		exit(1);
    }

    /* SharedArray = (ULong*) malloc(((8+1) * (NumIters+1) + 1) * sizeof(ULong)); */
    int* rawPtr = (int*) malloc(64*1024*1024 * sizeof(ULong));

    SharedArray = (int*)((char*) rawPtr + (CACHE_LINE_BYTES -
		 	   ((ULong)rawPtr & 0x3f)));

    memset(SharedArray, 0, 64*1024*1024 * sizeof(int));

	if (SharedMode) {
    	worker = executeThreadLoopShared;
	} else {
    	worker = executeThreadLoopPrivate;
	}

    
	Timer coreTimer("Worker Threads");

	coreTimer.Start();
    createLoopThreads(NumThreads, worker);

    waitForThreads(NumThreads);
	coreTimer.Stop();


    printCheckSumOfArray(NumThreads, NumIters);

    coreTimer.PrintElapsedTime("Worker threads finished in ");
    cout << "Final Index = \n" << SharedIndex;

    if (VerboseFlag)
        printSharedArray(NumThreads, NumIters);

    return 0;
}

