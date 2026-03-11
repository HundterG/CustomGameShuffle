#ifndef WEBSOCKETSERVER_H_
#define WEBSOCKETSERVER_H_

#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "Socket.h"
#include <deque>
#include <chrono>
#include <cstring>
#include <list>
#include "HTTPRequestProccess.h"
#include "IXWebSocketHandshakeKeyGen.h"

class WebSocketServerBase
{
public:
	virtual void BroadcastData(void const *data, unsigned int size) = 0;
};

class WebSocketFunctions
{
public:
	virtual void OnNewConnection(WebSocketServerBase *server, SocketBase *socket, char const *userAgent) = 0;
	virtual void OnDataReceived(WebSocketServerBase *server, SocketBase *socket) = 0;
	virtual void OnConnectionClosed(WebSocketServerBase *server, SocketBase *socket) = 0;
	virtual void Reset(void) {}
};

template<typename FunctionsType, typename ListenerType, int ThreadCount>
class WebSocketServer : public WebSocketServerBase
{
	static_assert(0 < ThreadCount, "Invalid ThreadCount. Must be more than 0");
	static_assert(std::is_base_of<ListenerBase, ListenerType>::value, "ListenerType is not a child class of ListenerBase");
	static_assert(std::is_base_of<WebSocketFunctions, FunctionsType>::value, "FunctionsType is not a child class of WebSocketFunctions");

	struct ConnectionPair
	{
		SocketBase *connection = nullptr;
		std::atomic_bool workerReadRef = ATOMIC_VAR_INIT(false);
		std::atomic_int workerWriteRef = ATOMIC_VAR_INIT(0);
	};

	struct SharedData
	{
		std::atomic_int refCount = ATOMIC_VAR_INIT(0);
		unsigned int size = 0;
		unsigned char data[];
	};

	struct WorkerEvent
	{
		WorkerEvent() { for(int i=0 ; i<32 ; ++i) connections[i] = nullptr; }
		enum Type
		{
			Empty,
			NewConnection,
			ReadData,
			SendData,
		} type = Type::Empty;
		union
		{
			SocketBase *newConnection;
			ConnectionPair *connections[32];
		};
		SharedData *data = nullptr;
	};

	struct WatcherEvent
	{
		WatcherEvent() { newConnection = nullptr; }
		enum Type
		{
			Empty,
			NewConnection,
			SendData,
		} type = Type::Empty;
		union
		{
			SocketBase *newConnection;
			SharedData *data;
		};
	};

	class WebSocketHTTPHandshake : public HTTPRequestProccess
	{
	public:
		char userAgent[1024] = {0};
		void DoRequest(SocketBase *socket)
		{
			bool gotUpgrade = false;
			bool gotVersion = false;
			char secKey[1024] = {0};
			ParseHelper(socket,
				[](char const *requestCode, char const *HTTPVersion, char const *uri) { return (std::strcmp(requestCode, "GET") == 0) ? HelperFunctionReturn::OK : HelperFunctionReturn::FORBIDDEN; },
				[&](char const *headerKey, char const *headerValue)
				{
					if(std::strcmp(headerKey, "sec-websocket-version") == 0)
					{
						if(std::strcmp(headerValue, "13") != 0)
						{
							std::pair<char const*, char const*> version = {"Sec-WebSocket-Version", "13"};
							WriteResponse(socket, "400 BAD REQUEST", &version, 1, nullptr, 0);
							return HelperFunctionReturn::BAIL;
						}
						else
							gotVersion = true;
					}
					else if(std::strcmp(headerKey, "upgrade") == 0)
					{
						char const *expectedValue = "websocket";
						for(int i=0 ;; ++i)
						{
							if(std::tolower(headerValue[i]) != expectedValue[i])
								break;
							if(expectedValue[i] == 0)
							{
								gotUpgrade = true;
								return HelperFunctionReturn::OK;
							}
						}
						// special firefox variant
						if(std::strcmp(headerValue, "keep-alive, Upgrade") == 0)
							gotUpgrade = true;
					}
					else if(std::strcmp(headerKey, "sec-websocket-key") == 0)
						WebSocketHandshakeKeyGen::generate(headerValue, secKey);
					else if(std::strcmp(headerKey, "user-agent") == 0)
						std::strcpy(userAgent, headerValue);

					return HelperFunctionReturn::OK;
				},
				[](unsigned char const *buffer, unsigned int bufferSize, uint64_t totalSize){ return HelperFunctionReturn::BADREQUEST; },
				[](SocketBase *socket, char const *requestCode, char const *uri){ return HelperFunctionReturn::OK; }
			);

			if(gotUpgrade && gotVersion)
			{
				std::pair<char const*, char const*> headers[] = 
				{
					{"Upgrade", "websocket"},
					{"Connection", "Upgrade"},
					{"Server", "NS_WebSocketServer"},
					{"Sec-WebSocket-Accept", nullptr}
				};
				int headersSize = 3;

				if(secKey[0] != 0)
				{
					headers[3].second = secKey;
					++headersSize;
				}

				// we do our own response here to not set the content length header
				socket->WriteString("HTTP/1.1 101 Switching Protocols");
				socket->WriteCRLF();

				for(int i=0 ; i<headersSize ; ++i)
				{
					std::pair<char const*, char const*> &header = headers[i];
					socket->WriteString(header.first);
					socket->WriteString(": ");
					socket->WriteString(header.second);
					socket->WriteCRLF();
				}
				socket->WriteCRLF();
			}
			else
			{
				WriteResponse(socket, "400 BAD REQUEST", nullptr, 0, nullptr, 0);
				socket->Close();
			}
		}
	};

