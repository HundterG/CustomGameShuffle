// g++ server.cpp -g -lssl -lcrypto -lcurses -o server.out
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <map>

// Use the old one till the new one is ready
#define USE_NS_WEBSOCKETSERVER 0

#if USE_NS_WEBSOCKETSERVER == 1
#include "NetworkStuff/OpenSSLSocket.h"
#include "NetworkStuff/WebSocketServer.h"
#else
#define IXWEBSOCKET_USE_TLS
#define IXWEBSOCKET_USE_OPEN_SSL
#include "ixwebsocket/IXCancellationRequest.cpp"
#include "ixwebsocket/IXConnectionState.cpp"
#include "ixwebsocket/IXDNSLookup.cpp"
#include "ixwebsocket/IXExponentialBackoff.cpp"
#include "ixwebsocket/IXHttp.cpp"
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
#include "ixwebsocket/IXWebSocket.cpp"
#include "ixwebsocket/IXWebSocketCloseConstants.cpp"
#include "ixwebsocket/IXWebSocketHandshake.cpp"
#include "ixwebsocket/IXWebSocketHttpHeaders.cpp"
#include "ixwebsocket/IXWebSocketPerMessageDeflate.cpp"
#include "ixwebsocket/IXWebSocketPerMessageDeflateCodec.cpp"
#include "ixwebsocket/IXWebSocketPerMessageDeflateOptions.cpp"
#include "ixwebsocket/IXWebSocketServer.cpp"
#include "ixwebsocket/IXWebSocketTransport.cpp"
#endif

#include "serverCommon.inl"

// https://stackoverflow.com/questions/59738140/why-is-firefox-not-trusting-my-self-signed-certificate

enum RunState
{
	PreGame = 0,
	InGame = 1,
	PostGame = 2
};

struct ShuffleEntry
{
	ShuffleEntry(int i, int t) : index(i), timeLength(t) {}
	int index;
	int timeLength;
};

struct OpenConnection
{
	OpenConnection(HSTL::ID userAgentID) : LeaderboardID(userAgentID) {}
	HSTL::ID LeaderboardID;
	std::atomic_bool isInUse = ATOMIC_VAR_INIT(false);

	std::string GetName(void)
	{
		char const *hexCharacters = "0123456789ABCDEF";
		uint32_t pointer = uint32_t(uintptr_t(this));
		char pointerAsString[16] = {0};
		pointerAsString[0] = '0';
		pointerAsString[1] = 'x';
		for(int i=0 ; i<8 ; ++i)
		{
			pointerAsString[9-i] = hexCharacters[(pointer & 0x0F)];
			pointer >>= 4;
		}
		return std::string(pointerAsString);
	}
};

std::atomic<RunState> runState = ATOMIC_VAR_INIT(RunState::PreGame);
std::atomic_int64_t startTime = ATOMIC_VAR_INIT(-1);
std::atomic_int64_t currentShuffleStartTime = ATOMIC_VAR_INIT(-1);

std::vector<ShuffleEntry> shuffles;
int currentShuffle = -1;

int totalLength = 0;

ServerUI ui;
std::string certFile;
std::string keyFile;

std::mutex openConnectionsLock;
std::map<void*, OpenConnection> openConnections;

std::mutex connectionsPendingDeleteLock;
std::vector<void*> connectionsPendingDelete;

#if USE_NS_WEBSOCKETSERVER == 1
void ParseXMLMessage(OpenConnection &connection, char const *msg, int size)
{
	try
	{
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_buffer(msg, size);
		if(!result)
			return;
			
		// TODO: Commands from the client get parsed here
	}
	catch(...)
	{
	}
}

