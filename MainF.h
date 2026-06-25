#pragma once
#include <iostream>
#include <string>
#pragma comment(lib, "wbemuuid.lib")
#include <Wbemidl.h>
#include <vector>
#include <chrono>
class MainF
{
public:
	struct Process {
		DWORD pid;
		std::string name;
		std::chrono::steady_clock::time_point startTime; // Точка времени запуска
	};
	std::vector<std::string> Tasks;
	std::vector<Process> Tasks_active;
	MainF();
	std::string BstrToUtf8(BSTR bstr);
	void CheckNameProcess(std::string Name, DWORD id, int type);
};

