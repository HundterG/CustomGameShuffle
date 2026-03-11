#ifndef HTTPREQUESTPROCCESS_H_
#define HTTPREQUESTPROCCESS_H_

#include "Socket.h"
#include <utility>
#include <type_traits>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <algorithm>
#include "Config.h"

//#include <iostream>

class HTTPRequestProccess
{
public:
	virtual ~HTTPRequestProccess() = default;

	virtual void DoRequest(SocketBase *socket) = 0;
	virtual void Reset(void) {}
	static bool IsASCII(unsigned char c)
	{
		return 32 <= c && c < 128;
	}

protected:
	enum HelperFunctionReturn
	{
		OK, // will continue processing, no response will be sent at the end. Call WriteResponse to send a response.
		BADREQUEST, // will stop processing and send 400 BAD REQUEST
		FORBIDDEN, // will stop processing and send 403 FORBIDDEN
		NOT_FOUND, // will stop processing and send 404 NOT FOUND
		BAIL, // will stop processing and close the connecting without sending a response
	};

private:
	template<typename RequestValidFn, int i> struct CallRequestFunction;
	template<typename RequestValidFn> struct CallRequestFunction<RequestValidFn, 0> { static inline HelperFunctionReturn Func(RequestValidFn requestValidFunction, char const *code, char const *version, char const *uri)
	{
		return requestValidFunction(code, version, uri);
	} };
	template<typename RequestValidFn>  struct CallRequestFunction<RequestValidFn, 1> { static inline HelperFunctionReturn Func(RequestValidFn, char const*, char const*, char const*)
	{
		return HelperFunctionReturn::OK;
	} };
	template<typename RequestValidFn>  struct CallRequestFunction<RequestValidFn, 2> { static inline HelperFunctionReturn Func(RequestValidFn requestValidFunction, char const *code, char const *version, char const *uri)
	{
		if(requestValidFunction)
			return requestValidFunction(code, version, uri);
		else
			return HelperFunctionReturn::OK;
	} };
	template<typename RequestValidFn>  struct CallRequestFunction<RequestValidFn, 3> { static inline HelperFunctionReturn Func(RequestValidFn, char const*, char const*, char const*)
	{
		return HelperFunctionReturn::OK;
	} };

	template<typename ParseHeaderFn, int i> struct CallHeaderFunction;
	template<typename ParseHeaderFn> struct CallHeaderFunction<ParseHeaderFn, 0> { static inline HelperFunctionReturn Func(ParseHeaderFn parseHeaderFunction, char const *name, char const *value)
	{
		return parseHeaderFunction(name, value);
	} };
	template<typename ParseHeaderFn> struct CallHeaderFunction<ParseHeaderFn, 1> { static inline HelperFunctionReturn Func(ParseHeaderFn, char const*, char const*)
	{
		return HelperFunctionReturn::OK;
	} };
	template<typename ParseHeaderFn> struct CallHeaderFunction<ParseHeaderFn, 2> { static inline HelperFunctionReturn Func(ParseHeaderFn parseHeaderFunction, char const *name, char const *value)
	{
		if(parseHeaderFunction)
			return parseHeaderFunction(name, value);
		else
			return HelperFunctionReturn::OK;
	} };
	template<typename ParseHeaderFn> struct CallHeaderFunction<ParseHeaderFn, 3> { static inline HelperFunctionReturn Func(ParseHeaderFn, char const*, char const*)
	{
		return HelperFunctionReturn::OK;
	} };

