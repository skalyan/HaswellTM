#ifndef TRANSREGION_HPP
#define TRANSREGION_HPP

#include <stdio.h>
#include <pthread.h>

#include "FallbackLock.hpp"

typedef enum {
    RTM_SUCCESS  = -1,
    RTM_ZERO     = 0x0,
    RTM_MIN      = RTM_ZERO,
    RTM_ABORT    = 0x1,
    RTM_RETRY    = 0x2,
    RTM_CONFLICT = 0x4,
    RTM_OVFLOW   = 0x8,
    RTM_DEBUGP   = 0x10,
    RTM_NESTED   = 0x20,
    RTM_USER     = 0x40,
    RTM_UNSAFE   = 0xff,
    RTM_MAX      = RTM_UNSAFE
} RTM_Status;

/*
char *RTM_StatusStrings[] = {
    "RTM_SUCCESS",
    "RTM_ZERO",
    "RTM_ABORT",
    "RTM_RETRY",
    "RTM_CONFLICT",
    "RTM_OVERFLOW",
    "RTM_DEBUG",
    "RTM_NESTED",
    "RTM_USER"
    "RTM_UNSAFE"
};
*/

#define TX_SUCCESS -1

#define TRACE_PRINT(tLevel, cLevel, msg)   \
    if ((tLevel) >= (cLevel)) {            \
        printf((msg));                     \
    }                                

int
txBegin()
{
    int rv;
    __asm__ __volatile__("movl $-1, %%eax\n"
                         ".byte 0xc7, 0xf8, 0, 0, 0, 0\n"
                         :"=a" (rv) : :);
   return rv;
}

void
txEnd()
{
    __asm__ __volatile__(".byte 0x0f, 0x01, 0xd5\n");
}

void
txAbort(int s)
{
    __asm__ __volatile__(".byte 0xc6, 0xf8, 0xff\n");
}

int
txTest()
{
    int rv = 0;
    __asm__ __volatile__(".byte 0x0f, 0x01, 0xd6\n"
                         :"=a" (rv));
}

class TransRegion {
public:
    TransRegion(FallbackLock* fLock,
                unsigned int& nAcqs,
                bool& TxSucc,
                unsigned int  nRetries = 20,
                unsigned int  traceLevel = 0) :
        fLock_(fLock),  nRetries_(nRetries), nAcqs_(nAcqs), TxSucc_(TxSucc),
        traceLevel_(traceLevel), locked(0)
    {
		unsigned tryCount = 0;
        for (tryCount = 0; tryCount <= nRetries_; ++tryCount) {
            int status = txBegin();
            if (status == TX_SUCCESS) {
                if (txTest()) {
                	if (!fLock_->isLocked()) {
                    	return;
                } else {
					printf("TxBegin staus ==== %d\n", status);
                    // Abort the TX, as some other thread is executing the critical
                    // section in a non-RTM way, i.e. acquired the fallback lock.
                    TRACE_PRINT(traceLevel_, 1, "Another thread entered the region "
                                "non transactionally. Aborting...\n")
                    txAbort(RTM_UNSAFE);
                    status = RTM_UNSAFE;
                }
            }
            // Atomically increment the abort count.
            if (status == RTM_UNSAFE) {
					 printf("TxUnsafe  %d \n", status);
                while (locked) {
                    for (int i = 1000; i > 0; --i);
                }
            } else if ((status & RTM_RETRY))    {
					 // printf("TxRetry  %d \n", status);
            } else {
                tryCount = nRetries_ + 1;
			}
        }
        fLock_->lock();
		locked = 1;
    }

    ~TransRegion() {
        if (locked) {
            fLock_->unlock();
        } else {
            txEnd();
        }
    }

private:
    FallbackLock* fLock_;
    unsigned int  nRetries_;
    unsigned int& nAcqs_;
    bool& TxSucc_;
    unsigned int  traceLevel_;
	unsigned int locked;
    
    // Private default constructor to prevent illegal initialization
    TransRegion();
};

#endif // TRANSREGION_HPP
