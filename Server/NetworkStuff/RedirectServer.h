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
	char errorResponse[28] = 
	{
		'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', 
		'4', '0', '0', ' ', 'B', 'A', 'D', ' ', 'R', 'E', 'Q', 'U', 'E', 'S', 'T',
		0x0D, 0x0A,
		0x0D, 0x0A
	};

	static bool IsASCII(unsigned char c)
	{
		return 32 <= c && c < 128;
	}

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

#define RETURNBADREQUEST(__msg) { socket->WriteData(errorResponse, 28); /*std::cout << __msg << "\n";*/ socket->Close(); return; }

	void DoRequest(SocketBase *socket)
	{
		Timeout timeout(NS_TIMEOUT);
		bool sizeGood = false;
		for(int i=0 ; i<30 ; ++i)
		{
			unsigned char c = socket->ReadByte(timeout);
			if(timeout.GetCancel()) { socket->Close(); return; }
			if(c == ' ')
			{
				sizeGood = true;
				break;
			}
		}
		if(!sizeGood)
			RETURNBADREQUEST("Invalid size in code");

		sizeGood = false;
		char uri[1024] = {0};
		for(int i=0 ; i<1020 ; ++i)
		{
			unsigned char c = socket->ReadByte(timeout);
			if(timeout.GetCancel()) { socket->Close(); return; }
			if(c == ' ')
			{
				sizeGood = true;
				break;
			}
			if(!IsASCII(c))
				RETURNBADREQUEST("Invalid ASCII in url");
			uri[i] = char(c);
		}
		if(!sizeGood)
			RETURNBADREQUEST("Invalid size in url");

		socket->WriteData(response, responseSize);
		socket->WriteString(uri);
		socket->WriteCRLF();
		socket->WriteCRLF();
	}

#undef RETURNBADREQUEST

	void AsyncAccepterFunction(void)
	{
		bool doSafetySleep = false;
		while(accepter.IsGood() && !shouldClose)
		{
			SocketBase *newConnection = accepter.Accept();
			if(newConnection)
			{
				try
				{
					if(newConnection->Prepare())
						DoRequest(newConnection);
				}
				catch(...)
				{}
				newConnection->Close();
				accepter.HandleDelete(newConnection);
				doSafetySleep = false;
			}
			else if(doSafetySleep)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			else
				doSafetySleep = true;
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
