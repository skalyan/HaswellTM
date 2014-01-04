#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <cassert>
#include <climits>
#include <iostream>

#include "hLock.h"

#include "FallbackLock.hpp"
#include "RHash.hpp"
#include "Timer.hpp"
#include "TransRegion.hpp"


#define MAX_THREADS 8
#define MAX_RETRIES 4

#define NUM_BUCKETS 4173

#define ALIGN64 __attribute__ ((aligned (64)))
#define CACHE_LINE_BYTES 64

int MaxRetries  = MAX_RETRIES;
bool TxSucc     = false;
int TraceLevel  = 0;

typedef unsigned long long ULong;

FallbackLock::LockType_t LockType = FallbackLock::MUTEX_LOCK; // By default, use Mutex Lock

#ifdef USE_RTM
#define TX_BEGIN(fbLock, nAborts, MaxRetries)  { \
    TransRegion tx((fbLock), (nAborts), (TxSucc), (MaxRetries), (TraceLevel));
#define TX_END(fbLock)                         } 
#elif NO_LOCKING
#define TX_BEGIN(fbLock, nAborts, MaxRetries)  { 
#define TX_END(fbLock)                         } 
#else
#define TX_BEGIN(fbLock, nAborts, MaxRetries)  { \
    (fbLock)->lock();
#define TX_END(fbLock)                           \
    (fbLock)->unlock();                          \
                                               }
#endif


typedef void* (*ThreadWorker_t)(void *);

typedef struct {
	unsigned int _pad1[15];
	unsigned int data;
} CLThrData_t ALIGN64;

CLThrData_t* CLArray = NULL;

typedef struct {
    int threadId_;
    int nEntries_;
	int wSpace_;
    KaLib::HashTable<int>* htPtr_;
	CLThrData_t* thArr_;
} WorkerArgs_t;

// volatile HLock_t lock1 ALIGN64;
pthread_t *ThreadArray;

pthread_mutex_t PmxLock = PTHREAD_MUTEX_INITIALIZER;
pthread_spinlock_t PspLock;

FallbackLock* GLock;

int NumIters 	  = (0x1 << 12);
int NumThreads 	  = 2;
int NumEntries    = 1024*1024;
int VerboseFlag   = 0;
int RTMEnabled 	  = 1;
int UpdateSpacing = 0;

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

int
FibonacciR(int num) {
    if ((num == 0) || (num == 1)) {
        return num;
    }
    return FibonacciR(num-1) + FibonacciR(num-2);
}

int
FibonacciI(int num) {
    if ((num == 0) || (num == 1)) {
        return num;
    }
    int f_2 = 0;
    int f_1 = 1;
    for (int i = 2; i < num; ++i) {
        int f = f_1 + f_2;
        f_2 = f_1;
        f_1 = f;
    }
    return f_1;
}

static unsigned int ebx = 0;
unsigned int eax = 1;

void *
executeThreadLoopSimple(void* data)
{
    WorkerArgs_t *wArg = (WorkerArgs_t*)data;
    int threadID       = (int) wArg->threadId_;
    int lnEntries      = wArg->nEntries_;
    int updateIters    = wArg->wSpace_;
    int nAborts        = 0;
    int tlSum          = 0;
	int v              = 217;

    srand( time(NULL) );    
    cout << "Thread ID: " <<  threadID << endl;

	Timer threadWorker("Worker");
	threadWorker.Start();

    for (int uic = updateIters, ec = wArg->nEntries_; ec > 0; --ec, --uic) {
        int k = rand() % NUM_BUCKETS;
        unsigned int nacqs = 0;


		FibonacciI(10);

      __asm__ __volatile__(".byte 0xf2,0xf0; add %%eax, (%%ebx)\n"
                           : : "a"(eax), "b"(&ebx) : "memory");

        if (uic == 0) {
        	++wArg->thArr_[threadID].data;
			uic = updateIters;
		} else {
		    tlSum += wArg->thArr_[threadID].data;
		}

		FibonacciI(100);

		__asm__ __volatile__(".byte 0xf3,0xf0; sub %%eax, (%%ebx)\n"
		                     : : "a"(eax), "b"(&ebx) : "memory");

        nAborts += nacqs;
    }

	threadWorker.Stop();
	cout << " nAborts for Thread " << threadID << " is " << nAborts << endl;

    cout << "Thread " << threadID << " took " << threadWorker.ElapsedTime();

    cout << "Thread " << threadID << " Sum " << tlSum << endl;
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
        wArr[i].wSpace_   = UpdateSpacing;
        wArr[i].htPtr_    = SharedTable;
        wArr[i].thArr_    = CLArray;
      
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
            cout << "thread_join failed for " << i << endl;
            exit(-1);
        } else {
            cout << "thread " << i << " finished" << endl;
        }
    }
    return fStatus;
}

