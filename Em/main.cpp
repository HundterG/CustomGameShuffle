#include <emscripten/emscripten.h>
#include <emscripten/webaudio.h>
#include <emscripten/wasm_worker.h>
#include <emscripten/atomic.h>
#include <emscripten/websocket.h>
#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glut.h>
#include <fstream>
#include <string>
#include <vector>
#include "log.inl"
#include "GLArgParser.inl"
#include "debug.h"
#include <cstdlib>
#include "xml/pugixml.cpp"

#define SERVER_LOCATION "ws://localhost:8085/"

// /home/dev/Desktop/emscript/emsdk/upstream/emscripten/emcc main.cpp -o ../Client/main.html -sEXPORTED_FUNCTIONS=_ResizeCall,_main,_DoInput,_DebugSetGame,_DebugSave,_DebugEnableStep,_DebugDoStep,_DebugStart -sEXPORTED_RUNTIME_METHODS=[HEAPU8] --embed-file files@/ -sAUDIO_WORKLET -sWASM_WORKERS -sNO_DISABLE_EXCEPTION_CATCHING -s TOTAL_MEMORY=268435456 -lwebsocket.js

#if defined(CGS_DEBUG_COMMANDS)
// Maybe someday ill include the full thing
// Or not considering how much editing I had to do to make this work
//#include <emscripten_browser_file.h>

#define _EM_JS_INLINE(ret, c_name, js_name, params, code)                          \
  extern "C" {                                                                     \
  ret c_name params EM_IMPORT(js_name);                                            \
  EMSCRIPTEN_KEEPALIVE                                                             \
  __attribute__((section("em_js"), aligned(1))) inline char __em_js__##js_name[] = \
    #params "<::>" code;                                                           \
  }

#define EM_JS_INLINE(ret, name, params, ...) _EM_JS_INLINE(ret, name, name, params, #__VA_ARGS__)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-variable-declarations"
EM_JS_INLINE(void, download, (char const *filename, void const *buffer, size_t buffer_size), {
	/// Offer a buffer in memory as a file to download, specifying download filename and mime type
	var a = document.createElement('a');
	a.download = UTF8ToString(filename);
	var bufferCopy = new ArrayBuffer(buffer_size);
	var uint8Array = new Uint8Array(bufferCopy);
	uint8Array.set(new Uint8Array(Module["HEAPU8"].buffer, buffer, buffer_size));
	a.href = URL.createObjectURL(new Blob([uint8Array], {type: "application/octet-stream"}));
	a.click();
});
#pragma GCC diagnostic pop
#endif

class GameBase
{
public:
	virtual void Init(std::string &config) {}
	virtual void InitRender(void) {}
	virtual void Tick(void) {}
	virtual void SetControllerState(bool a, bool b, bool l, bool r, bool up, bool down, bool left, bool right) {}
	virtual void SetStartThisFrame(void) {}
	virtual void Render(void)
	{
		// black screen with error text
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	virtual void OnSwapOn(void) {}
	virtual void OnSwapOff(void) {}
#if defined(CGS_DEBUG_COMMANDS)
	virtual void SaveState(void) {}
#endif
};

EM_JS(int, GetGameCanvasWidth, (), {
	var gameCanvas = document.querySelector('.webGLCanvas');
	return gameCanvas.clientWidth;
});

EM_JS(int, GetGameCanvasHeight, (), {
	var gameCanvas = document.querySelector('.webGLCanvas');
	return gameCanvas.clientHeight;
});

#include "CommonAudio.inl"
#include "CommonVideo.inl"
#include "testgear.inl"
#include "Sky/sky.inl"
#include "NES/NESEmulator.inl"

EM_JS(void, SetProgress, (int p), {
	var progressElement = document.querySelector('.progressBar');
	progressElement.style.width = p + "%";
	console.log("Progress " + p + "%");
});

EM_JS(void, SetError, (), {
	var box = document.querySelector('.box');
	box.innerHTML = "<div class='errorText'>A setup error has occurred</div>";
});

EM_JS(void, SetTimer, (int min, float sec), {
	var timerElement = document.querySelector('.timer');
	if(min <= 0)
	{
		if(sec < 1)
		{
			timerElement.innerHTML = "1";
			return;
		}
		if(sec < 2)
		{
			timerElement.innerHTML = "2";
			return;
		}
		if(sec < 3)
		{
			timerElement.innerHTML = "3";
			return;
		}
		if(sec < 4)
		{
			timerElement.innerHTML = "4";
			return;
		}
		if(sec < 5)
		{
			timerElement.innerHTML = "5";
			return;
		}
	}
	timerElement.innerHTML = min + ":" + sec.toFixed(2).padStart(5, '0');
});

EM_JS(void, SetTimerTime, (), {
	var timerElement = document.querySelector('.timer');
	timerElement.innerHTML = "Time's Out";
});

EM_JS(void, SetPreGame, (int show), {
	var pregameElement = document.querySelector('.pregame');
	pregameElement.style.display = (show==0) ? "none" : "block";
});

EM_JS(void, SetEndGame, (), {
	resizeCodeReady = false;
	var box = document.querySelector('.box');
	box.style.width = '1000px';
	box.style.background = '#f0f0f0';
	box.innerHTML = '<h1 class="headerText">Thanks For Playing!</h1>';
	var foot = document.querySelector('.footer');
	foot.style.display = 'table';
	var gameBox = document.querySelector('.gameBox');
	gameBox.style.display = 'none';
	var gameCanvas = document.querySelector('.webGLCanvas');
	gameCanvas.style.display = 'none';
});

namespace
{
	emscripten_lock_t currentIndexLock = EMSCRIPTEN_LOCK_T_STATIC_INITIALIZER;
	int currentIndex = 0;
	GameBase NullGame;
	std::vector<GameBase*> Games;
	// A B L R Left Right Up Down
	bool buttons[8] = {false, false, false, false, false, false, false, false};

#if defined(CGS_DEBUG_COMMANDS)
	bool debugEnableStep = false;
	bool debugDoStep = false;
	bool debugDoSave = false;
	int debugDoStart = 0;
	emscripten_lock_t debugDownloadLock = EMSCRIPTEN_LOCK_T_STATIC_INITIALIZER;
	void *debugSaveData = nullptr;
	size_t debugSaveSize = 0;
#endif

