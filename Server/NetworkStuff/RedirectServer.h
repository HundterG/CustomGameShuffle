#ifndef REDIRECTSERVER_H_
#define REDIRECTSERVER_H_

#include <thread>
#include <atomic>
#include "Socket.h"

template<typename ListenerType>
class RedirectServer
{
	std::thread asyncListener;
	ListenerType accepter;
	std::atomic_bool running = ATOMIC_VAR_INIT(false);
	std::atomic_bool shouldClose = ATOMIC_VAR_INIT(false);
	char response[4 * 1024] = {0};
	int responseSize = 0;

	void MakeResponse(char const *location)
	{
		auto writeString = [&](char const *s)
		{
			while(*s != 0 && responseSize < (4 * 1024))
			{
				response[responseSize] = *s;
				++s;
				++responseSize;
			}
		};
		auto writeCRLF = [&]()
		{
			if((responseSize + 1) < (4 * 1024))
			{
				response[responseSize] = 0x0D;
				response[responseSize + 1] = 0x0A;
				responseSize += 2;
			}
		};

		writeString("HTTP/1.1 ");
		writeString("301 REDIRECT");
		writeCRLF();
		writeString("Content-Length: 0");
		writeCRLF();
		writeString("Location: ");
		writeString(location);
		writeCRLF();
		writeCRLF();
	}

	bool StartFunction(uint16_t port, char const *location)
	{
		bool expected = false;
		if(!running.compare_exchange_strong(expected, true))
			return false;
		shouldClose = false;
		MakeResponse(location);

		if(!accepter.Start(port))
		{
			running = false;
			return false;
		}

		return true;
	}

	static void AsyncAccepterFunctionCaller(RedirectServer *server)
	{
		server->AsyncAccepterFunction();
	}

	void AsyncAccepterFunction(void)
	{
		while(accepter.IsGood() && !shouldClose)
		{
			SocketBase *newConnection = accepter.Accept();
			if(newConnection)
			{
				if(newConnection->Prepare())
					newConnection->WriteData(response, responseSize);
				newConnection->Close();
				accepter.HandleDelete(newConnection);
			}
		}
		running = false;
	}

public:
	~RedirectServer()
	{
		if(running)
		{
			shouldClose = true;
			accepter.Stop();
			while(running)
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			if(asyncListener.joinable())
				asyncListener.join();
		}

	}

	void Run(uint16_t port, char const *location)
	{
		if(!StartFunction(port, location))
			return;
		AsyncAccepterFunction();
	}

	bool RunAsync(uint16_t port, char const *location)
	{
		if(!StartFunction(port, location))
			return false;
		asyncListener = std::thread(AsyncAccepterFunctionCaller, this);
		return true;
	}
};

#endif
