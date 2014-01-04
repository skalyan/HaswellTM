#ifndef TRANSREGION_HPP
#define TRANSREGION_HPP

#include "FallbackLock.hpp"

#include <stdio.h>
#include <pthread.h>

#include "hLock.h"

typedef enum {
    RTM_SUCCESS  = (~0U),
    RTM_MIN      = RTM_SUCCESS,
    RTM_EXPLICIT = (1 << 0),
    RTM_RETRY    = (1 << 1),
    RTM_CONFLICT = (1 << 2),
    RTM_OVFLOW   = (1 << 3),
    RTM_DEBUGP   = (1 << 4),
    RTM_NESTED   = (1 << 5),
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

int lockState = Free;

class TransRegion {
public:
    TransRegion(FallbackLock* fLock,
                unsigned int& nAcqs,
                bool& TxSucc,
                unsigned int  nRetries = 10,
                unsigned int  traceLevel = 0) :
        fLock_(fLock),  nRetries_(nRetries), nAcqs_(nAcqs), TxSucc_(TxSucc),
        traceLevel_(traceLevel), abortTX(0)
    {
		unsigned int tryCount = 0;
		// transLog[0] = 0;
				transLog[7] = 0;
        for (tryCount = 0; tryCount <= nRetries_; ++tryCount) {
            int status = txBegin();
            if (status == RTM_SUCCESS) {
               	if (!fLock_->isLocked()) {
                   	return;
               	} else {
                   	status = RTM_UNSAFE;
                   	txAbort(RTM_UNSAFE);
					abortTX++;
               	}
            }
            // Atomically increment the abort count.
            if ((status & RTM_EXPLICIT) ||  (((status & 0xff000000) >> 24) == RTM_UNSAFE)) {
				abortTX++;
				transLog[0] = 1;
				tryCount = nRetries_ + 1;
                while (fLock_->isLocked()) {
                // while (lockState == Held) {
                    for (int i = 100; i > 0; --i);
                }
				// Now, go retry
            } else if ((status & RTM_RETRY) != 0)    {
				transLog[1] = 1;
					// Continue to retry, if we didn't exceed the limit yet.
            } else {
				//printf("bad status %d \n", status);
				transLog[2] = 1;
				// Induce a break out of the TX loop
				tryCount = nRetries_ + 1;
			}
        }
        fLock_->lock();
    }

    ~TransRegion() {
        // if (lockState == Held) {
        if (fLock_->isLocked()) {
			lockState = Free;
            fLock_->unlock();
        } else {
            txEnd();
        }
		if (abortTX != 0) {
			// printf("transLog needs to be looked at...\n");
		}
    }

private:
    FallbackLock* fLock_;
    unsigned int  nRetries_;
    unsigned int& nAcqs_;
    bool& TxSucc_;
    unsigned int  traceLevel_;
	int transLog[7];
	int abortTX;
    
    // Private default constructor to prevent illegal initialization
    TransRegion();
};

#endif // TRANSREGION_HPP
