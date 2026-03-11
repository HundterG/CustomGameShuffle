#ifndef OPENSSLSOCKET_H_
#define OPENSSLSOCKET_H_

#include "Socket.h"
#include "Config.h"
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>
#include <mutex>
#include <cstring>
#include <thread>
#include <chrono>

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

// #include <iostream>

class OpenSSLListener;

class OpenSSLSocket : public SocketBase
{
	friend OpenSSLListener;
	std::mutex lock;
	SSL *sslConnection = nullptr;
	SSL_CTX *sslContext = nullptr;
	int socket = -1;

	void InternalClose(void)
	{
		if(sslConnection != nullptr)
		{
			SSL_free(sslConnection);
			sslConnection = nullptr;
		}
		if(sslContext != nullptr)
		{
			SSL_CTX_free(sslContext);
			sslContext = nullptr;
		}
		if(0 <= socket)
		{
			close(socket);
			socket = -1;
		}
	}

public:
	~OpenSSLSocket()
	{
		std::lock_guard<std::mutex> g(lock);
		InternalClose();
	}

	bool Prepare(void)
	{
		std::lock_guard<std::mutex> g(lock);
		if(socket < 0 || !sslConnection || !sslContext)
			return false;

		Timeout timeout(NS_TIMEOUT);
		while(!timeout.IsTimeoutReached())
		{
			ERR_clear_error();
			int result = SSL_accept(sslConnection);
			if(result == 1)
				return true;

			int error = SSL_get_error(sslConnection, result);
			//std::cout << ERR_error_string(error, nullptr) << "\n";
			if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			else
				return false;
		}
		return false;
	}

	bool IsOpen(void)
	{
		return 0 <= socket && sslConnection != nullptr && sslContext != nullptr;
	}

	void Close(void)
	{
		std::lock_guard<std::mutex> g(lock);
		InternalClose();
	}

	void WriteData(void const *data, unsigned int size)
	{
		std::lock_guard<std::mutex> g(lock);
		if(socket < 0 || !sslConnection || !sslContext)
			return;

		unsigned int sent = 0;
		unsigned char const *dataC = reinterpret_cast<unsigned char const*>(data);
		while(sent < size)
		{
			ERR_clear_error();
			int ret = SSL_write(sslConnection, dataC + sent, size - sent);
			int error = SSL_get_error(sslConnection, ret);
			if(error == SSL_ERROR_NONE)
				sent += ret;
			else if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			else
			{
				InternalClose();
				return;
			}
		}
	}

	void ReadData(Timeout &timeout, void *buffer, unsigned int size)
	{
		std::lock_guard<std::mutex> g(lock);
		if(socket < 0 || !sslConnection || !sslContext)
			return;

		unsigned int readC = 0;
		unsigned char *bufferC = reinterpret_cast<unsigned char*>(buffer);
		while(readC < size)
		{
			ERR_clear_error();
			int ret = SSL_read(sslConnection, bufferC + readC, size - readC);
			int error = SSL_get_error(sslConnection, ret);
			if(error == SSL_ERROR_NONE)
				readC += ret;
			else if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
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
				InternalClose();
				return;
			}
		}
	}

	bool HasData(void)
	{
		std::lock_guard<std::mutex> g(lock);
		if(socket < 0 || !sslConnection || !sslContext)
			return false;

		return 0 < SSL_pending(sslConnection);
	}
};

class OpenSSLListener : public ListenerBase
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

#define CLEANUP() { if(sslConnection != nullptr) SSL_free(sslConnection); if(sslContext != nullptr) SSL_CTX_free(sslContext); close(newSocket); }

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
			SSL *sslConnection = nullptr;
			SSL_CTX *sslContext = nullptr;
			
			{
				SSL_METHOD const *method = SSLv23_server_method();
				if(method == nullptr)
				{
					CLEANUP();
					return nullptr;
				}
				else
				{
					sslContext = SSL_CTX_new(method);
					if(sslContext == nullptr)
					{
						CLEANUP();
						return nullptr;
					}
					else
					{
						SSL_CTX_set_mode(sslContext, SSL_MODE_ENABLE_PARTIAL_WRITE);
                        SSL_CTX_set_mode(sslContext, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
                        SSL_CTX_set_options(sslContext, SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_IGNORE_UNEXPECTED_EOF);
					}
				}
			}

			ERR_clear_error();
			if(SSL_CTX_use_certificate_chain_file(sslContext, GetCertFileName()) != 1)
			{
				//auto sslErr = ERR_get_error();
				//std::cout << ERR_error_string(sslErr, nullptr) << "\n";
				CLEANUP();
				return nullptr;
			}
			if(SSL_CTX_use_PrivateKey_file(sslContext, GetKeyFileName(), SSL_FILETYPE_PEM) != 1)
			{
				//auto sslErr = ERR_get_error();
				//std::cout << ERR_error_string(sslErr, nullptr) << "\n";
				CLEANUP();
				return nullptr;
			}

			ERR_clear_error();
			SSL_CTX_set_verify(sslContext, SSL_VERIFY_NONE, nullptr);
			if(SSL_CTX_set_cipher_list(sslContext, "ECDHE-ECDSA-AES128-GCM-SHA256 ECDHE-ECDSA-AES256-GCM-SHA384 ECDHE-ECDSA-AES128-SHA "
				"ECDHE-ECDSA-AES256-SHA ECDHE-ECDSA-AES128-SHA256 ECDHE-ECDSA-AES256-SHA384 "
				"ECDHE-RSA-AES128-GCM-SHA256 ECDHE-RSA-AES256-GCM-SHA384 ECDHE-RSA-AES128-SHA "
				"ECDHE-RSA-AES256-SHA ECDHE-RSA-AES128-SHA256 ECDHE-RSA-AES256-SHA384 "
				"DHE-RSA-AES128-GCM-SHA256 DHE-RSA-AES256-GCM-SHA384 DHE-RSA-AES128-SHA "
				"DHE-RSA-AES256-SHA DHE-RSA-AES128-SHA256 DHE-RSA-AES256-SHA256 AES128-SHA") != 1)
			{
				CLEANUP();
				return nullptr;
			}

			sslConnection = SSL_new(sslContext);
			if(sslConnection == nullptr)
			{
				CLEANUP();
				return nullptr;
			}
			SSL_set_fd(sslConnection, newSocket);

			OpenSSLSocket *sock = new OpenSSLSocket;
			sock->sslConnection = sslConnection;
			sock->sslContext = sslContext;
			sock->socket = newSocket;

			return sock;
		}
	}

#undef CLEANUP

	void HandleDelete(SocketBase *socket)
	{
		delete socket;
	}

	bool IsGood(void)
	{
		return 0 <= listener;
	}

	// To be overriden by user code to set the cert and key files
	virtual char const *GetCertFileName(void) = 0;
	virtual char const *GetKeyFileName(void) = 0;
};

#endif
