#include <fstream>
#include <map>
#include <cstring>
#include <string>

namespace HSTL
{
	typedef uint64_t ID;
	static_assert(sizeof(ID) == 8, "HSTL ID is not 8 bytes");
	typedef int64_t WholeNumber;
	static_assert(sizeof(WholeNumber) == 8, "HSTL WholeNumber is not 8 bytes");
	ID const defaultID = 37;

	namespace Internal
	{
		struct Hasher
		{
			ID value = 37;

			template <typename T>
			void Hash(T *buffer, WholeNumber length)
			{
				while (0 < length)
				{
					value = (value * 54059) ^ (*buffer * 76963);
					++buffer;
					--length;
				}
			}

			template <typename T>
			void HashToNull(T *buffer)
			{
				while (*buffer != 0)
				{
					value = (value * 54059) ^ (*buffer * 76963);
					++buffer;
				}
			}

			template <typename T>
			void Add(T v)
			{
				value = (value * 54059) ^ (v * 76963);
			}
		};
	}

	inline ID StringHash(char const *string)
	{
		Internal::Hasher worker;
		worker.HashToNull(string);
		return worker.value;
	}

	inline ID Hash(char const *buffer, WholeNumber length)
	{
		Internal::Hasher worker;
		worker.Hash(buffer, length);
		return worker.value;
	}
}

struct LoadedFile
{
	char *data = nullptr;
	HSTL::WholeNumber size = 0;
	std::string mime;
	bool Cache = true;
	int OptionalCacheTime = 0;
};

#define IXWEBSOCKET_USE_TLS
#define IXWEBSOCKET_USE_OPEN_SSL
#define HTTP_PRINT_STUFF
#include "ixwebsocket/IXCancellationRequest.cpp"
#include "ixwebsocket/IXConnectionState.cpp"
#include "ixwebsocket/IXDNSLookup.cpp"
#include "ixwebsocket/IXExponentialBackoff.cpp"
#include "ixwebsocket/IXHttp.cpp"
#include "ixwebsocket/IXHttpServer.cpp"
#include "ixwebsocket/IXNetSystem.cpp"
#include "ixwebsocket/IXSelectInterrupt.cpp"
#include "ixwebsocket/IXSelectInterruptEvent.cpp"
#include "ixwebsocket/IXSelectInterruptFactory.cpp"
#include "ixwebsocket/IXSelectInterruptPipe.cpp"
#include "ixwebsocket/IXSetThreadName.cpp"
#include "ixwebsocket/IXSocket.cpp"
#include "ixwebsocket/IXSocketConnect.cpp"
#include "ixwebsocket/IXSocketFactory.cpp"
#include "ixwebsocket/IXSocketOpenSSL.cpp"
#include "ixwebsocket/IXSocketServer.cpp"
#include "ixwebsocket/IXSocketTLSOptions.cpp"
#include "ixwebsocket/IXStrCaseCompare.cpp"
#include "ixwebsocket/IXUrlParser.cpp"
#include "ixwebsocket/IXUserAgent.cpp"
#include "ixwebsocket/IXUuid.cpp"
#include "ixwebsocket/IXWebSocketHttpHeaders.cpp"

#include "xml/pugixml.cpp"

#include "cli/cmdparser.hpp"

std::map<HSTL::ID, LoadedFile> files;
HSTL::ID defaultFile = HSTL::defaultID;

ix::HttpResponsePtr GetFile(ix::HttpRequestPtr request, std::shared_ptr<ix::ConnectionState> state)
{
	std::cout << request->method << " : " << request->uri << "\n";
	if(request->method != "GET")
		return std::make_shared<ix::HttpResponse>(403, "FORBIDDEN", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "403", 4);

	if(!files.empty())
	{
		char const *url = request->uri.c_str();
		HSTL::WholeNumber size = request->uri.size();
		if(url[0] == '/' || url[0] == '\\')
		{
			++url;
			--size;
		}

		HSTL::ID id = HSTL::defaultID;
		if(size <= 0)
			id = defaultFile;
		else
			id = HSTL::Hash(url, size);

		auto file = files.find(id);
		if(file != files.end())
		{
			ix::WebSocketHttpHeaders headers;
			headers.emplace("Content-Type", file->second.mime);
			headers.emplace("Cross-Origin-Opener-Policy", "same-origin");
			headers.emplace("Cross-Origin-Embedder-Policy", "credentialless");
			if(file->second.Cache)
			{
				if(0 <= file->second.OptionalCacheTime)
					headers.emplace("Cache-Control", "max-age=" + std::to_string(file->second.OptionalCacheTime));
			}
			else
				headers.emplace("Cache-Control", "no-store");
			if(id == defaultFile)
				headers.emplace("Content-Security-Policy", "worker-src 'self' blob: 'wasm-unsafe-eval';");
			return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, headers, file->second.data, file->second.size);
		}
		else
			return std::make_shared<ix::HttpResponse>(404, "NOT FOUND", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "404", 4);
	}
	else
		return std::make_shared<ix::HttpResponse>(404, "NOT FOUND", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "404", 4);
}