	template<typename ProccessContentFn, int i> struct CallContentFunction;
	template<typename ProccessContentFn> struct CallContentFunction<ProccessContentFn, 0> { static inline HelperFunctionReturn Func(ProccessContentFn proccessContentFunction, unsigned char const *buffer, unsigned int bufferSize, uint64_t totalSize)
	{
		return proccessContentFunction(buffer, bufferSize, totalSize);
	} };
	template<typename ProccessContentFn> struct CallContentFunction<ProccessContentFn, 1> { static inline HelperFunctionReturn Func(ProccessContentFn, unsigned char const*, unsigned int, uint64_t)
	{
		return HelperFunctionReturn::OK;
	} };
	template<typename ProccessContentFn> struct CallContentFunction<ProccessContentFn, 2> { static inline HelperFunctionReturn Func(ProccessContentFn proccessContentFunction, unsigned char const *buffer, unsigned int bufferSize, uint64_t totalSize)
	{
		if(proccessContentFunction)
			return proccessContentFunction(buffer, bufferSize, totalSize);
		else
			return HelperFunctionReturn::OK;
	} };
	template<typename ProccessContentFn> struct CallContentFunction<ProccessContentFn, 3> { static inline HelperFunctionReturn Func(ProccessContentFn, unsigned char const*, unsigned int, uint64_t)
	{
		return HelperFunctionReturn::OK;
	} };

	template<typename ProccessRequestFn, int i> struct CallProccessFunction;
	template<typename ProccessRequestFn> struct CallProccessFunction<ProccessRequestFn, 0> { static inline HelperFunctionReturn Func(ProccessRequestFn proccessRequestFunction, SocketBase *socket, char const *code, char const *uri)
	{
		return proccessRequestFunction(socket, code, uri);
	} };
	template<typename ProccessRequestFn> struct CallProccessFunction<ProccessRequestFn, 1> { static inline HelperFunctionReturn Func(ProccessRequestFn, SocketBase *socket, char const*, char const*)
	{
		HTTPRequestProccess::WriteResponse(socket, "200 OK", nullptr, 0, nullptr, 0);
		return HelperFunctionReturn::OK;
	} };
	template<typename ProccessRequestFn> struct CallProccessFunction<ProccessRequestFn, 2> { static inline HelperFunctionReturn Func(ProccessRequestFn proccessRequestFunction, SocketBase *socket, char const *code, char const *uri)
	{
		if(proccessRequestFunction)
			proccessRequestFunction(socket, code, uri);
		else
		{
			HTTPRequestProccess::WriteResponse(socket, "200 OK", nullptr, 0, nullptr, 0);
			return HelperFunctionReturn::OK;
		}
	} };
	template<typename ProccessRequestFn> struct CallProccessFunction<ProccessRequestFn, 3> { static inline HelperFunctionReturn Func(ProccessRequestFn, SocketBase *socket, char const*, char const*)
	{
		HTTPRequestProccess::WriteResponse(socket, "200 OK", nullptr, 0, nullptr, 0);
		return HelperFunctionReturn::OK;
	} };

protected:

#define RETURNBADREQUEST(__msg) { WriteResponse(socket, "400 BAD REQUEST", nullptr, 0, nullptr, 0); /*std::cout << __msg << "\n";*/ socket->Close(); return; }
#define CHECKRESULT() { \
	switch(result) { \
	case HelperFunctionReturn::OK: break; \
	case HelperFunctionReturn::BADREQUEST: WriteResponse(socket, "400 BAD REQUEST", nullptr, 0, nullptr, 0); socket->Close(); return; \
	case HelperFunctionReturn::FORBIDDEN: WriteResponse(socket, "403 FORBIDDEN", nullptr, 0, nullptr, 0); socket->Close(); return; \
	case HelperFunctionReturn::NOT_FOUND: WriteResponse(socket, "404 NOT FOUND", nullptr, 0, nullptr, 0); socket->Close(); return; \
	case HelperFunctionReturn::BAIL: socket->Close(); return; \
	} }