	enum class LoadStage
	{
		Entry,
		OpenGameListFile,
		SetupEmulators_Game,
		SetupEmulators_Render,
		ConnectToServer,
		WaitForServer,
		BetweenServerAndWait,
		WaitForStart,
		InProgress,
		GameEndWait,
		AfterGame,
	};

	LoadStage loadStage = LoadStage::Entry;
	int targetGameCount = 0;
	int tempLoopCounter = 0;
	std::ifstream gameListFile;
	// Needs to be manually set in previous state
	double stageStartTime = 0;
	int remainingTime = 0;
	emscripten_wasm_worker_t emulatorTickThread;
	EMSCRIPTEN_WEBSOCKET_T serverConnection = 0;
}

extern "C"
{
	void ResizeCall(void)
	{
		int width = GetGameCanvasWidth();
		int height = GetGameCanvasHeight();
		glutReshapeWindow(width, height);
	}

	void DoInput(int button, int isDown)
	{
		if(0 <= button && button < 8)
			buttons[button] = 0 < isDown;
	}

	void SetGameIndex(int newIndex)
	{
		if(0 <= newIndex && newIndex < Games.size())
		{
			emscripten_lock_waitinf_acquire(&currentIndexLock);
			Games[currentIndex]->OnSwapOff();
			currentIndex = newIndex;
			Games[currentIndex]->OnSwapOn();
			emscripten_lock_release(&currentIndexLock);
		}
	}

	void DebugSetGame(int game)
	{
#if defined(CGS_DEBUG_COMMANDS)
		int newIndex = game - 1;
		SetGameIndex(newIndex);
#endif
	}

	void DebugSave(void)
	{
#if defined(CGS_DEBUG_COMMANDS)
		debugDoSave = true;
#endif
	}

	void DebugEnableStep(void)
	{
#if defined(CGS_DEBUG_COMMANDS)
		debugEnableStep = !debugEnableStep;
#endif
	}

	void DebugDoStep(void)
	{
#if defined(CGS_DEBUG_COMMANDS)
		debugDoStep = true;
#endif
	}

	void DebugStart(void)
	{
#if defined(CGS_DEBUG_COMMANDS)
		debugDoStart = 3;
#endif
	}

	int GetGameIndex(void)
	{
		emscripten_lock_waitinf_acquire(&currentIndexLock);
		int ret = currentIndex;
		emscripten_lock_release(&currentIndexLock);
		return ret;
	}
}

#if defined(CGS_DEBUG_COMMANDS)
void QueueDebugDownload(void const *buffer, size_t size)
{
	emscripten_lock_waitinf_acquire(&debugDownloadLock);
	if(debugSaveData == nullptr)
	{
		debugSaveData = malloc(size);
		memcpy(debugSaveData, buffer, size);
		debugSaveSize = size;
	}
	emscripten_lock_release(&debugDownloadLock);
}
#endif

void Tick(void);
bool OnServerOpen(int eventType, const EmscriptenWebSocketOpenEvent *e, void *userData);
bool OnServerMessage(int eventType, const EmscriptenWebSocketMessageEvent *e, void *userData);
bool OnServerClose(int eventType, const EmscriptenWebSocketCloseEvent *e, void *userData);
bool OnServerError(int eventType, const EmscriptenWebSocketErrorEvent *e, void *userData);

void Idle(void)
{
	FlushLog();

	switch(loadStage)
	{
	case LoadStage::Entry:
		SetProgress(40);
		CommonVideo::InitRenderStuff();
		loadStage = LoadStage::OpenGameListFile;
		break;

	case LoadStage::OpenGameListFile:
		// make gamelist an xml file
		gameListFile.open("GameList.txt");
		if(gameListFile.is_open())
		{
			std::string line;
			if(std::getline(gameListFile, line))
			{
				targetGameCount = std::stoi(line);
				tempLoopCounter = 0;
				loadStage = LoadStage::SetupEmulators_Game;
			}
			else
			{
				SetError();
				throw "GameList could not be loaded\n";
			}
		}
		else
		{
			SetError();
			throw "GameList could not be loaded\n";
		}
		break;

	case LoadStage::SetupEmulators_Game:
	{
		if(tempLoopCounter < targetGameCount)
		{
			std::string line;
			if(std::getline(gameListFile, line))
			{
				std::string code = line.substr(0, 4);
				GameBase *newGame = &NullGame;
				//if(code == "GB  ")
				if(code == "TEST")
					newGame = new GLGears();
				else if(code == "GB  ")
					newGame = new GBEmu();
				else if(code == "GBA ")
					newGame = new GBAEmu();
				else if(code == "NES ")
					newGame = new NESEmu();
				std::string config = line.substr(4);
				newGame->Init(config);
				Games.push_back(newGame);
				++tempLoopCounter;
				SetProgress(int((float(tempLoopCounter)/targetGameCount) * 30) + 40);
			}
			else
			{
				SetError();
				throw "GameList could not be read\n";
			}
		}
		else
		{
			gameListFile.close();
			tempLoopCounter = 0;
			loadStage = LoadStage::SetupEmulators_Render;
		}
	}
		break;

	case LoadStage::SetupEmulators_Render:
		if(tempLoopCounter < targetGameCount)
		{
			Games[tempLoopCounter]->InitRender();
			++tempLoopCounter;
			SetProgress(int((float(tempLoopCounter)/targetGameCount) * 20) + 70);
		}
		else
		{
			loadStage = LoadStage::ConnectToServer;
		}
		break;

	case LoadStage::ConnectToServer:
	{
		EmscriptenWebSocketCreateAttributes attr;
		emscripten_websocket_init_create_attributes(&attr);
		attr.url = SERVER_LOCATION;
		attr.protocols = "binary";
		// This variable is dumb
		attr.createOnMainThread = true;
		serverConnection = emscripten_websocket_new(&attr);
		if(serverConnection <= 0)
		{
			SetError();
			throw "Could not create websocket\n";
		}
		emscripten_websocket_set_onopen_callback(serverConnection, nullptr, OnServerOpen);
		emscripten_websocket_set_onmessage_callback(serverConnection, nullptr, OnServerMessage);
		emscripten_websocket_set_onclose_callback(serverConnection, nullptr, OnServerClose);
		emscripten_websocket_set_onerror_callback(serverConnection, nullptr, OnServerError);
		loadStage = LoadStage::WaitForServer;
	}
		break;

	case LoadStage::WaitForServer:
		break;

	case LoadStage::BetweenServerAndWait:
		if(stageStartTime + 10000 < emscripten_performance_now())
		{
			SetError();
			throw "No response from server\n";
		}
		break;

	case LoadStage::WaitForStart:
		break;

	case LoadStage::InProgress:
		glutPostRedisplay();
#if defined(CGS_DEBUG_COMMANDS)
		emscripten_lock_waitinf_acquire(&debugDownloadLock);
		if(debugSaveData != nullptr)
		{
			download("file", debugSaveData, debugSaveSize);
			free(debugSaveData);
			debugSaveData = nullptr;
			debugSaveSize = 0;
		}
		emscripten_lock_release(&debugDownloadLock);
#endif
		{
			double remainingSeconds = ((remainingTime * 1000.0f) - (emscripten_performance_now() - stageStartTime)) / 1000.0f;
			if(remainingSeconds < 0)
			{
				SetTimer(0, 0.0f);
			}
			else
			{
				int minutes = int(remainingSeconds) / 60;
				int seconds = int(remainingSeconds) % 60;
				double unused = 0.0;
				double subSecond = modf(remainingSeconds, &unused) + seconds;
				SetTimer(minutes, subSecond);
			}
		}
		break;

	case LoadStage::GameEndWait:
		if(stageStartTime + 5000 < emscripten_performance_now())
		{
			SetEndGame();
			loadStage = LoadStage::AfterGame;
		}
		break;

	case LoadStage::AfterGame:
		break;
	}
}

void Tick(void)
{
	double lastTime = -1.0;
	double overTime = 0.0;
	int debugFrames = 0;
	int debugTicks = 0;
	double debugLastTime = 0;

	while(loadStage == LoadStage::InProgress)
	{
		double t = emscripten_performance_now();
		++debugTicks;
		if(lastTime < 0)
		{
			lastTime = t;
			debugLastTime = t;
			continue;
		}

		double dt = t - lastTime;
		if((1000.0/60.0) - overTime < dt)
		{
			int gameIndex = GetGameIndex();
			Games[gameIndex]->SetControllerState(buttons[0], buttons[1], buttons[2], buttons[3], buttons[6], buttons[7], buttons[4], buttons[5]);
#if defined(CGS_DEBUG_COMMANDS)
			if(0 < debugDoStart)
			{
				Games[gameIndex]->SetStartThisFrame();
				--debugDoStart;
			}
			if(debugDoSave)
			{
				Games[gameIndex]->SaveState();
				debugDoSave = false;
			}
			if(debugEnableStep)
			{
				if(debugDoStep)
				{
					Games[gameIndex]->Tick();
					debugDoStep = false;
				}
			}
			else
				Games[gameIndex]->Tick();
#else
			Games[gameIndex]->Tick();
#endif
			lastTime = t;
			overTime = dt - (1000.0/60.0);
			++debugFrames;
		}
		else
			emscripten_wasm_worker_sleep(100000); // 1/10 ms

		if(debugLastTime + 1000 <= t)
		{
			WriteLog("FPS: %d - Ticks: %d\n", debugFrames, debugTicks);
			debugFrames = 0;
			debugTicks = 0;
			debugLastTime = t;
		}
	}
}

bool OnServerOpen(int /*eventType*/, const EmscriptenWebSocketOpenEvent *e, void*)
{
	SetProgress(100);
	stageStartTime = emscripten_performance_now();
	if(loadStage == LoadStage::WaitForServer)
		loadStage = LoadStage::BetweenServerAndWait;
	EM_ASM( resizeCodeReady = true; OnDownloadEnd(); );
	return false;
}

bool OnServerMessage(int /*eventType*/, const EmscriptenWebSocketMessageEvent *e, void*)
{
	if(e->data)
	{
		WriteLog("%s\n", e->data);
		do
		{
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_string(reinterpret_cast<char*>(e->data));
			if(!result)
				break;
			
			auto switchNode = doc.child("swi");
			if(!switchNode.empty())
			{
				if(loadStage != LoadStage::InProgress)
				{
					loadStage = LoadStage::InProgress;
					emulatorTickThread = emscripten_malloc_wasm_worker(1024 * 1024); // 1MB
					emscripten_wasm_worker_post_function_v(emulatorTickThread, Tick);
				}
				SetPreGame(0);
				stageStartTime = emscripten_performance_now();
				auto timeAttribute = switchNode.attribute("time");
				int time = timeAttribute.as_int();
				if(0 < time)
					remainingTime = time;
				auto gameAttribute = switchNode.attribute("game");
				SetGameIndex(gameAttribute.as_int());
				break;
			}

			auto preNode = doc.child("pre");
			if(!preNode.empty())
			{
				SetPreGame(1);
				auto timeAttribute = preNode.attribute("len");
				int time = timeAttribute.as_int();
				if(0 < time)
				{
					int minutes = time / 60;
					int seconds = time % 60;
					SetTimer(minutes, seconds);
				}
				loadStage = LoadStage::WaitForStart;
				break;
			}

			auto endNode = doc.child("end");
			if(!endNode.empty())
			{
				SetTimerTime();
				stageStartTime = emscripten_performance_now();
				loadStage = LoadStage::GameEndWait;
				break;
			}
		} while (0);
	}
	return false;
}

bool OnServerClose(int /*eventType*/, const EmscriptenWebSocketCloseEvent *e, void*)
{
	SetError();
	throw "Connection has been interupted\n";
}

bool OnServerError(int /*eventType*/, const EmscriptenWebSocketErrorEvent *e, void*)
{
	SetError();
	throw "Could not connect to server\n";
}

void Draw(void)
{
	if(loadStage == LoadStage::InProgress)
		Games[GetGameIndex()]->Render();
}

int main(int argc, char *argv[])
{
	InitAudio();

	glutInit(&argc, argv);
	glutInitWindowSize(GetGameCanvasWidth(), GetGameCanvasHeight());
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	glutCreateWindow("CGS");

	glutIdleFunc(Idle);
	glutDisplayFunc(Draw);
	
	glutMainLoop();
	return 0;
}