class CGSServerWebSocketFunctions : public WebSocketFunctions
{
public:
	void OnNewConnection(WebSocketServerBase *server, SocketBase *socket, char const *userAgent)
	{
		switch(runState)
		{
		case RunState::PreGame:
			socket->WriteString(("<pre len=\"" + std::to_string(totalLength) + "\" />").c_str());
			break;
		case RunState::InGame:
		{
			int64_t allProgress = std::time(nullptr) - startTime;
			int64_t remainingTime = totalLength - allProgress;
			int index = (currentShuffle < 0) ? 0 : currentShuffle;
			socket->WriteString(("<swi game=\"" + std::to_string(shuffles[index].index) + "\" time=\"" + std::to_string(remainingTime) + "\" />").c_str());
		}
			break;
		case RunState::PostGame:
			socket->WriteString("<end />");
			break;
		}

		std::string name;
		{
			std::lock_guard<std::mutex> lock(openConnectionsLock);
			auto newConnection = openConnections.emplace(socket, HSTL::StringHash(userAgent));
			name = std::move(newConnection.first->second.GetName());
		}

		ui.AddToLog(("Accepting new Connection (" + name + ")").c_str());
	}
	void OnDataReceived(WebSocketServerBase *server, SocketBase *socket)
	{
		OpenConnection *connection = nullptr;
		{
			std::lock_guard<std::mutex> lock(openConnectionsLock);
			auto f = openConnections.find(socket);
			connection = &f->second;
			connection->isInUse = true;
		}

		if(connection)
		{
			unsigned int bufferSize = 0;
			do
			{
				unsigned char getBuffer[512] = {0};
				bufferSize = socket->ReadSomeData(getBuffer, 512);
				std::memcpy(connection->tempXMLBuffer + connection->tempXMLBufferSize, getBuffer, bufferSize);
				connection->tempXMLBufferSize += int(bufferSize);

				if(0 < connection->tempXMLBufferSize)
				{
					int openPos = FindCharacter(connection->tempXMLBuffer, connection->tempXMLBufferSize, '<');
					if(openPos < 0 || connection->tempXMLBufferSize <= openPos)
					{
						ui.AddToLog(("(" + connection->GetName() + ") XML Buffer is invalid").c_str());
						socket->Close();
						connection->isInUse = false;
						return;
					}
					if(openPos != 0)
					{
						for(int put=0, get=openPos ; get<connection->tempXMLBufferSize ; ++put, ++get)
							connection->tempXMLBuffer[put] = connection->tempXMLBuffer[get];
						connection->tempXMLBufferSize -= openPos;
					}

					int closePos = FindCharacter(connection->tempXMLBuffer, connection->tempXMLBufferSize, '>');
					if(0 < closePos || closePos < connection->tempXMLBufferSize)
					{
						ParseXMLMessage(*connection, reinterpret_cast<char*>(connection->tempXMLBuffer), closePos + 1);

						for(int put=0, get=closePos+1 ; get<connection->tempXMLBufferSize ; ++put, ++get)
							connection->tempXMLBuffer[put] = connection->tempXMLBuffer[get];
						connection->tempXMLBufferSize -= closePos+1;
					}
				}
			} while(bufferSize != 0);

			connection->isInUse = false;
		}
	}
	void OnConnectionClosed(WebSocketServerBase *server, SocketBase *socket)
	{
		std::lock_guard<std::mutex> lock(connectionsPendingDeleteLock);
		connectionsPendingDelete.push_back(socket);
	}
};

class CGSServerSSLListener : public OpenSSLListener
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
#else
void ClientCallbackFn(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg)
{
	if(msg->type == ix::WebSocketMessageType::Open)
	{
		switch(runState)
		{
		case RunState::PreGame:
			webSocket.sendText("<pre len=\"" + std::to_string(totalLength) + "\" />");
			break;
		case RunState::InGame:
		{
			int64_t allProgress = std::time(nullptr) - startTime;
			int64_t remainingTime = totalLength - allProgress;
			int index = (currentShuffle < 0) ? 0 : currentShuffle;
			webSocket.sendText("<swi game=\"" + std::to_string(shuffles[index].index) + "\" time=\"" + std::to_string(remainingTime) + "\" />");
		}
			break;
		case RunState::PostGame:
			webSocket.sendText("<end />");
			break;
		}
	}
	else if(msg->type == ix::WebSocketMessageType::Message)
	{
		try
		{
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_buffer(msg->str.c_str(), msg->str.size());
			if(result)
			{
				// TODO: Commands from the client get parsed here
			}
		}
		catch(...)
		{
		}
	}
}
#endif

