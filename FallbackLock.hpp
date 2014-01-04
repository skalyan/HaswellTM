#ifndef FALLBACKLOCK_HPP
#define FALLBACKLOCK_HPP

extern "C" {
#include "hLock.h"
#include "sched.h"
};

#include <pthread.h>
#include <iostream>

#define CACHE_LINE_BYTES 64

using namespace std;

class FallbackLock {
public:
   typedef enum {
      MUTEX_LOCK = 1,
      SPIN_LOCK,
      HLE_LOCK,
      CUSTOM_LOCK
   } LockType_t;
   enum { Unlocked = 0, Locked = 1 };
public:
   FallbackLock()
      : state_(Unlocked)
   {
   }

   ~FallbackLock()
   {
      state_ = Unlocked;
   }

   virtual void lock()
   {
      state_ = Locked;
   }

   virtual void unlock()
   {
      state_ = Unlocked;
   }

   virtual bool isLocked()
   {
      return (state_ == Locked);
   }


   char
   CAS(volatile ULong* loc, ULong oldval, ULong newval)
   {
      char result;
      __asm__ __volatile__(
#ifdef HLE_ON
      	 ".byte 0xf2\n"
#endif
     	 "lock\n"
     	 "cmpxchgw %2, %1\n"
     	 "sete %0\n"
     	 : "=q" (result), "=m" (*loc)
     	 : "r" (newval), "m" (*loc), "a" (oldval)
     	 : "memory"
      );
      return result;
   }

   char
   SimpleCAS(volatile ULong* loc, ULong oldval, ULong newval)
   {
      char result;
      __asm__ __volatile__(
     	 "lock\n"
     	 "cmpxchgw %2, %1\n"
     	 "sete %0\n"
     	 : "=q" (result), "=m" (*loc)
     	 : "r" (newval), "m" (*loc), "a" (oldval)
     	 : "memory"
      );
      return result;
   }

private:
   volatile unsigned int state_;
   unsigned int padding[15];
};


class HLELock : public FallbackLock {
public:
   HLELock() {
      rawPtr_ = (int*) malloc(2 * sizeof(ULong));

      hLock_ = (HLock_t)rawPtr_;
      //hLock_ = (ULong*)((char*) rawPtr_ + (CACHE_LINE_BYTES - ((ULong)rawPtr_ & 0x3f)));

      *hLock_ = Free;
   }

   ~HLELock() {
      if (*hLock_ != Free) {
         cerr << " HLE Lock in invalid state upon destruction " << endl;
      }
      hLock_ = NULL;
      free(rawPtr_);
   }

   void lock()
   {
      HLE_Acquire(hLock_);
      // FallbackLock::lock();
   }

   void unlock()
   {
      // FallbackLock::unlock();
      HLE_Release(hLock_);
   }

private:

   
   int
   HLE_Acquire(HLock_t lock)
   {
	  int backOff = 8;
      while (CAS((ULong*)lock, Free, Held) == 0) {
   		 /* Implement better back-off here */
         for (int c = backOff; c > 0; --c);
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
   HLE_Release(volatile HLock_t lck)
   {
   	__asm__ __volatile__(
#ifdef HLE_ON
   		".byte 0xf3 \n"
#endif
   		"movw %1, %0\n"
   		: "=m" (*lck)
   		: "i" (Free)
   		: "memory"
   	);
   }

private:
   int* rawPtr_;
   HLock_t hLock_;
};

class SpinFallbackLock : public FallbackLock {
public:
   SpinFallbackLock(pthread_spinlock_t& splock)
      : pspLock_(splock)
   {
      pthread_spin_init(&pspLock_, PTHREAD_PROCESS_PRIVATE);
   }

   ~SpinFallbackLock()
   {
      pthread_spin_destroy(&pspLock_);
   }

   void lock()
   {
      pthread_spin_lock(&pspLock_);
      FallbackLock::lock();
   }

   void unlock()
   {
      FallbackLock::unlock();
      pthread_spin_unlock(&pspLock_);
   }

private:
   pthread_spinlock_t& pspLock_;
};

class MutexFallbackLock : public FallbackLock {
public:
   MutexFallbackLock(pthread_mutex_t& mxlock)
      : mxlock_(mxlock)
   {
      pthread_mutex_init(&mxlock_, NULL);
   }

   ~MutexFallbackLock()
   {
      pthread_mutex_destroy(&mxlock_);
   }

   void lock()
   {
      pthread_mutex_lock(&mxlock_);
      // FallbackLock::lock();
   }

   void unlock()
   {
      // FallbackLock::unlock();
      pthread_mutex_unlock(&mxlock_);
   }

private:
   pthread_mutex_t& mxlock_;
};

class CustomSpinLock : public FallbackLock {
public:
   CustomSpinLock() {
      lock_ = (int*) malloc(64 * sizeof(char));

      //lock_ = (ULong*)((char*) rawPtr_ + (CACHE_LINE_BYTES - ((ULong)rawPtr_ & 0x3f)));

      *lock_ = Free;
   }

   ~CustomSpinLock() {
      if (*lock_ != Free) {
         cerr << " CS Lock in invalid state upon destruction " << endl;
      }
      free(lock_);
   }

   void lock()
   {
	  int backOff = 8;

      if (!lock_) { 
         cerr << "Lock in an inconsistent state" << endl;
	  }
      while (SimpleCAS((ULong*)lock_, Free, Held) == 0) {
        for (int c = backOff; c > 0; --c);
		backOff <<= 1;
   	  }
   }

   void unlock()
   {
      *lock_ = Free; 
      // FallbackLock::unlock();
   }

   bool isLocked() {
	  return (*lock_ ==  Held);
   }

private:
   int* lock_;
};

class NopLock : public FallbackLock {
public:
   NopLock() {
      lock_ = (int*) malloc(64 * sizeof(char));

      //lock_ = (ULong*)((char*) rawPtr_ + (CACHE_LINE_BYTES - ((ULong)rawPtr_ & 0x3f)));

      *lock_ = Free;
   }

   ~NopLock() {
      free(lock_);
   }

   void lock()
   {
	  int backOff = 8;

      if (!lock_) { 
         cerr << "Lock in an inconsistent state" << endl;
	  }
	  *lock_ = Held;
   }

   void unlock()
   {
      *lock_ = Free; 
   }

   bool isLocked() {
	  return (*lock_ ==  Held);
   }

private:
   int* lock_;
};

#endif
