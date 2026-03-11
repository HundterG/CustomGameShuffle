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

#include "xml/pugixml.cpp"

#include "cli/cmdparser.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <atomic>

#include <ncurses.h>

class ServerUIFunctions
{
public:
	virtual void GetRightStatsLine1(char line[128]) = 0;
	virtual void GetRightStatsLine2(char line[128]) = 0;

	virtual int GetMaxInputTemplates(void) = 0;
	virtual void GetInputTemplate(int index, char line[128]) = 0;
	virtual void ProcessInput(char line[128]) = 0;

	virtual void Tick(void) {}
};

class ServerUI
{
	std::mutex logLock;
	char logBuffer[1024][512] = {0};
	int logScrollFromBottom = 0;
	int logBottomIndex = 0;
	int logSize = 0;

	char inputString[128 + 4] = {0};
	int inputStringSize = 0;
	int inputTemplateIndex = 0;

	std::atomic_bool running = ATOMIC_VAR_INIT(false);

public:
	void Run(ServerUIFunctions &functions)
	{
		bool expected = false;
		if(!running.compare_exchange_strong(expected, true))
			return;

		setlocale(LC_ALL, "");
		initscr();
		cbreak();
		noecho();
		keypad(stdscr, TRUE);
		curs_set(1);
		mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
		timeout(100);

		if(has_colors())
		{
			start_color();
			use_default_colors();
			init_pair(1, COLOR_WHITE, -1);
			init_pair(2, COLOR_CYAN, -1);
			init_pair(3, COLOR_GREEN, -1);
			init_pair(4, COLOR_YELLOW, -1);
			init_pair(5, COLOR_RED,-1);
		}

		int xSize=0, ySize=0, xMid=0;
		getmaxyx(stdscr, ySize, xSize);
		xMid = xSize / 2;

		WINDOW *statsLeft = newwin(2, xMid-1, 0, 0);
		WINDOW *statsRight = newwin(2, xMid-1, 0, xMid+1);
		WINDOW *log = newwin(ySize-5, xSize, 3, 0);
		WINDOW *inputLine = newwin(1, xSize, ySize-1, 0);
		WINDOW *borders = newwin(ySize, xSize, 0, 0);

		mvwvline(borders, 0, xMid, '|', 2);
		mvwhline(borders, 2, 0, '-', xSize);
		mvwhline(borders, ySize-2, 0, '-', xSize);

		std::chrono::time_point<std::chrono::system_clock> systemStatsLastUpdate;

		while(running)
		{
			functions.Tick();

			mvwvline(borders, 0, xMid, '|', 2);
			mvwhline(borders, 2, 0, '-', xSize);
			mvwhline(borders, ySize-2, 0, '-', xSize);

			if(systemStatsLastUpdate + std::chrono::seconds(1) < std::chrono::system_clock::now())
			{
				std::ifstream file("/proc/meminfo");
				if(file.is_open())
				{
					std::string line, key;
					int totalMem = 0;
					int freeMem = 0;
					int value = 0;
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

					if(0 < totalMem && 0 < freeMem)
					{
						int used = totalMem - freeMem;
						float usedP = float(used)/totalMem;
						int color = 5; // Red
						if(usedP < 0.5f)
							color = 3; // Green
						else if(usedP < 0.9f)
							color = 4; // Yellow

						werase(statsLeft);
						wattron(statsLeft, has_colors() ? COLOR_PAIR(color)|A_BOLD : A_BOLD);
						mvwprintw(statsLeft, 0, 1, "Mem: %f%%", (usedP*100));
						wattroff(statsLeft, has_colors() ? COLOR_PAIR(color)|A_BOLD : A_BOLD);
						// Cpu usage at some point
					}
				}

				{
					char rightLine1[128 + 4] = {0};
					char rightLine2[128 + 4] = {0};
					functions.GetRightStatsLine1(rightLine1);
					functions.GetRightStatsLine2(rightLine2);

					werase(statsRight);
					wattron(statsRight, A_BOLD);
					mvwaddnstr(statsRight, 0, 1, rightLine1, 128);
					mvwaddnstr(statsRight, 1, 1, rightLine2, 128);
					wattroff(statsRight, A_BOLD);
				}

				systemStatsLastUpdate = std::chrono::system_clock::now();
			}

			{
				werase(log);
				std::lock_guard<std::mutex> lock(logLock);
				int scanline = ySize - 6;
				int currentLogLine = (logBottomIndex - logScrollFromBottom) & 0x03FF;
				int visibleLinesRemaining = logSize - logScrollFromBottom;
				while(0 <= scanline)
				{
					if(visibleLinesRemaining <= 0)
					{
						--logScrollFromBottom;
						if(logScrollFromBottom < 0)
							logScrollFromBottom = 0;
						break;
					}

					int lineSize = std::strlen(logBuffer[currentLogLine]);
					int lineCost = (lineSize / xSize) + 1;

					int currentDrawLine = scanline - (lineCost-1);
					for(int i=0 ; i<lineCost ; ++i)
					{
						if(0 <= currentDrawLine && (i*xSize) < lineSize)
							mvwaddnstr(log, currentDrawLine, 0, logBuffer[currentLogLine] + (i*xSize), lineSize - (i*xSize));
						++currentDrawLine;
					}

					scanline -= lineCost;
					--visibleLinesRemaining;
					currentLogLine = (currentLogLine-1) & 0x03FF;
				}
			}
			
			werase(inputLine);
			wattron(inputLine, has_colors() ? COLOR_PAIR(2)|A_BOLD : A_BOLD);
			mvwaddnstr(inputLine, 0, 1, ">>>", 3);
			wattroff(inputLine, has_colors() ? COLOR_PAIR(2)|A_BOLD : A_BOLD);
			if(0 < inputStringSize)
				mvwaddnstr(inputLine, 0, 5, inputString, inputStringSize);
			wmove(inputLine, 0, 5+inputStringSize);

			wnoutrefresh(borders);
			wnoutrefresh(statsLeft);
			wnoutrefresh(statsRight);
			wnoutrefresh(log);
			wnoutrefresh(inputLine);
			doupdate();

			int ch = getch();
			switch(ch)
			{
			case ERR:
				// Just means no input
				break;

			case KEY_RESIZE:
				getmaxyx(stdscr, ySize, xSize);
				xMid = xSize / 2;

				wresize(statsLeft, 2, xMid-1);
				mvwin(statsLeft, 0, 0);

				wresize(statsRight, 2, xMid-1);
				mvwin(statsRight, 0, xMid+1);

				wresize(log, ySize-5, xSize);
				mvwin(log, 3, 0);

				wresize(inputLine, 1, xSize);
				mvwin(inputLine, ySize-1, 0);

				wresize(log, ySize, xSize);
				mvwin(log, 0, 0);
				break;

			case KEY_BACKSPACE:
			case 127: // ASCII Delete
			case 8: // ASCII BackSpace
				if(0 < inputStringSize)
				{
					inputTemplateIndex = -1;
					inputString[inputStringSize] = 0;
					--inputStringSize;
				}
				break;

			case KEY_ENTER:
			case 13: // ASCII CR
			case 10: // ASCII NL
				if(0 < inputStringSize)
				{
					functions.ProcessInput(inputString);
					inputTemplateIndex = -1;
					logScrollFromBottom = 0;
					inputString[0] = 0;
					inputStringSize = 0;
				}
				break;

			case KEY_MOUSE:
			{
				MEVENT mouseEvent;
				if(getmouse(&mouseEvent) == OK)
				{
					if(mouseEvent.bstate & BUTTON4_PRESSED)
					{
						++logScrollFromBottom;
					}
					else if(mouseEvent.bstate & BUTTON5_PRESSED)
					{
						--logScrollFromBottom;
						if(logScrollFromBottom < 0)
							logScrollFromBottom = 0;
					}
				}
			}
				break;

			case KEY_UP:
				if(inputTemplateIndex - 1 < 0)
					inputTemplateIndex = functions.GetMaxInputTemplates() - 1;
				functions.GetInputTemplate(inputTemplateIndex, inputString);
				inputStringSize = std::strlen(inputString);
				break;

			case KEY_DOWN:
				if(functions.GetMaxInputTemplates() <= inputTemplateIndex + 1)
					inputTemplateIndex = 0;
				functions.GetInputTemplate(inputTemplateIndex, inputString);
				inputStringSize = std::strlen(inputString);
				break;

			default:
				if(32 <= ch && ch <= 126 && inputStringSize < 128)
				{
					inputTemplateIndex = -1;
					inputString[inputStringSize] = char(ch);
					++inputStringSize;
					inputString[inputStringSize] = 0;
				}
				break;
			}
		}

		delwin(borders);
		delwin(inputLine);
		delwin(log);
		delwin(statsRight);
		delwin(statsLeft);
		endwin();
	}

	void AddToLog(char const *line)
	{
		std::lock_guard<std::mutex> lock(logLock);
		int lineSize = std::strlen(line);
		if(lineSize < 510)
		{
			logBottomIndex = ++logBottomIndex & 0x03FF;
			std::strcpy(logBuffer[logBottomIndex], line);
			if(logScrollFromBottom != 0)
				++logScrollFromBottom;
			++logSize;
			if(1024 < logSize)
				logSize = 1024;
		}
		else
		{
			// figure something out
		}
	}

	void GoToBottomOfLog(void)
	{
		logBottomIndex = 0;
	}

	void StopRunning(void)
	{
		running = false;
	}
};

// undef a bunch of ncurses stuff
#undef OK