class CGSServerUIFunctionPack : public ServerUIFunctions
{
	bool inEndTime = false;
	std::chrono::time_point<std::chrono::system_clock> endRequestTime;
#if USE_NS_WEBSOCKETSERVER == 1
	WebSocketServer<CGSServerWebSocketFunctions, CGSServerSSLListener, 16> *server = nullptr;
#else
	ix::WebSocketServer *server = nullptr;
#endif
public:
	void GetRightStatsLine1(char line[128])
	{
		switch(runState)
		{
		case RunState::PreGame:
			std::strcpy(line, "Phase: Pre-Game");
			break;

		case RunState::InGame:
		{
			int64_t allProgress = std::time(nullptr) - startTime;
			int64_t remainingTime = totalLength - allProgress;
			int hours = remainingTime / 3600;
			if(99 < hours) hours = 99;
			int minutes = (remainingTime / 60) % 60;
			int seconds = remainingTime % 60;
			std::snprintf(line, 128, "Phase: Running    %02d:%02d:%02d", hours, minutes, seconds);
		}
			break;

		case RunState::PostGame:
			std::strcpy(line, "Phase: After Game");
			break;

		default:
			line[0] = 0;
		}
	}
	void GetRightStatsLine2(char line[128])
	{
		int numberOfConnections = 0;
		{
			std::lock_guard<std::mutex> lock(openConnectionsLock);
			numberOfConnections = int(openConnections.size());
		}
		std::snprintf(line, 128, "Player Count: %d", numberOfConnections);
	}

