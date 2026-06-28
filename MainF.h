#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <map>
#include <mutex>
struct Process {
    std::string name;
    float totalTime = 0.0f;  // Общее время работы за все сессии
};

class MainF {
public:
    MainF(HANDLE hStopEvent);
    ~MainF();
    HANDLE hStopEvent;
    // Основные функции
    void WriteFile();
    void ReadFile();
    void PrintProcesses();

    // Управление процессами
    void AddProcess(const std::string& name);
    void RemoveProcess(const std::string& name);
    void ClearProcesses();

    // Утилиты
    std::string FormatProcessTime(float seconds);
    std::string GetExePath();
    long long ParseTimeString(const std::string& timeStr);
    std::string BstrToUtf8(BSTR bstr);

    // Все вектора и мапы для сохранени  служб
    std::vector<Process> processes;
    std::map<DWORD, std::chrono::steady_clock::time_point> processStartTimes;
    std::map<DWORD, std::string> processNames;
    std::map<DWORD, std::string> processPids;

    std::recursive_mutex dataMutex; //блокарь потока для чтения и записи файлов

    void StartWMIMonitoring();
    std::chrono::steady_clock::time_point GetProcessStartTime(DWORD pid);
    std::vector<std::pair<DWORD, std::string>> GetAllRunningProcesses();
    DWORD GetParentProcessId(DWORD pid);
    std::string GetProcessNameByPid(DWORD pid);
    bool IsMainProcess(DWORD pid, const std::string& processName);

};