#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>

#include <sys/time.h>

#include "Timer.hpp"

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
int CollisionFactor = 0x1;
int CollisionFilter = (CollisionFactor - 1);


typedef void* (*ThreadWorker_t)(void *);

ULong hLock_[64] ALIGN64;

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


extern "C" {
#include "hLock.h"
};

   char
   CAS(volatile ULong* loc, ULong oldval, ULong newval)
   {
      char result;
      __asm__ __volatile__(
      	 ".byte 0xf2\n"
     	 "lock\n"
     	 "cmpxchgw %2, %1\n"
     	 "sete %0\n"
     	 : "=q" (result), "=m" (*loc)
     	 : "r" (newval), "m" (*loc), "a" (oldval)
     	 : "memory"
      );
      return result;
   }

   void AllocHLELock() {
      int *rawPtr_ = (int*) malloc(256 * sizeof(ULong));

      // hLock_ = (ULong*)((char*) rawPtr_ + (CACHE_LINE_BYTES - ((ULong)rawPtr_ & 0x3f)));

      // *hLock_ = Free;
      pthread_spin_init(&PspLock, 0);
	  memset(hLock_, 0, 64*sizeof(int));
   }

   void FreeHLELock() {
      if (*hLock_ != Free) {
         cerr << " HLE Lock in invalid state upon destruction " << endl;
      }
   }

   int
   HLE_Acquire(ULong* lock)
   {
	  int backOff = 4;
      while (CAS((ULong*)lock, Free, Held) == 0) {
   		 if (*lock == Held) {
   		    /* Implement better back-off here */
            for (int c = backOff; c > 0; --c);
         }
		 backOff <<= 1;
   	  }
   	  return 0;
   }
   
   int
   HLE_TxTest(void)
   {
   	  unsigned char al;
   	  __asm__ __volatile__(".byte 0x0f, 0x01, 0xd6\n"
   	                       "setnz %%al\n"
   						  : "=a"(al) : : "cc");
   	  return al;
   }
   
   void
   HLE_Release(ULong* lck)
   {

	char result;
    
    __asm__ __volatile__(
        ".byte 0xf3 \n"
        "movw %1, %0\n"
        : "=m" (*lck)
        : "i" (Free)
        : "memory"
    );

   }


void *
executeThreadLoopShared(void* data)
{
    int k, txr = 0;
    int threadID = (ULong) data;
	int ind = threadID - 1;

    for (k = 0; k < NumIters; ++k) {
		ind = ((k & (CollisionFilter)) != 0) ? (threadID << 4) : 0;
        pthread_spin_lock(&PspLock);

        SharedArray[ind] += threadID;
        // *sptr = ((threadID-1) * NumIters) + k;
        pthread_spin_unlock(&PspLock);

    }
    return NULL;
}

void *
executeThreadLoopPrivate(void* data)
{
    int k, txr = 0;
    int threadID = (ULong) data;

	int ind = (threadID) << 4;
    for (k = 0; k < NumIters; ++k) {
		ind = ((k & CollisionFactor) != 0) ? (threadID << 4) : ind;

        pthread_spin_lock(&PspLock);
        SharedArray[ind] += threadID;
        // *sptr = ((threadID-1) * NumIters) + k;
        pthread_spin_unlock(&PspLock);

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
            // printf("pthread %d finished\n", i);
        }
    }
    printf("All threads finished\n", i);
    return fStatus;
}

void
printCheckSumOfArray(int numThreads, int numIters)
{
    int i;
    unsigned long long sum = 0;

    for (i = 0, sum = SharedArray[0]; i <= numThreads * 16 ; i += 16, sum += SharedArray[i]) {
        printf("%d ", SharedArray[i]);
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
            {"collisionFactor",required_argument, 0,       'c'},
            {0, 0, 0, 0}
        };
        int optionIndex = 0;
        c = getopt_long(argc, argv, "i:t:d:r:l:s:c:", longOptions, &optionIndex);

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
            case 'c':
                CollisionFactor = atoi(optarg);
				CollisionFilter = (CollisionFactor - 1);
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

    /* SharedArray = (ULong*) malloc(((8+1) * (NumIters+1) + 1) * sizeof(ULong)); */
    int* rawPtr = (int*) malloc(64*1024*1024 * sizeof(ULong));

    SharedArray = (int*)((char*) rawPtr + (CACHE_LINE_BYTES -
		 	   ((ULong)rawPtr & 0x3f)));

    memset(SharedArray, 0, 64*256 * sizeof(int));

	if (SharedMode) {
    	worker = executeThreadLoopShared;
	} else {
    	worker = executeThreadLoopPrivate;
	}

	AllocHLELock();
    
	Timer coreTimer("Worker Threads");

	coreTimer.Start();
    createLoopThreads(NumThreads, worker);

    waitForThreads(NumThreads);
	coreTimer.Stop();

    printCheckSumOfArray(NumThreads, NumIters);

    coreTimer.PrintElapsedTime("Worker threads finished in|");
    cout << "Final Index = \n" << SharedIndex;

    if (VerboseFlag)
        printSharedArray(NumThreads, NumIters);

    return 0;
}
