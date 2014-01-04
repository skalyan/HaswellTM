#ifndef TIMER_HPP
#define TIMER_HPP

#include <exception>
#include <string>
#include <sys/time.h>
#include <iostream>

using namespace std;
class Timer {
public:

	Timer(const string& name) : name_(name)
	{ } 

	void Start() {
		gettimeofday(&startTime_, NULL);
	}

	void Stop() {
		gettimeofday(&stopTime_, NULL);
	}

	void Reset() {
		startTime_ = {0,0};
		stopTime_  = {0,0};
	}

    uint64_t rdtsc(void) {
		unsigned lo, hi;
		uint64_t v;
		asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
		v = hi;
		v <<= 32;
		v |= lo;
		return v;
    }

    uint64_t get_time_ns(void) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return (tv.tv_sec * 1000000000ULL) + (tv.tv_usec * 1000ULL);
	}

	double ElapsedTime() const throw() {
		long seconds       = 0;
		long useconds      = 0;
		double elapsedTime = 0.0;
	
		seconds = stopTime_.tv_sec - startTime_.tv_sec;
		useconds = stopTime_.tv_usec - startTime_.tv_usec;

		elapsedTime = seconds + useconds/1000000.0;

		if (elapsedTime < 0.0) {
			throw("Bad state: negative elapsed time");
		}

		return elapsedTime;
	}

	void PrintElapsedTime(const string& msg) const {
		try {
			cout << "Timer " << name_ << ":";
			cout << msg << " " << ElapsedTime();
			cout << endl;
		}
		catch (...) {
			cout << "Unable to compute elapased time!\n";
			throw;
		}
	}

private:
	const string& name_;
	timeval startTime_;
	timeval stopTime_;
};

#endif // TIMER_HPP
