#ifndef NORMALSOCKET_H_
#define NORMALSOCKET_H_

#include "Socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <net/if.h>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>

//#include <iostream>

class PosixListener;

class PosixSocket : public SocketBase
{
	friend PosixListener;
	int socket = -1;
public:
	~PosixSocket()
	{
		if(0 <= socket)
		{
			close(socket);
			socket = -1;
		}
	}

	bool Prepare(void)
	{
		return true;
	}

	bool IsOpen(void)
	{
		return 0 <= socket;
	}

	void Close(void)
	{
		//std::cout << "close called : ";
		if(0 <= socket)
		{
			close(socket);
			socket = -1;
			//std::cout << "Done\n";
		}
		//std::cout << "No\n";
	}

	void WriteData(void const *data, unsigned int size)
	{
		if(socket < 0)
			return;

		//unsigned char const *dataC = reinterpret_cast<unsigned char const*>(data);

		//std::cout << "Writing " << size << " bytes to (" << socket << ")\n";
		//std::cout << "\t";
		//for(unsigned int i=0 ; i<size ; ++i)
		//{
		//	char c = dataC[i];
		//	if(32 <= c && c < 128)
		//		std::cout.put(c);
		//	else
		//		std::cout << "(" << int(c) << ")";
		//}
		//std::cout << "\n";

		unsigned int sent = 0;
		unsigned char const *dataC = reinterpret_cast<unsigned char const*>(data);
		while(sent < size)
		{
			int ret = write(socket, dataC + sent, size - sent);
			if(ret < 0)
			{
				if(errno == EAGAIN || errno == EWOULDBLOCK)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				else
				{
					close(socket);
					socket = -1;
					return;
				}
			}
			else
				sent += ret;
		}
	}

	void ReadData(Timeout &timeout, void *buffer, unsigned int size)
	{
		if(socket < 0)
			return;

		unsigned int readC = 0;
		unsigned char *bufferC = reinterpret_cast<unsigned char*>(buffer);
		while(readC < size)
		{
			int ret = read(socket, bufferC + readC, size - readC);
			if(ret < 0)
			{
				if(errno == EAGAIN || errno == EWOULDBLOCK)
				{
					if(timeout.IsTimeoutReached())
					{
						timeout.SetCancel();
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				else
				{
					close(socket);
					socket = -1;
					return;
				}
			}
			else
				readC += ret;
		}

		//std::cout << "Read " << readC << " bytes from (" << socket << ")\n";
		//std::cout << "\t";
		//for(unsigned int i=0 ; i<size ; ++i)
		//{
		//	char c = bufferC[i];
		//	if(32 <= c && c < 128)
		//		std::cout.put(c);
		//	else
		//		std::cout << "(" << int(c) << ")";
		//}
		//std::cout << "\n";
	}

	bool HasData(void)
	{
		if(socket < 0)
			return false;

		pollfd request;
		std::memset(&request, 0, sizeof(pollfd));
		request.fd = socket;
		request.events = POLLIN;
		return 0 < poll(&request, 1, 0);
	}
};

class PosixListener : public ListenerBase
{
	int listener = -1;
public:
	bool Start(uint16_t port)
	{
		listener = socket(AF_INET, SOCK_STREAM, 0);
		if(listener < 0)
			return false;
		sockaddr_in sockAddr;
		std::memset(&sockAddr, 0, sizeof(sockAddr));
		sockAddr.sin_port = htons(port);
		sockAddr.sin_family = AF_INET;
		if(bind(listener, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr)) < 0)
		{
			close(listener);
			listener = -1;
			return false;
		}
		if(listen(listener, 64) != 0)
		{
			close(listener);
			listener = -1;
			return false;
		}
		return true;
	}

	void Stop(void)
	{
		if(0 <= listener)
		{
			shutdown(listener, SHUT_RDWR);
			close(listener);
			listener = -1;
		}
	}

	SocketBase *Accept(void)
	{
		int newSocket = accept(listener, nullptr, nullptr);
		if(newSocket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return nullptr;
		else if(newSocket < 0)
		{
			close(listener);
			listener = -1;
			return nullptr;
		}
		else
		{
			//std::cout << "New Connection accepted (" << newSocket << ")\n";
			int flags = fcntl(newSocket, F_GETFL);
			fcntl(newSocket, F_SETFL, flags | O_NONBLOCK);
			PosixSocket *sock = new PosixSocket;
			sock->socket = newSocket;
			return sock;
		}
	}

	void HandleDelete(SocketBase *socket)
	{
		delete socket;
	}

	bool IsGood(void)
	{
		return 0 <= listener;
	}
};

#endif
