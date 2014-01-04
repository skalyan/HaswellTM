#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>

#include "FallbackLock.hpp"
#include "TransRegion.hpp"

#define MAX_THREADS 8
#define MAX_RETRIES 4

#define ALIGN64 __attribute__ ((aligned (64)))

#define CACHE_LINE_BYTES 64

typedef unsigned long long ULong;
int DumpLevel  = 0;
int MaxRetries = 5;

#ifdef USE_RTM
#define TX_BEGIN(fbLock, nLockAcqs, txSucc, maxRetries)  { \
    TransRegion tx((fbLock), (nLockAcqs), (txSucc), (maxRetries), (DumpLevel));
#define TX_END(fbLock)                         } 
#else
#define TX_BEGIN(fbLock, nLockAcqs, txSucc, maxRetries)  { \
    (fbLock)->lock();
#define TX_END(fbLock)                                     \
    (fbLock)->unlock();                                    \
                                               }
#endif

typedef void* (*ThreadWorker_t)(void *);

// volatile HLock_t lock1 ALIGN64;

int NumIters 	= (0x1 << 12);
int NumThreads 	= 2;
int VerboseFlag = 0;
int RTMEnabled 	= 1;

ULong SharedSum       ALIGN64 = 0;
ULong SharedIndex     ALIGN64 = 0;
ULong* SharedArray    ALIGN64 = NULL;
volatile ULong* SharedArrayPtr ALIGN64 = NULL;

pthread_t *ThreadArray;
pthread_mutex_t PmxLock = PTHREAD_MUTEX_INITIALIZER;
pthread_spinlock_t PspLock;

FallbackLock* GLock = NULL;

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
executeThreadLoop(void* data)
{
    int k, txr = 0;
    int threadID = (ULong) data;
    int lacqs = 0;
    int txS = 0;
    long long uv = 0;

    printf("Thread ID: %d\n", threadID);
    for (k = 0; k < NumIters; ++k) {
	unsigned int nLockAcqs = 0;
        bool txSucc = false;
	TX_BEGIN(GLock, nLockAcqs, txSucc, MaxRetries)

        *SharedArrayPtr += threadID;

        // *SharedArrayPtr = threadID;
        // SharedArrayPtr += 1;
        //
        // *sptr = ((threadID-1) * NumIters) + k;

	TX_END(GLock)
        if (nLockAcqs != 0) {
            lacqs += nLockAcqs;
        }
        if (txSucc == true) {
            ++txS;
        }
        for (int i=1*(rand()%9), uv=0; i >0; --i) {
            ++uv;
        }
    }
    printf("Thread: %d  TXs: %d LockAcqs: %d [%lu]\n", threadID, txS, lacqs, uv);
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
    printf("Shared-Sum : %lu\n", SharedArray[1]);
}

void
printSharedArray(int numThreads, int numIters)
{
    int i;

    printf("Assembled Array \n");
    for (i = 0; i < numThreads * numIters; i++) {
        printf(" %d", SharedArray[i]);
    }
    printf("\n");
}

int
main(int argc, char *argv[])
{
    int c;
    ThreadWorker_t worker = executeThreadLoop;

    while (1) {
        static struct option longOptions[] = {
            {"verbose",   no_argument,       &VerboseFlag, 1},
            {"iters",     required_argument, 0,            'i'},
            {"threads",   required_argument, 0,            't'},
            {"dumplevel", required_argument, 0,            'd'},
            {"numretries",required_argument, 0,            'r'},
            {0, 0, 0, 0}
        };
        int optionIndex = 0;
        c = getopt_long(argc, argv, "i:t:d:r:", longOptions, &optionIndex);

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
            default:
                abort();
        };
    }
    
    printf(" NumIters = %d \n", NumIters);

    /* SharedArray = (ULong*) malloc(((8+1) * (NumIters+1) + 1) * sizeof(ULong)); */
    int* rawPtr = (int*) malloc(64*1024*1024 * sizeof(ULong));

    SharedArray = (ULong*)((char*) rawPtr + (CACHE_LINE_BYTES -
		 	   ((ULong)rawPtr & 0x3f)));

    memset(SharedArray, 0, 64*1024*1024 * sizeof(ULong));

    SharedArrayPtr = &SharedArray[1];

    //GLock = new MutexFallbackLock(PmxLock);
    GLock = new SpinFallbackLock(PspLock);
    
    createLoopThreads(NumThreads, worker);

    waitForThreads(NumThreads);

    printf("All threads finished; count = %d\n", SharedIndex);

    printCheckSumOfArray(NumThreads, NumIters);

    if (VerboseFlag)
        printSharedArray(NumThreads, NumIters);

    return 0;
}