	int GetMaxInputTemplates(void)
	{
		return 3;
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

		case 2:
			std::strcpy(line, "Start");
			break;

		default:
			line[0] = 0;
		}
	}
	void ProcessInput(char line[128])
	{
		for(int i=0 ; i<128 ; ++i)
			line[i] = char(std::tolower(line[i]));
		if(inEndTime)
		{
			if(std::strcmp(line, "yes") == 0)
				ui.StopRunning();
			else
			{
				ui.GoToBottomOfLog();
				ui.AddToLog("Canceling End Request");
				inEndTime = false;
			}
		}
		else if(std::strcmp(line, "quit") == 0 || std::strcmp(line, "end") == 0)
		{
			if(runState == RunState::PostGame)
				ui.StopRunning();
			else
			{
				ui.GoToBottomOfLog();
				ui.AddToLog("Ending the server while event is in progress? (Confirm with yes in 10 seconds)");
				inEndTime = true;
				endRequestTime = std::chrono::system_clock::now();
			}
		}
		else if(std::strcmp(line, "tail") == 0)
			ui.GoToBottomOfLog();
		else if(std::strcmp(line, "start") == 0)
		{
			if(runState == RunState::PreGame)
			{
				ui.AddToLog("Starting the shuffler");
				startTime = std::time(nullptr);
				currentShuffleStartTime = startTime.load();
				runState = RunState::InGame;
			}
		}
	}
	void Tick(void)
	{
		if(inEndTime)
		{
			if(endRequestTime + std::chrono::seconds(10) < std::chrono::system_clock::now())
			{
				ui.AddToLog("Canceling End Request");
				inEndTime = false;
			}
		}

		if(runState == RunState::InGame)
		{
			int64_t clock = std::time(nullptr);
			int64_t progress = clock - currentShuffleStartTime;
			if(currentShuffle == -1 || shuffles[currentShuffle].timeLength <= progress)
			{
				if(currentShuffle + 1 < shuffles.size())
				{
					currentShuffleStartTime = clock;
					++currentShuffle;

					int64_t allProgress = clock - startTime;
					int64_t remainingTime = totalLength - allProgress;
					std::string message = "<swi game=\"" + std::to_string(shuffles[currentShuffle].index) + "\" time=\"" + std::to_string(remainingTime) + "\" />";
#if USE_NS_WEBSOCKETSERVER == 1
					server->BroadcastData(message.c_str(), message.size());
#else
					for(auto &con : server->getClients())
						con->sendText(message);
#endif
					ui.AddToLog(("Switching to game " + std::to_string(shuffles[currentShuffle].index)).c_str());
				}
				else
				{
					runState = RunState::PostGame;
#if USE_NS_WEBSOCKETSERVER == 1
					server->BroadcastData("<end />", 7);
#else
					for(auto &con : server->getClients())
						con->sendText("<end />");
#endif
					ui.AddToLog("Time's Out! Ending the shuffler");
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(connectionsPendingDeleteLock);
			if(!connectionsPendingDelete.empty())
			{
				for(auto & deletableConnection : connectionsPendingDelete)
				{
					std::lock_guard<std::mutex> lock(openConnectionsLock);
					auto connection = openConnections.find(deletableConnection);
					bool goodToRemove = false;
					if(connection != openConnections.end())
					{
						if(!connection->second.isInUse)
						{
							goodToRemove = true;
						}
					}
					if(goodToRemove)
					{
						openConnections.erase(connection);
						deletableConnection = nullptr;
					}
				}
			}
			connectionsPendingDelete.erase(std::remove(connectionsPendingDelete.begin(), connectionsPendingDelete.end(), nullptr), connectionsPendingDelete.end());
		}
	}
#if USE_NS_WEBSOCKETSERVER == 1
	void SetServer(WebSocketServer<CGSServerWebSocketFunctions, CGSServerSSLListener, 16> *newServer)
#else
	void SetServer(ix::WebSocketServer *newServer)
#endif
	{
		server = newServer;
	}
};

void MakeRandomFromParams(int min, int max, int seed, int gameCount, int length)
{
	totalLength = length;
	int predictedShuffleCount = (length / (max/2)) + 10;
	shuffles.reserve(predictedShuffleCount);
	std::srand(seed);
	int lastGame = -1;

	for(int t=length ; 0<t ;)
	{
		int gameIndex = 0;
		if(1 < gameCount)
		{
			do
			{
				gameIndex = std::rand() % gameCount;
			} while(lastGame == gameIndex);
			lastGame = gameIndex;
		}

		if(t < max)
		{
			shuffles.push_back(ShuffleEntry(gameIndex, t));
			t = 0;
		}
		else
		{
			int thisLength = (std::rand() % max) + 1;
			if(thisLength < min)
				thisLength = min;
			shuffles.push_back(ShuffleEntry(gameIndex, thisLength));
			t -= thisLength;
		}
	}
}

int main(int argc, char *args[])
{
	cli::Parser argGetter(argc, args);
	argGetter.set_required<int>("p", "port", "Port to open the websocket server on");
	argGetter.set_optional<int>("l", "lower", 15, "Minimum number of seconds in a game shuffle (when generating randomly)");
	argGetter.set_optional<int>("u", "upper", 120, "Maximum number of seconds in a game shuffle (when generating randomly)");
	argGetter.set_optional<int>("s", "seed", int(std::time(nullptr)), "Value to seed the RNG with (when generating randomly)");
	argGetter.set_optional<int>("c", "count", 1, "Number of games in the shuffle (when generating randomly)");
	argGetter.set_optional<int>("t", "time", 30 * 60, "Total time in seconds of the event (when generating randomly)");
	argGetter.set_optional<std::string>("i", "input", std::string(), "File with shuffle parameters to use");
	argGetter.set_optional<std::string>("o", "output", std::string(), "File to save generated shuffle list to (will exit after saving)");
	argGetter.set_optional<std::string>("k", "certKey", std::string(), "SSL key file");
	argGetter.set_optional<std::string>("q", "certFile", std::string(), "SSL cert file");
	argGetter.run_and_exit_if_error();

	{
		std::string inFilename = std::move(argGetter.get<std::string>("i"));
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
			auto settingNode = doc.child("setting");
			if(!listNode.empty())
			{
				for(auto entryNode : listNode.children("entry"))
				{
					auto gameIndexAttribute = entryNode.attribute("game");
					auto lengthAttribute = entryNode.attribute("length");
					shuffles.push_back(ShuffleEntry(gameIndexAttribute.as_int(0), lengthAttribute.as_int(5)));
				}
				totalLength = 0;
				for(auto entry : shuffles)
					totalLength += entry.timeLength;
			}
			else if(!settingNode.empty())
			{
				auto minAttribute = settingNode.attribute("min");
				auto maxAttribute = settingNode.attribute("max");
				auto seedAttribute = settingNode.attribute("seed");
				auto gameCountAttribute = settingNode.attribute("gameCount");
				auto lengthSecondsAttribute = settingNode.attribute("lengthSeconds");
				MakeRandomFromParams(minAttribute.as_int(15), maxAttribute.as_int(120), seedAttribute.as_int(std::time(nullptr)), gameCountAttribute.as_int(1), lengthSecondsAttribute.as_int(1800));
			}
			else
			{
				std::cout << "Failed to read xml\n";
				return 1;
			}
		}
		else
		{
			int min = argGetter.get<int>("l");
			if(min <= 0)
				min = 15;
			int max = argGetter.get<int>("u");
			if(max < min)
				max = min;
			int count = argGetter.get<int>("c");
			if(count <= 0)
				count = 1;
			int time = argGetter.get<int>("t");
			if(time <= 0)
				time = 30 * 60;

			MakeRandomFromParams(min, max, argGetter.get<int>("s"), count, time);

			std::string outFilename = std::move(argGetter.get<std::string>("o"));
			if(!outFilename.empty())
			{
				pugi::xml_document doc;
				auto listNode = doc.append_child("list");
				for(auto entry : shuffles)
				{
					auto entryNode = listNode.append_child("entry");
					auto gameIndexAttribute = entryNode.append_attribute("game");
					gameIndexAttribute.set_value(entry.index);
					auto lengthAttribute = entryNode.append_attribute("length");
					lengthAttribute.set_value(entry.timeLength);
				}
				if(doc.save_file(outFilename.c_str()))
					std::cout << "Save Successful\n";
				else
					std::cout << "Save Failed\n";
				return 0;
			}
		}
	}

#if USE_NS_WEBSOCKETSERVER == 1
	certFile = std::move(argGetter.get<std::string>("q"));
	keyFile = std::move(argGetter.get<std::string>("k"));

	WebSocketServer<CGSServerWebSocketFunctions, CGSServerSSLListener, 16> server;
	if(server.RunAsync(argGetter.get<int>("p")) == false)
	{
		std::cout << "ERROR: Server failed to start\n";
		return 1;
	}

	CGSServerUIFunctionPack CGSServerUIFunctions;
	CGSServerUIFunctions.SetServer(&server);
#else
	ix::initNetSystem();
	ix::WebSocketServer server(argGetter.get<int>("p"), "0.0.0.0", ix::SocketServer::kDefaultTcpBacklog, 256, ix::WebSocketServer::kDefaultHandShakeTimeoutSecs, ix::SocketServer::kDefaultAddressFamily);
	server.setOnClientMessageCallback(ClientCallbackFn);
	server.disablePerMessageDeflate();
	ix::SocketTLSOptions secure;
	secure.certFile = argGetter.get<std::string>("q");
	secure.keyFile = argGetter.get<std::string>("k");
	secure.tls = true;
	secure.caFile = "NONE";
	server.setTLSOptions(secure);
	if(server.listenAndStart() == false)
	{
		std::cout << "ERROR: Server failed to start\n";
		return 1;
	}

	CGSServerUIFunctionPack CGSServerUIFunctions;
	CGSServerUIFunctions.SetServer(&server);
#endif
	ui.Run(CGSServerUIFunctions);
	return 0;
}