bool
checkTable(const KaLib::HashTable<int>& hTable,
           int nThreads, int nEntries)
{
    unsigned int expectedItems = nThreads * nEntries;
    unsigned int itemCount = hTable.countNumEntries();
    cout << " CheckTable: Expected " << expectedItems
         << " Operations " << hTable.getNumOps() << endl;
    hTable.printStats(std::cout);
    return (expectedItems == hTable.getNumOps());
}

int
processArgs(int argc, char *argv[])
{
    int c;
	float wsplit = 0.0;

    while (1) {
        static struct option longOptions[] = {
            {"verbose",    no_argument,       &VerboseFlag,  1 },
            {"iters",      required_argument, 0,            'i'},
            {"entries",    required_argument, 0,            'e'},
            {"threads",    required_argument, 0,            't'},
            {"TraceLevel", required_argument, 0,            'd'},
            {"numretries", required_argument, 0,            'r'},
            {"locktype",   required_argument, 0,            'l'},
            {"worksplit",  required_argument, 0,            's'},
            {0, 0, 0, 0}
        };
        int optionIndex = 0;
        c = getopt_long(argc, argv, "i:e:t:d:r:l:s:", longOptions, &optionIndex);

        if (c == -1)
            break;

        switch (c) {
            case 0:
                if (longOptions[optionIndex].flag != 0)
                    break;
                cout << "option "
                     << longOptions[optionIndex].name
                     << endl;
                break;
            case 'i':
                NumIters = atoi(optarg);
				assert (NumIters > 0 && NumEntries < INT_MAX);
                break;
            case 'e':
                NumEntries = atoi(optarg);
				assert (NumEntries > 100 && NumEntries < INT_MAX);
                break;
            case 't':
                NumThreads = atoi(optarg);
				assert (NumThreads > 0 && NumThreads <= 8);
                break;
            case 'd':
                TraceLevel = atoi(optarg);
                break;
            case 'r':
                MaxRetries = atoi(optarg);
				assert (MaxRetries >= 0 && MaxRetries <= 16);
                break;
            case 'l': {
                int ltype = atoi(optarg);
                assert((ltype >= 1) && (ltype <= 4));
                LockType = static_cast<FallbackLock::LockType_t>(ltype);
                break;
            }
            case 's': {
				cout << "wsplit arg is " << optarg << endl;
                wsplit = atof(optarg);
				cout << "wsplit is " << wsplit << endl;
                assert((wsplit >= 0) && (wsplit <= 100));
	
                break;
            }
            default:
                abort();
        };
    }
    if (wsplit == 0) {
		UpdateSpacing = NumEntries;
	} else {
		UpdateSpacing = NumEntries / ((wsplit * (float) NumEntries) / 100);
	}
	cout << "Update Spacing is " << UpdateSpacing << endl;
}

int
main(int argc, char *argv[])
{

    processArgs(argc, argv);

    cout << " NumIters " << NumIters << ","
         << " NumEntries " <<  NumEntries << endl;

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
    }

    SharedTable = new KaLib::HashTable<int>(
        NumEntries * NumThreads, GLock);
    for (int ec = NumThreads * NumEntries; ec > 0; --ec) {
        int v = rand();

       	SharedTable->insert(ec, v);
    }

    CLArray = (CLThrData_t*) malloc(NumThreads * sizeof(CLThrData_t));

    ThreadWorker_t worker = executeThreadLoopSimple;
	Timer coreTimer("Workers");

	coreTimer.Start();

    createWorkerThreads(NumThreads, worker);
    waitForThreads(NumThreads);

	coreTimer.Stop();

	Timer checker("VerifyHashTable");
	checker.Start();

    checkTable(*SharedTable, NumThreads, NumEntries);

	checker.Stop();

    delete SharedTable;

	coreTimer.PrintElapsedTime("RESULT: Filling up the hash table finished in ");

    checker.PrintElapsedTime("Verification of Hash table done in ");

    return 0;
}

