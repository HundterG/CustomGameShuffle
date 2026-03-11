#ifndef HTTPSERVER_H_
#define HTTPSERVER_H_

#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "Socket.h"
#include <deque>
#include <chrono>

template<typename RequestType, typename ListenerType, int ThreadCount> // queue count?
class HTTPServer
{
	static_assert(0 < ThreadCount, "Invalid ThreadCount. Must be more than 0");
	static_assert(std::is_base_of<HTTPRequestProccess, RequestType>::value, "RequestType is not a child class of HTTPRequestProccess");
	static_assert(std::is_base_of<ListenerBase, ListenerType>::value, "ListenerType is not a child class of ListenerBase");

	std::mutex socketLock;
	std::condition_variable socketCV;
	std::deque<SocketBase*> waitingSockets;

	volatile char padding[64];

	std::thread threadPool[ThreadCount];
	std::thread asyncListener;
	ListenerType accepter;
	std::atomic_bool running = ATOMIC_VAR_INIT(false);
	std::atomic_bool shouldClose = ATOMIC_VAR_INIT(false);

	bool StartFunction(uint16_t port)
	{
		bool expected = false;
		if(!running.compare_exchange_strong(expected, true))
			return false;
		shouldClose = false;

		if(!accepter.Start(port))
		{
			running = false;
			return false;
		}

		for(int i=0 ; i<ThreadCount ; ++i)
			threadPool[i] = std::thread(WorkerThreadFunctionCaller, this);

		return true;
	}

	static void AsyncAccepterFunctionCaller(HTTPServer *server)
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
				std::unique_lock<std::mutex> lock(socketLock);
				waitingSockets.push_back(newConnection);
				socketCV.notify_one();
			}
		}

		shouldClose = true;
		for(int i=0 ; i<5 ; ++i)
		{
			socketCV.notify_all();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}

		for(int i=0 ; i<ThreadCount ; ++i)
			threadPool[i].join();

		running = false;
	}

	static void WorkerThreadFunctionCaller(HTTPServer *server)
	{
		server->WorkerThreadFunction();
	}

	void WorkerThreadFunction(void)
	{
		RequestType functions;
		functions.Reset();
		while(!shouldClose)
		{
			SocketBase *socket = nullptr;
			{
				std::unique_lock<std::mutex> lock(socketLock);
				while(socket == nullptr)
				{
					if(shouldClose)
						return;
					if(0 < waitingSockets.size())
					{
						socket = waitingSockets.front();
						waitingSockets.pop_front();
					}
					if(socket == nullptr)
						socketCV.wait(lock);
				}
			}

			try
			{
				if(socket->Prepare())
					while(socket->IsOpen())
					{
						functions.DoRequest(socket);
						functions.Reset();
					}
			}
			catch(...)
			{
				functions.Reset();
			}
			
			socket->Close();
			accepter.HandleDelete(socket);
		}
	}

public:
	HTTPServer() { for(int i=0 ; i<64 ; ++i) padding[i] = 0; }
	~HTTPServer()
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

	void Run(uint16_t port)
	{
		if(!StartFunction(port))
			return;
		AsyncAccepterFunction();
	}

	bool RunAsync(uint16_t port)
	{
		if(!StartFunction(port))
			return false;
		asyncListener = std::thread(AsyncAccepterFunctionCaller, this);
		return true;
	}
};

#endif