int main(int argc, char *args[])
{
	cli::Parser argGetter(argc, args);
	argGetter.set_required<std::string>("f", "fileList", "The xml file list for the server");
	argGetter.set_required<int>("p", "port", "Port to open the https server on");
	argGetter.set_optional<int>("r", "redirectPort", 0, "Port to listen on for http redirect (Must be paired with redirectLocation)");
	argGetter.set_optional<std::string>("l", "redirectLocation", std::string(), "Location to http redirect to (Must be paired with redirectPort)");
	argGetter.set_optional<std::string>("k", "certKey", std::string(), "SSL key file");
	argGetter.set_optional<std::string>("q", "certFile", std::string(), "SSL cert file");
	argGetter.run_and_exit_if_error();

	{
		std::string inFilename = std::move(argGetter.get<std::string>("f"));
		if(!inFilename.empty())
		{
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_file(inFilename.c_str());
			if(!result)
			{
				std::cout << "Failed to read xml\n";
				return 1;
			}

			auto listNode = doc.child("list");
			if(!listNode.empty())
			{
				for(auto entryNode : listNode.children("file"))
				{
					auto pathAttribute = entryNode.attribute("path");
					auto uriAttribute = entryNode.attribute("uri");
					auto mimeAttribute = entryNode.attribute("mime");
					auto cacheAttribute = entryNode.attribute("cache");
					auto defaultAttribute = entryNode.attribute("default");

					if(pathAttribute.empty() || mimeAttribute.empty())
					{
						std::cout << "Failed to load files : Missing required attributes\n";
						return 1;
					}

					HSTL::ID id = HSTL::defaultID;
					if(pugi::char_t const * uri = uriAttribute.as_string(nullptr))
						id = HSTL::StringHash(uri);
					else if(pugi::char_t const * uri = pathAttribute.as_string(nullptr))
						id = HSTL::StringHash(uri);
					else
					{
						std::cout << "Failed to load files : No ID\n";
						return 1;
					}

					if(files.find(id) != files.end())
					{
						std::cout << "Failed to load files : ID collision\n";
						return 1;
					}

					files.emplace(id, LoadedFile());
					auto fileEntry = files.find(id);
					fileEntry->second.mime = mimeAttribute.as_string();
					if(defaultAttribute.as_bool(false))
						defaultFile = id;

					if(0 < cacheAttribute.as_int(-1))
					{
						fileEntry->second.Cache = true;
						fileEntry->second.OptionalCacheTime = cacheAttribute.as_int();
					}
					else
						fileEntry->second.Cache = cacheAttribute.as_bool(true);

					std::ifstream file(pathAttribute.as_string(), std::ifstream::binary | std::ifstream::ate);
					if(!file.is_open())
					{
						std::cout << "Failed to load files : Could not open path " << pathAttribute.as_string() << "\n";
						return 1;
					}

					HSTL::WholeNumber size = file.tellg();
					file.seekg(0);
					fileEntry->second.data = new char[size];
					HSTL::WholeNumber readLen = 0;
					while(readLen < size)
					{
						file.read(reinterpret_cast<char*>(fileEntry->second.data + readLen), size - readLen);
						readLen += file.gcount();
						if(file.bad())
						{
							std::cout << "Failed to load files : Failed reding file\n";
							return 1;
						}
					}
					fileEntry->second.size = readLen;
				}
			}
			else
			{
				std::cout << "Failed to read xml\n";
				return 1;
			}
		}
	}

	ix::initNetSystem();

	ix::HttpServer serverMain(argGetter.get<int>("p"), "0.0.0.0", 20, 128);
	serverMain.setOnConnectionCallback(GetFile);
	ix::SocketTLSOptions secure;
	secure.certFile = argGetter.get<std::string>("q");
	secure.keyFile = argGetter.get<std::string>("k");
	secure.tls = true;
	secure.caFile = "NONE";
	serverMain.setTLSOptions(secure);
	if(serverMain.listen().first == false)
	{
		std::cout << "Failed to listen main server\n";
		return 1;
	}
	
	serverMain.start();

	if(0 < argGetter.get<int>("r") && !argGetter.get<std::string>("l").empty())
	{
		ix::HttpServer serverRedirect(argGetter.get<int>("r"), "0.0.0.0");
		serverRedirect.makeRedirectServer(argGetter.get<std::string>("l"));
		if(serverRedirect.listen().first == false)
		{
			std::cout << "Failed to listen redirect server\n";
			return 1;
		}
		serverRedirect.start();
		for(;;)
			sleep(60);
	}
	else
		for(;;)
			sleep(60);
}
