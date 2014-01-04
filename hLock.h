#ifndef HLOCK_H
#define HLOCK_H

typedef unsigned long long ULong;

typedef ULong * HLock_t;

#define Free 0
#define Held 1

char CAS(volatile ULong *loc, ULong old, ULong newval);

int Acquire(HLock_t lock);

void Release(HLock_t lock);

#endif // HLOCK_H
