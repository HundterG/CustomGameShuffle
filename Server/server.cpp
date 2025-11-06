#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

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
#include "ixwebsocket/IXSocketServer.cpp"
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

#include "xml/pugixml.cpp"

#include "cli/cmdparser.hpp"

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

std::atomic_bool running = ATOMIC_VAR_INIT(true);
std::atomic<RunState> runState = ATOMIC_VAR_INIT(RunState::PreGame);
std::atomic_int64_t startTime = ATOMIC_VAR_INIT(-1);
std::atomic_int64_t currentShuffleStartTime = ATOMIC_VAR_INIT(-1);

std::vector<ShuffleEntry> shuffles;
int currentShuffle = -1;

int totalLength = 0;

void ServerThreadFn(ix::WebSocketServer *server)
{
	while(running)
	{
		bool shouldSleep = true;
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

					for(auto &con : server->getClients())
						con->sendText(message);
					shouldSleep = false;
				}
				else
				{
					runState = RunState::PostGame;
					for(auto &con : server->getClients())
						con->sendText("<end />");
				}
			}
		}

		if(shouldSleep)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

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
}

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
	argGetter.set_optional<int>("f", "threads", 1, "Number of threads to use for sending game switch updates (NOTE: Not used yet)");
	argGetter.set_optional<int>("l", "lower", 15, "Minimum number of seconds in a game shuffle (when generating randomly)");
	argGetter.set_optional<int>("u", "upper", 120, "Maximum number of seconds in a game shuffle (when generating randomly)");
	argGetter.set_optional<int>("s", "seed", int(std::time(nullptr)), "Value to seed the RNG with (when generating randomly)");
	argGetter.set_optional<int>("c", "count", 1, "Number of games in the shuffle (when generating randomly)");
	argGetter.set_optional<int>("t", "time", 30 * 60, "Total time in seconds of the event (when generating randomly)");
	argGetter.set_optional<std::string>("i", "input", std::string(), "File with shuffle parameters to use");
	argGetter.set_optional<std::string>("o", "output", std::string(), "File to save generated shuffle list to (will exit after saving)");
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

	ix::initNetSystem();
	ix::WebSocketServer server(argGetter.get<int>("p"), "0.0.0.0", ix::SocketServer::kDefaultTcpBacklog, 256, ix::WebSocketServer::kDefaultHandShakeTimeoutSecs, ix::SocketServer::kDefaultAddressFamily);
	server.setOnClientMessageCallback(ClientCallbackFn);
	server.disablePerMessageDeflate();
	if(server.listenAndStart() == false)
	{
		std::cout << "ERROR: Server failed to start\n";
		return 1;
	}

	std::thread serverThread(ServerThreadFn, &server);

	while(running)
	{
		std::string line;
		std::cout << ">>> ";
		std::getline(std::cin, line);
		std::for_each(line.begin(), line.end(), [](char &c){ c = char(std::tolower(c)); });

		if(line == "memory")
		{
			std::ifstream file("/proc/meminfo");
			if(file.is_open())
			{
				std::string line, key;
				int totalMem = 0;
				int freeMem = 0;
				int value;
				while(std::getline(file, line))
				{
					std::istringstream lineAsStream(line);
					while(lineAsStream >> key >> value)
					{
						if(key == "MemTotal:")
							totalMem = value;
						else if(key == "MemFree:")
							freeMem = value;
					}
				}

				if(totalMem <= 0 || freeMem <= 0)
					std::cout << "Failed to get memory info\n";
				else
				{
					int used = totalMem - freeMem;
					std::cout << "Used:\t\t" << used << " KB\nTotal:\t\t" << totalMem << " KB\n";
				}
			}
			else
				std::cout << "Failed to get memory info\n";
		}

		else if(line == "stats")
		{
			char const *states[] = {"Pre-Game", "Running", "After Game"};
			std::cout << "Phase: " << states[runState] << "\n";
			std::cout << "Player Count: " << server.getConnectedClientsCount() << "\n";
			if(0 < startTime)
			{
				int64_t progress = std::time(nullptr) - startTime;
				int64_t remainingTime = totalLength - progress;
				std::cout << "Remaining Time: " << remainingTime << " Seconds\n";
			}
			else
			{
				std::cout << "Run Length: " << totalLength << " Seconds\n";
			}
		}

		else if(line == "start")
		{
			startTime = std::time(nullptr);
			currentShuffleStartTime = startTime.load();
			runState = RunState::InGame;
		}
	
		else if(line == "end" || line == "exit")
		{
			if(runState == RunState::PostGame)
				running = false;
			else
			{
				std::cout << "Ending the server while event is in progress?\n>>> ";
				std::getline(std::cin, line);
				if(1 <= line.size())
				{
					if(line[0] == 'y' || line[0] == 'Y')
						running = false;
				}
			}
		}

		else
		{
			std::cout << "Commands:\n\tStart\n\tEnd\n\tExit\n\tMemory\n\tStats\n";
		}
	}

	serverThread.join();
	server.stop();
	ix::uninitNetSystem();
	return 0;
}
