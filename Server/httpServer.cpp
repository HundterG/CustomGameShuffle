// g++ httpServer.cpp -g -lssl -lcrypto -lcurses -o http.out
#include <map>

#include "NetworkStuff/HTTPRequestProccess.h"
#include "NetworkStuff/HTTPServer.h"
#include "NetworkStuff/RedirectServer.h"
#include "NetworkStuff/PosixSocket.h"
#include "NetworkStuff/OpenSSLSocket.h"

#include "serverCommon.inl"

struct LoadedFile
{
	char *data = nullptr;
	HSTL::WholeNumber size = 0;
	std::string mime;
	bool Cache = true;
	int OptionalCacheTime = 0;
};

std::map<HSTL::ID, LoadedFile> files;
HSTL::ID defaultFile = HSTL::defaultID;
ServerUI ui;
std::string certFile;
std::string keyFile;

class HTTPServerProccess : public HTTPRequestProccess
{
public:
	void DoRequest(SocketBase *socket)
	{
		auto fileIt = files.end();
		bool isDefault = false;
		ParseHelper(socket, 
			[&](char const *requestCode, char const *HTTPVersion, char const *uri)
			{
				if(std::strcmp(requestCode, "GET") != 0)
					return HelperFunctionReturn::FORBIDDEN;
				
				if(uri[0] == '/' || uri[0] == '\\')
					++uri;

				HSTL::ID fileID = HSTL::defaultID;
				if(uri[0] == 0)
				{
					fileID = defaultFile;
					isDefault = true;
				}
				else
					fileID = HSTL::StringHash(uri);

				char logString[1024 + 128] = {0};
				fileIt = files.find(fileID);
				if(fileIt != files.end())
				{
					snprintf(logString, sizeof(logString), "/%s (OK)", uri);
					ui.AddToLog(logString);
					return HelperFunctionReturn::OK;
				}
				else
				{
					snprintf(logString, sizeof(logString), "/%s (NOT FOUND)", uri);
					ui.AddToLog(logString);
					return HelperFunctionReturn::NOT_FOUND;
				}
			}, 
			[](char const *headerKey, char const *headerValue)
			{
				if(std::strcmp(headerKey, "content-length") == 0)
					return HelperFunctionReturn::FORBIDDEN;
				return HelperFunctionReturn::OK;
			}, 
			[](unsigned char const *buffer, unsigned int bufferSize, uint64_t totalSize){ return HelperFunctionReturn::FORBIDDEN; }, 
			[&](SocketBase *socket, char const *requestCode, char const *uri)
			{
				std::pair<char const*, char const*> headers[6];
				headers[0].first = "Content-Type"; headers[0].second = fileIt->second.mime.c_str(); 
				headers[1].first = "Cross-Origin-Opener-Policy"; headers[1].second = "same-origin"; 
				headers[2].first = "Cross-Origin-Embedder-Policy"; headers[2].second = "credentialless"; 
				headers[3].first = "X-Robots-Tag"; headers[3].second = "noindex, nofollow";
				int headersSize = 4;

				char cacheTimeString[256] = {0};
				if(fileIt->second.Cache == false)
				{
					headers[headersSize].first = "Cache-Control"; headers[headersSize].second = "no-store";
					++headersSize;
				}
				else if(0 <= fileIt->second.OptionalCacheTime)
				{
					sprintf(cacheTimeString, "max-age=%d", fileIt->second.OptionalCacheTime);
					headers[headersSize].first = "Cache-Control"; headers[headersSize].second = cacheTimeString;
					++headersSize;
				}

				if(isDefault)
				{
					headers[headersSize].first = "Content-Security-Policy"; headers[headersSize].second = "worker-src 'self' blob: 'wasm-unsafe-eval';";
					++headersSize;
				}

				WriteResponse(socket, "200 OK", headers, headersSize, fileIt->second.data, fileIt->second.size);
				return HelperFunctionReturn::OK;
			});
	}
};

class HTTPServerUIFunctions : public ServerUIFunctions
{
public:
	void GetRightStatsLine1(char line[128]) {}
	void GetRightStatsLine2(char line[128]) {}

	int GetMaxInputTemplates(void)
	{
		return 2;
	}
	void GetInputTemplate(int index, char line[128])
	{
		switch(index)
		{
		case 0:
			std::strcpy(line, "Quit");
			break;

		case 1:
			std::strcpy(line, "Tail");
			break;

		default:
			line[0] = 0;
		}
	}
	void ProcessInput(char line[128])
	{
		for(int i=0 ; i<128 ; ++i)
			line[i] = char(std::tolower(line[i]));
		if(std::strcmp(line, "quit") == 0)
			ui.StopRunning();
		else if(std::strcmp(line, "tail") == 0)
			ui.GoToBottomOfLog();
	}
} HTTPUIFunctions;

class HTTPServerSSLListener : public OpenSSLListener
{
public:
	char const *GetCertFileName(void)
	{
		return certFile.c_str();
	}
	
	char const *GetKeyFileName(void)
	{
		return keyFile.c_str();
	}
};

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

	certFile = std::move(argGetter.get<std::string>("q"));
	keyFile = std::move(argGetter.get<std::string>("k"));

	HTTPServer<HTTPServerProccess, HTTPServerSSLListener, 64> serverMain;
	if(serverMain.RunAsync(argGetter.get<int>("p")) == false)
	{
		std::cout << "Failed to listen main server\n";
		return 1;
	}

	RedirectServer<PosixListener> serverRedirect;
	if(0 < argGetter.get<int>("r") && !argGetter.get<std::string>("l").empty())
	{
		if(serverRedirect.RunAsync(argGetter.get<int>("r"), argGetter.get<std::string>("l").c_str()) == false)
		{
			std::cout << "Failed to listen redirect server\n";
			return 1;
		}
	}

	ui.Run(HTTPUIFunctions);
}
