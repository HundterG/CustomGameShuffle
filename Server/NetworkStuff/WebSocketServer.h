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
	virtual void SendDataToClient(SocketBase *socket, void const *data, unsigned int size) = 0;
};

class WebSocketFunctions
{
public:
	virtual void OnNewConnection(WebSocketServerBase *server, SocketBase *socket, char const *userAgent) = 0;
	virtual void OnDataReceived(WebSocketServerBase *server, SocketBase *socket, unsigned char const *buffer, unsigned int bufferSize, unsigned int totalSize) = 0;
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
		WorkerEvent() { for(int i=0 ; i<32 ; ++i) connections[i] = nullptr; data = nullptr; }
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
		union
		{
			SharedData *data;
			unsigned char firstByte;
		};
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
		bool doSafetySleep = false;
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
				doSafetySleep = false;
			}
			else if(doSafetySleep)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			else
				doSafetySleep = true;
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

#define CLOSE(__code, __string, __stringSize) \
{ \
	unsigned char header[4]; \
	header[0] = 0x88; \
	header[1] = (unsigned char)(__stringSize + 2); \
	uint16_t code = uint16_t(__code); \
	*reinterpret_cast<uint16_t*>(header + 2) = htobe16(code); \
	connection->WriteData(header, 4); \
	connection->WriteString(__string); \
	connection->Close(); \
	return; \
} 
	
	void ProccessReadData(SocketBase *connection, unsigned char firstByte, FunctionsType &functions)
	{
		
		unsigned int packetSize = 0;

		// proccess until the timeout
		// will need to do custom timeout stuff
		Timeout timeout(NS_TIMEOUT);
		bool lastPacket = (firstByte & 0x80) != 0;
		int opCode = firstByte & 0x0F;
		unsigned char secondByte = connection->ReadByte(timeout);
		if(secondByte & 0x7F < 126)
			packetSize = secondByte & 0x7F;
		else if(secondByte & 0x7F == 126)
		{
			uint16_t sizeBE = 0;
			connection->ReadData(timeout, &sizeBE, 2);
			packetSize = be16toh(sizeBE);
		}
		else if(secondByte & 0x7F == 127)
		{
			uint64_t sizeBE = 0;
			connection->ReadData(timeout, &sizeBE, 8);
			uint64_t size = be64toh(sizeBE);
			if(UINT32_MAX < size)
				CLOSE(1009, "Message is too big", 18);
			packetSize = (unsigned int)(size);
		}

		if(NS_MAX_UPLOAD_SIZE < packetSize)
			CLOSE(1009, "Message is too big", 18);

		bool doXorHash = secondByte & 0x80 != 0;
		unsigned char xorHash[4] = {0};
		if(doXorHash)
			connection->ReadData(timeout, xorHash, 4);

		if(opCode == 0 || opCode == 1 || opCode == 2) // Data
		{
			unsigned int readLength = 0;
			unsigned char buffer[64 * 1024] = {0};
			while(readLength < packetSize)
			{
				unsigned int readThisLoop = std::min((unsigned int)(64*1024), packetSize-readLength);
				connection->ReadData(timeout, buffer, readThisLoop);

				if(doXorHash)
				{
					for(unsigned int i=0 ; i<readThisLoop ; ++i)
						buffer[i] ^= xorHash[(readLength + i) & 0x03];
				}

				functions.OnDataReceived(this, connection, buffer, readThisLoop, packetSize);
				functions.Reset();
				readLength += readThisLoop;
			}
		}
		else if(opCode == 8) // Close
		{
			CLOSE(1000, "Bye Bye", 7);
		}
		else if(opCode == 9) // Ping
		{
			if(125 < packetSize)
				CLOSE(1002, "Bad Ping", 8);

			unsigned char data[125];
			connection->ReadData(timeout, data, packetSize);
			if(doXorHash)
			{
				for(unsigned int i=0 ; i<packetSize ; ++i)
					data[i] ^= xorHash[i & 0x03];
			}
			
			unsigned char header[2];
			header[0] = 0x8A;
			header[1] = secondByte;
			connection->WriteData(header, 2);
			connection->WriteData(data, packetSize);
		}
		else if(opCode == 10) // Pong
		{
			if(125 < packetSize)
				CLOSE(1002, "Bad Pong", 8);
			
			unsigned char data[125];
			connection->ReadData(timeout, data, packetSize);
		}
		else
			CLOSE(1002, "Bad OpCode", 10);
	}

