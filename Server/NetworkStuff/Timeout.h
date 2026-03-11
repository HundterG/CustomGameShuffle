#ifndef TIMEOUT_H_
#define TIMEOUT_H_

#include <chrono>

class Timeout
{
	std::chrono::time_point<std::chrono::system_clock> start;
	std::chrono::seconds timeout;
	bool cancel = false;
public:
	Timeout(int seconds) : start(std::chrono::system_clock::now()), timeout(seconds) {}
	bool IsTimeoutReached(void) { return start + timeout < std::chrono::system_clock::now(); }
	void SetCancel(void) { cancel = true; }
	bool GetCancel(void) { return cancel; }
};
#endif
