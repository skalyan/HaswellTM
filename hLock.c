#include "hLock.h"

#define CAS_LOOP 1
#define LOCKING_ON 1

char CAS(volatile ULong* loc, ULong oldval, ULong newval)
{
	char result;
	__asm__ __volatile__(
#ifdef TURN_ON_HLE
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

int Acquire(HLock_t lock)
{
#ifdef LOCKING_ON
#ifdef CAS_LOOP
	while (CAS((ULong*)lock, Free, Held) == 0) {
		while (*lock == Held);
		/* Implement some back-off here */
	}
#else
	CAS((ULong*)lock, Free, Held);
#endif // CAS_LOOP
#endif // LOCKING_ON
	return 0;
}

int TxTest(void)
{
	unsigned char al;
	__asm__ __volatile__(".byte 0x0f, 0x01, 0xd6\n"
						 "setnz %%al\n"
						 : "=a"(al) : : "cc");
	return al;
}

/* void Release(volatile HLock_t lck) */
void Release(HLock_t lck)
{
	__asm__ __volatile__(
#ifdef TURN_ON_HLE
		".byte 0xf3 \n"
#endif
		"movw %1, %0\n"
		: "=m" (*lck)
		: "i" (Free)
		: "memory"
	);
}