#undef CLOSE

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
				try
				{
					ProccessReadData(event.connections[0]->connection, event.firstByte, functions);
					event.connections[0]->workerReadRef = false;
				}
				catch(...)
				{
					event.connections[0]->connection->Close();
				}
				break;

			case WorkerEvent::SendData:
				for(int i=0 ; i<32 ; ++i)
				{
					ConnectionPair *connection = event.connections[i];
					if(connection != nullptr)
					{
						try
						{
							connection->connection->WriteData(event.data->data, event.data->size);
						}
						catch(...)
						{
							connection->connection->Close();
						}
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
					if(connection.workerReadRef == false)
					{
						unsigned char firstByte = 0;
						try
						{
							if(connection.connection->ReadSomeData(&firstByte, 1) == 1)
							{
								std::unique_lock<std::mutex> lock(workerLock);
								workerEvents.emplace_back();
								WorkerEvent &newEvent = workerEvents.back();
								newEvent.type = WorkerEvent::ReadData;
								newEvent.connections[0] = &connection;
								newEvent.firstByte = firstByte;
								newEvent.connections[0]->workerReadRef = true;
								workerCV.notify_one();
							}
						}
						catch(...)
						{
							connection.connection->Close();
						}
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

		SharedData *newData = (SharedData*)(malloc(sizeof(SharedData) + 10 + size));
		new(newData) SharedData;

		unsigned int headerSize = 10;
		newData->data[0] = 0x82; // FIN bit (7) set with an opcode of 2 (binary data)
		if(size < 126)
		{
			newData->data[1] = (unsigned char)(size);
			headerSize = 2;
		}
		else if(size < INT16_MAX)
		{
			newData->data[1] = 126; // magic number for "next 2 bytes have the data length for this frame"
			int16_t size16 = int16_t(size);
			*reinterpret_cast<uint16_t*>(newData->data + 2) = htobe16(size16);
			headerSize = 4;
		}
		else
		{
			newData->data[1] = 127; // magic number for "next 8 bytes have the data length for this frame"
			int64_t size64 = size;
			*reinterpret_cast<uint64_t*>(newData->data + 2) = htobe64(size64);
		}
		std::memcpy(newData->data + headerSize, data, size);
		newData->size = size + headerSize;

		std::lock_guard<std::mutex> lock(watcherLock);
		watcherEvents.emplace_back();
		WatcherEvent &event = watcherEvents.back();
		event.type = WatcherEvent::SendData;
		event.data = newData;
	}

	void SendDataToClient(SocketBase *socket, void const *data, unsigned int size)
	{
		unsigned char header[10];
		header[0] = 0x82; // FIN bit (7) set with an opcode of 2 (binary data)

		unsigned int headerSize = 10;
		if(size < 126)
		{
			header[1] = (unsigned char)(size);
			headerSize = 2;
		}
		else if(size < INT16_MAX)
		{
			header[1] = 126; // magic number for "next 2 bytes have the data length for this frame"
			int16_t size16 = int16_t(size);
			*reinterpret_cast<uint16_t*>(header + 2) = htobe16(size16);
			headerSize = 4;
		}
		else
		{
			header[1] = 127; // magic number for "next 8 bytes have the data length for this frame"
			int64_t size64 = size;
			*reinterpret_cast<uint64_t*>(header + 2) = htobe64(size64);
		}

		socket->WriteData(header, headerSize);
		socket->WriteData(data, size);
	}
};

#endif