	std::mutex workerLock;
	std::condition_variable workerCV;
	std::deque<WorkerEvent> workerEvents;

	volatile char padding[64];

	std::mutex watcherLock;
	std::condition_variable watcherCV;
	std::deque<WatcherEvent> watcherEvents;

	volatile char padding2[64];

	std::thread workerThreadPool[ThreadCount];
	std::thread asyncListener;
	std::thread watcherThread;
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
			workerThreadPool[i] = std::thread(WorkerThreadFunctionCaller, this);

		watcherThread = std::thread(WatcherThreadFunctionCaller, this);

		return true;
	}

	static void AsyncAccepterFunctionCaller(WebSocketServer *server)
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
				std::unique_lock<std::mutex> lock(workerLock);
				workerEvents.emplace_back();
				WorkerEvent &event = workerEvents.back();
				event.type = WorkerEvent::NewConnection;
				event.newConnection = newConnection;
				workerCV.notify_one();
			}
		}

		shouldClose = true;
		for(int i=0 ; i<5 ; ++i)
		{
			workerCV.notify_all();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}

		for(int i=0 ; i<ThreadCount ; ++i)
			workerThreadPool[i].join();

		watcherThread.join();

		running = false;
	}

	static void WorkerThreadFunctionCaller(WebSocketServer *server)
	{
		server->WorkerThreadFunction();
	}

	void WorkerThreadFunction(void)
	{
		FunctionsType functions;
		functions.Reset();

		while(!shouldClose)
		{
			WorkerEvent event;
			{
				std::unique_lock<std::mutex> lock(workerLock);
				while(event.type == WorkerEvent::Empty)
				{
					if(shouldClose)
						return;
					if(0 < workerEvents.size())
					{
						event = workerEvents.front();
						workerEvents.pop_front();
					}
					if(event.type == WorkerEvent::Empty)
						workerCV.wait(lock);
				}
			}

			switch(event.type)
			{
			case WorkerEvent::NewConnection:
				try
				{
					if(event.newConnection->Prepare())
					{
						WebSocketHTTPHandshake handShake;
						handShake.DoRequest(event.newConnection);
						if(event.newConnection->IsOpen())
						{
							functions.OnNewConnection(this, event.newConnection, handShake.userAgent);
							functions.Reset();

							std::lock_guard<std::mutex> lock(watcherLock);
							watcherEvents.emplace_back();
							WatcherEvent &outEvent = watcherEvents.back();
							outEvent.type = WatcherEvent::NewConnection;
							outEvent.newConnection = event.newConnection;
						}
						else
						{
							event.newConnection->Close();
							accepter.HandleDelete(event.newConnection);
						}
					}
					else
					{
						event.newConnection->Close();
						accepter.HandleDelete(event.newConnection);
					}
				}
				catch(...)
				{
					event.newConnection->Close();
					accepter.HandleDelete(event.newConnection);
				}
				break;

			case WorkerEvent::ReadData:
				functions.OnDataReceived(this, event.connections[0]->connection);
				functions.Reset();
				event.connections[0]->workerReadRef = false;
				break;

			case WorkerEvent::SendData:
				for(int i=0 ; i<32 ; ++i)
				{
					ConnectionPair *connection = event.connections[i];
					if(connection != nullptr)
					{
						connection->connection->WriteData(event.data->data, event.data->size);
						--connection->workerWriteRef;
					}
				}

				if(--event.data->refCount <= 0)
					free(event.data);
				break;

			default:
				break;
			}
		}
	}

	static void WatcherThreadFunctionCaller(WebSocketServer *server)
	{
		server->WatcherThreadFunction();
	}

	void WatcherThreadFunction(void)
	{
		FunctionsType functions;
		functions.Reset();
		std::chrono::time_point<std::chrono::system_clock> lastSocketLoop;
		std::list<ConnectionPair> connections;

		while(!shouldClose)
		{
			WatcherEvent event;
			lastSocketLoop = std::chrono::system_clock::now();

			{
				std::unique_lock<std::mutex> lock(watcherLock);
				while(event.type == WatcherEvent::Empty)
				{
					if(shouldClose)
						return;
					if(0 < watcherEvents.size())
					{
						event = watcherEvents.front();
						watcherEvents.pop_front();
					}
					if(event.type == WatcherEvent::Empty)
					{
						if(lastSocketLoop + std::chrono::seconds(1) < std::chrono::system_clock::now())
							break;
						else
							watcherCV.wait_for(lock, std::chrono::seconds(1));
					}
				}
			}

			switch(event.type)
			{
			case WatcherEvent::NewConnection:
			{
				connections.emplace_back();
				ConnectionPair &newConnection = connections.back();
				newConnection.connection = event.newConnection;
			}
				break;

			case WatcherEvent::SendData:
				if(!connections.empty())
				{
					std::unique_lock<std::mutex> lock(workerLock);
					ConnectionPair *tempConnections[32];
					int tempConnectionPos = 0;
					int totalEventsSent = 0;

					for(auto & connection : connections)
					{
						if(32 <= tempConnectionPos)
						{
							workerEvents.emplace_back();
							WorkerEvent &newEvent = workerEvents.back();
							newEvent.type = WorkerEvent::SendData;
							for(int i=0 ; i<32 ; ++i)
								newEvent.connections[i] = tempConnections[i];
							newEvent.data = event.data;
							tempConnectionPos = 0;
							++totalEventsSent;
						}

						if(connection.connection->IsOpen())
						{
							++connection.workerWriteRef;
							tempConnections[tempConnectionPos] = &connection;
							++tempConnectionPos;
						}
					}

					if(0 < tempConnectionPos)
					{
						workerEvents.emplace_back();
						WorkerEvent &newEvent = workerEvents.back();
						newEvent.type = WorkerEvent::SendData;
						for(int i=0 ; i<tempConnectionPos ; ++i)
							newEvent.connections[i] = tempConnections[i];
						newEvent.data = event.data;
						++totalEventsSent;
					}
					
					if(0 < totalEventsSent)
					{
						event.data->refCount = totalEventsSent;
						for(int i=0 ; i<totalEventsSent ; ++i)
							workerCV.notify_one();
					}
					else
					{
						free(event.data);
					}
				}
				else
				{
					free(event.data);
				}
				break;
			}

			for(auto & connection : connections)
			{
				if(connection.connection->IsOpen())
				{
					if(connection.connection->HasData() && connection.workerReadRef == false)
					{
						std::unique_lock<std::mutex> lock(workerLock);
						workerEvents.emplace_back();
						WorkerEvent &newEvent = workerEvents.back();
						newEvent.type = WorkerEvent::ReadData;
						newEvent.connections[0] = &connection;
						newEvent.connections[0]->workerReadRef = true;
						workerCV.notify_one();
					}
				}
				else if(connection.workerReadRef == false && connection.workerWriteRef == 0)
				{
					functions.OnConnectionClosed(this, connection.connection);
					functions.Reset();
					accepter.HandleDelete(connection.connection);
					connection.connection = nullptr;
				}
			}
			connections.remove_if([](ConnectionPair &connection){ return connection.connection == nullptr; });
		}
	}

public:
	WebSocketServer() { for(int i=0 ; i<64 ; ++i) padding[i] = padding2[i] = 0; }
	~WebSocketServer()
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

	void BroadcastData(void const *data, unsigned int size)
	{
		if(!running)
			return;
			
		SharedData *newData = (SharedData*)(malloc(sizeof(SharedData) + size));
		new(newData) SharedData;
		newData->size = size;
		std::memcpy(newData->data, data, size);

		std::lock_guard<std::mutex> lock(watcherLock);
		watcherEvents.emplace_back();
		WatcherEvent &event = watcherEvents.back();
		event.type = WatcherEvent::SendData;
		event.data = newData;
	}
};

#endif
