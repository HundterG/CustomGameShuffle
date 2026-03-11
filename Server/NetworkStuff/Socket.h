#ifndef SOCKET_H_
#define SOCKET_H_

#include "Timeout.h"

class SocketBase
{
public:
	virtual ~SocketBase() = default;
	virtual bool Prepare(void) = 0;

	virtual bool IsOpen(void) = 0;
	virtual void Close(void) = 0;

	void WriteString(char const *string)
	{
		unsigned int size = 0;
		char const *walk = string;
		while(*walk != 0)
		{
			++size;
			++walk;
		}
		WriteData(string, size);
	}
	void WriteCRLF(void)
	{
		unsigned char CRLF[2] = {0x0D, 0x0A};
		WriteData(&CRLF, 2);
	}
	virtual void WriteData(void const *data, unsigned int size) = 0;
	
	unsigned char ReadByte(Timeout &timeout)
	{
		unsigned char c = 0;
		ReadData(timeout, &c, 1);
		return c;
	}
	virtual void ReadData(Timeout &timeout, void *buffer, unsigned int size) = 0;

	//virtual unsigned int ReadSomeData(Timeout &timeout, void *buffer, unsigned int size) = 0;

	virtual bool HasData(void) = 0;
};

class ListenerBase
{
public:
	virtual ~ListenerBase() = default;
	virtual bool Start(uint16_t port) = 0;
	virtual void Stop(void) = 0;
	virtual SocketBase *Accept(void) = 0;
	virtual void HandleDelete(SocketBase *socket) = 0;
	virtual bool IsGood(void) = 0;
};

#endif
