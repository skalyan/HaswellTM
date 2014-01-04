#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>

#include "RHash.hpp"

#include "FallbackLock.hpp"
#include "TransRegion.hpp"

#define MAX_THREADS 8
#define MAX_RETRIES 4


#define ALIGN64 __attribute__ ((aligned (64)))

#define CACHE_LINE_BYTES 64

typedef unsigned long long ULong;
int DumpLevel = 1;
int MaxRetries = 5;

#ifdef USE_RTM
#define TX_BEGIN(fbLock, nAborts, maxRetries)  { \
    TransRegion tx((fbLock), (nAborts), (maxRetries), (DumpLevel));
#define TX_END(fbLock)                         } 
#else
#define TX_BEGIN(fbLock, nAborts, maxRetries)  { \
    fbLock.lock();
#define TX_END(fbLock)                           \
    (fbLock).unlock();                           \
                                               }
#endif

typedef void* (*ThreadWorker_t)(void *);

typedef struct {
    int threadId_;
    int nEntries_;
    KaLib::HashTable<int>* htPtr_;
} WorkerArgs_t;

// volatile HLock_t lock1 ALIGN64;
pthread_t *ThreadArray;
pthread_mutex_t PmxLock1 = PTHREAD_MUTEX_INITIALIZER;
pthread_spinlock_t PspLock;
FallbackLock FallbackLockObj(PspLock);

int NumIters 	= (0x1 << 12);
int NumThreads 	= 2;
int NumEntries  = 1024*1024;
int VerboseFlag = 0;
int RTMEnabled 	= 1;

KaLib::HashTable<int>* SharedTable = NULL;

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
executeThreadLoopSimple(void* data)
{
    WorkerArgs_t *wArg = (WorkerArgs_t*)data;
    int threadID       = (int) wArg->threadId_;
    int lnEntries      = wArg->nEntries_;
    int nAborts        = 0;
    int nTries         = (threadID == 2) ? 0 : MaxRetries;

    srand( time(NULL) );    
    printf("Thread ID: %d\n", threadID);

    while (lnEntries--) {
        int k = rand();
        int v = rand();
        unsigned int nAborts = 0;

        TX_BEGIN(FallbackLockObj, nAborts, nTries)

        wArg->htPtr_->insert(k, v);

	TX_END(FallbackLockObj)
    }

    pthread_exit((void*) threadID);
    return NULL;
}

void
createWorkerThreads(int numThreads, ThreadWorker_t tWorker)
{
    int i, tStatus = 0;

    ThreadArray = (pthread_t*) malloc((numThreads + 1) * sizeof(pthread_t*));
    WorkerArgs_t *wArr = (WorkerArgs_t*) malloc((numThreads + 1) * sizeof(WorkerArgs_t));

    for (i = 1; i <= numThreads; i++)
    {
        wArr[i].threadId_ = i;
        wArr[i].nEntries_ = NumEntries;
        wArr[i].htPtr_    = SharedTable;
      
        tStatus = pthread_create(&ThreadArray[i], NULL, tWorker, (void*)&wArr[i]);
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

int
main(int argc, char *argv[])
{
    int c;
    ThreadWorker_t worker = executeThreadLoopSimple;

    while (1) {
        static struct option longOptions[] = {
            {"verbose",   no_argument,       &VerboseFlag, 1},
            {"iters",     required_argument, 0,            'i'},
            {"entries",   required_argument, 0,            'e'},
            {"threads",   required_argument, 0,            't'},
            {"dumplevel", required_argument, 0,            'd'},
            {"numretries",required_argument, 0,            'r'},
            {0, 0, 0, 0}
        };
        int optionIndex = 0;
        c = getopt_long(argc, argv, "i:e:t:d:r:", longOptions, &optionIndex);

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
            case 'e':
                NumEntries = atoi(optarg);
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
    
    printf(" NumIters = %d, NumEntries = %d \n", NumIters, NumEntries);

    SharedTable = new KaLib::HashTable<int>(
        NumEntries * NumThreads, FallbackLockObj);
        
    createWorkerThreads(NumThreads, worker);

    waitForThreads(NumThreads);

    printf("All threads finished \n");

    delete SharedTable;

    return 0;
}