#define CHECKTIMEOUT() { if(timeout.GetCancel()) { socket->Close(); return; } }

	template<typename RequestValidFn, typename ParseHeaderFn, typename ProccessContentFn, typename ProccessRequestFn>
	inline void ParseHelper(SocketBase *socket, RequestValidFn requestValidFunction, ParseHeaderFn parseHeaderFunction, ProccessContentFn proccessContentFunction, ProccessRequestFn proccessRequestFunction)
	{
		//static_assert(std::is_null_pointer<RequestValidFn>::value ||
		//	std::is_same< std::invoke_result<RequestValidFn, char const*, char const*, char const*>::type, HelperFunctionReturn>::value
		//	, "Invalid requestValidFunction, can be null, HelperFunctionReturn Fn(char const *requestCode, char const *HTTPVersion, char const *uri), or a pointer to");
		//static_assert(std::is_null_pointer<ParseHeaderFn>::value ||
		//	std::is_same< std::invoke_result<ParseHeaderFn, char const*, char const*>::type, HelperFunctionReturn>::value
		//	, "Invalid ParseHeaderFn, can be null, HelperFunctionReturn Fn(char const *headerKey, char const *headerValue), or a pointer to");
		//static_assert(std::is_null_pointer<ProccessContentFn>::value ||
		//	std::is_same< std::invoke_result<ProccessContentFn, unsigned char const*, unsigned int, uint64_t>::type, HelperFunctionReturn>::value
		//	, "Invalid ProccessContentFn, can be null, HelperFunctionReturn Fn(unsigned char const *buffer, unsigned int bufferSize, uint64_t totalSize), or a pointer to");
		//static_assert(std::is_null_pointer<ProccessRequestFn>::value ||
		//	std::is_same< std::invoke_result<ProccessRequestFn, SocketBase*, char const*, char const*>::type, HelperFunctionReturn>::value
		//	, "Invalid ProccessRequestFn, can be null, HelperFunctionReturn Fn(SocketBase *socket, char const *requestCode, char const *uri), or a pointer to");

		Timeout timeout(NS_TIMEOUT);

		// read the first line
		char code[32] = {0};
		char httpVersion[16] = {0};
		char uri[1024] = {0};

		bool sizeGood = false;
		for(int i=0 ; i<30 ; ++i)
		{
			unsigned char c = socket->ReadByte(timeout);
			CHECKTIMEOUT();
			if(c == ' ')
			{
				sizeGood = true;
				break;
			}
			if(!IsASCII(c))
				RETURNBADREQUEST("Invalid ASCII in code");
			code[i] = char(c);
		}
		if(!sizeGood)
			RETURNBADREQUEST("Invalid size in code");

		sizeGood = false;
		for(int i=0 ; i<1020 ; ++i)
		{
			unsigned char c = socket->ReadByte(timeout);
			CHECKTIMEOUT();
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

		sizeGood = false;
		for(int i=0 ; i<15 ; ++i)
		{
			unsigned char c = socket->ReadByte(timeout);
			CHECKTIMEOUT();
			if(c == 0x0D)
			{
				sizeGood = true;
				break;
			}
			if(!IsASCII(c))
				RETURNBADREQUEST("Invalid ASCII in version");
			httpVersion[i] = char(std::toupper(c));
		}
		if(!sizeGood)
			RETURNBADREQUEST("Invalid size in version");
		if(socket->ReadByte(timeout) != 0x0A)
			RETURNBADREQUEST("Malformed NL in FL");
		CHECKTIMEOUT();

		if(!(httpVersion[0] == 'H' && httpVersion[1] == 'T' && httpVersion[2] == 'T' && httpVersion[3] == 'P' && httpVersion[4] == '/'))
			RETURNBADREQUEST("Malformed HTTP version");

		// first line check
		HelperFunctionReturn result = CallRequestFunction<RequestValidFn, (std::is_null_pointer<RequestValidFn>::value * 1) +
			((std::is_pointer<RequestValidFn>::value || std::is_member_pointer<RequestValidFn>::value) * 2) >::Func(std::forward<RequestValidFn>(requestValidFunction), code, httpVersion, uri);
		CHECKRESULT();

		// parse headers
		bool headerCountGood = false;
		uint64_t contentSize = 0;
		for(int headerCount=0 ; headerCount<50 ; ++headerCount)
		{
			char line[1024] = {0};
			int lineSize = 0;
			int colonPos = -1;
			bool lineSizeGood = false;
			for(int i=0 ; i<1020 ; ++i)
			{
				unsigned char c = socket->ReadByte(timeout);
				CHECKTIMEOUT();
				if(c == 0x0D)
				{
					line[lineSize] = 0;
					lineSizeGood = true;
					break;
				}
				if(c == ':')
				{
					if(colonPos == -1)
						colonPos = i;
				}
				if(!IsASCII(c))
					RETURNBADREQUEST("Invalid ASCII in header");
				if(colonPos == -1)
					line[lineSize] = char(std::tolower(c));
				else
					line[lineSize] = char(c);
				++lineSize;
			}
			if(!lineSizeGood)
				RETURNBADREQUEST("Invalid size in header");
			if(socket->ReadByte(timeout) != 0x0A)
				RETURNBADREQUEST("Malformed NL in Header");
			CHECKTIMEOUT();
			
			if(lineSize == 0)
			{
				headerCountGood = true;
				break;
			}
			
			if(colonPos == -1)
				RETURNBADREQUEST("No colin in header");
			line[colonPos] = 0;

			int valuePos = colonPos + 1;
			while(valuePos < lineSize && (line[valuePos] == ' ' || line[valuePos] == '\t'))
				++valuePos;
			if(lineSize <= valuePos)
				RETURNBADREQUEST("No value in header");

			if(std::strcmp("content-length", line) == 0)
			{
				contentSize = strtoull(line + valuePos, nullptr, 10);
				if(NS_MAX_UPLOAD_SIZE < contentSize)
					RETURNBADREQUEST("content size is more that williing to proccess");
			}

			result = CallHeaderFunction<ParseHeaderFn, (std::is_null_pointer<ParseHeaderFn>::value * 1) +
				((std::is_pointer<ParseHeaderFn>::value || std::is_member_pointer<ParseHeaderFn>::value) * 2) >::Func(std::forward<ParseHeaderFn>(parseHeaderFunction), line, line + valuePos);
			CHECKRESULT();
		}
		if(!headerCountGood)
			RETURNBADREQUEST("too many headers");

		// get content if available
		if(contentSize != 0)
		{
			uint64_t readLength = 0;
			unsigned char buffer[64 * 1024] = {0};
			while(readLength < contentSize)
			{
				unsigned int readThisLoop = (unsigned int)(std::min(uint64_t(64*1024), contentSize-readLength));
				socket->ReadData(timeout, buffer, readThisLoop);
				CHECKTIMEOUT();
				result = CallContentFunction<ProccessContentFn, (std::is_null_pointer<ProccessContentFn>::value * 1) +
					((std::is_pointer<ProccessContentFn>::value || std::is_member_pointer<ProccessContentFn>::value) * 2) >::Func(std::forward<ProccessContentFn>(proccessContentFunction), buffer, readThisLoop, contentSize);
				CHECKRESULT();
				readLength += readThisLoop;
			}
		}

		// call proccessRequestFunction
		result = CallProccessFunction<ProccessRequestFn, (std::is_null_pointer<ProccessRequestFn>::value * 1) +
			((std::is_pointer<ProccessRequestFn>::value || std::is_member_pointer<ProccessRequestFn>::value) * 2) >::Func(std::forward<ProccessRequestFn>(proccessRequestFunction), socket, code, uri);
		CHECKRESULT();
	}

#undef CHECKTIMEOUT
#undef CHECKRESULT
#undef RETURNBADREQUEST

	static void WriteResponse(SocketBase *socket, char const *code, std::pair<char const*, char const*> headers[], unsigned int headersSize, void const *data, unsigned int dataSize)
	{
		// write code
		socket->WriteString("HTTP/1.1 ");
		socket->WriteString(code);
		socket->WriteCRLF();

		// write headers
		if(dataSize == 0)
		{
			socket->WriteString("Content-Length: 0");
			socket->WriteCRLF();
		}
		else
		{
			char number[16] = {0};
			sprintf(number, "%d", dataSize);
			socket->WriteString("Content-Length: ");
			socket->WriteString(number);
			socket->WriteCRLF();
		}

		for(unsigned int i=0 ; i<headersSize ; ++i)
		{
			std::pair<char const*, char const*> &header = headers[i];
			socket->WriteString(header.first);
			socket->WriteString(": ");
			socket->WriteString(header.second);
			socket->WriteCRLF();
		}
		socket->WriteCRLF();

		// write data
		if(dataSize != 0)
			socket->WriteData(data, dataSize);
	}
};

#endif
