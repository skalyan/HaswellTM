#include "hash.hpp"

#include <iostream>
#include <random>
#include <chrono>

#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

using namespace std;

namespace {

int Verbosity;
int NIters;
int NThreads;
int NEntries;
int Distribution;

#define ALIGN64 __attribute__ ((aligned (64)))

typedef void* (*ThreadWorker_t)(void *);

unsigned int KeyMax;

pthread_t *WorkerThreads;

typedef struct {
    int threadId_;
    KaLib::HashTable<int>* htPtr_;
} WorkerArgs_t;

void
ProcessOptions(int argc, char** argv)
{
    while (1) {
        static struct option long_options[] =
        {
            {"verbose", no_argument,        &Verbosity,    'v'},
            {"threads", required_argument, &NThreads,      't'},
            {"entries", required_argument, &NEntries,      'e'},
            {"count",   required_argument, &NIters,          'c'},
            {"distr",   required_argument, &Distribution, 'd'},
            { 0, 0, 0, 0}
        };
    
        int option_index = 0;
        int c = getopt_long(argc, argv, "vt:e:c:", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'v':
                Verbosity = true;
                cout << ("option -verbose ") << Verbosity << endl;
                break;
            
            case 't':
                NThreads = atoi(optarg);
                cout << ("option -threads ") << NThreads << endl;
                break;
    
            case 'e':
                NEntries = atoi(optarg);
                cout << ("option -entries ") << NEntries << endl;
                break;
    
            case 'c':
                NIters = atoi(optarg);
                cout << ("option -count ") << NIters << endl;
                break;
    
            case 'd':
                Distribution = atoi(optarg);
                cout << ("option -distribution ") << Distribution << endl;
                break;

            case 'r':
                NRetries = atoi(optarg);
                cout << ("option -retries ") << Retries << endl;
                break;
            default:
                exit(1);
        }
    }
}

void*
NeedleWorkUnif(void* threadArgs)
{
    WorkerArgs_t *wArg = (WorkerArgs_t*)threadArgs;
    srand( time(NULL) );    
    long tId = (long) wArg->threadId_;

    int lnEntries = NEntries;

    while (lnEntries--) {
        int k = rand();
        int v = rand();
        wArg->htPtr_->insert(k, v);
    }
    pthread_exit((void*) tId);
    return threadArgs;
}

#ifdef NORMAL_DISTRIBUTION
void*
NeedleWorkNorm(void* threadArgs)
{
    WorkerArgs_t *wArg = (WorkerArgs_t*)threadArgs;
    long tId = (long) wArg->threadId_;
    srand( time(NULL) );

    std::default_random_engine generator;
    generator.seed( time(NULL) );
    std::normal_distribution<double> distribution(0.5, 0.50);

    int lnEntries = NEntries;

    while (lnEntries--) {
        double ks = distribution(generator);
        int k = ks * KeyMax;
        int v = rand();
        wArg->htPtr_->insert(k, v);
    }
    pthread_exit((void*) tId);
    return threadArgs;
}
#endif // NORMAL_DISTRIBUTION
    
void
Threading(int nThreads)
{
    KaLib::HashTable<int> MyTable(NEntries * nThreads);
    WorkerThreads = new pthread_t[NThreads+1];
    using namespace std::chrono;

    std::chrono::time_point<std::chrono::system_clock> start =
        std::chrono::system_clock::now();

    for (int t = 1; t <= NThreads; t = t + 1) {
        WorkerArgs_t tArg = {t, &MyTable};    
        pthread_create(&(WorkerThreads[t]), NULL, NeedleWorkUnif, (void*)&tArg);
    }

    for (int t = 1; t <= NThreads; t = t + 1) {
        pthread_join(WorkerThreads[t], NULL);
        cout << "Thread #" << t << " finished\n";
    }

    std::chrono::time_point<std::chrono::system_clock> end =
                                               std::chrono::system_clock::now();

    int elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>
                                                          (end - start).count();
    // typedef std::chrono::duration<int, std::chrono::seconds> seconds_t;
    // seconds_t duration( std::chrono::duration_cast<seconds_t>(end - start) );

    MyTable.printStats(std::cout);
    // cout << "Elapsed time (secs)spent in the Workers: " << duration.count();
    cout << "Elapsed time (secs)spent in the Workers: " << elapsed_seconds
         << endl;

    pthread_exit(NULL);
}

} // anonymous

int
main(int argc, char** argv)
{
    ProcessOptions(argc, argv);

    KeyMax = (NEntries * NThreads);

    Threading(NThreads);
}
