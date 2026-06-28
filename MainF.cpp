#define _WIN32_DCOM
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#include "MainF.h"
#include <comdef.h>
#include <Wbemidl.h>
#include <tlhelp32.h>
#include <fcntl.h>
#include <io.h>
#include <set>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#pragma comment(lib, "advapi32.lib")
#include <mutex>

// ===== КОНСТРУКТОР =====
MainF::MainF(HANDLE hStopEvent)
    : hStopEvent(hStopEvent) {
	this->hStopEvent = hStopEvent;
    // Загружаем данные из файла
    ReadFile();

    std::cout << "=== List of processes for listening ===" << std::endl;
    PrintProcesses();
    std::cout << std::endl;

    // Запускаем WMI мониторинг
    StartWMIMonitoring();
}

MainF::~MainF() {
    WriteFile();
}

// ===== ЗАПИСЬ В ФАЙЛ =====
void MainF::WriteFile() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    std::string filePath = GetExePath() + "processes.txt";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        file.open("processes.txt");
        if (!file.is_open()) {
            return;
        }
    }

    time_t now_time = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_time);

    file << "=== List of processes for listening ===" << std::endl;
    file << "DATE: " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    file << std::endl;

    file << "--- Processes ---" << std::endl;
    if (processes.empty()) {
        file << "NONE" << std::endl;
    }
    else {
        for (size_t i = 0; i < processes.size(); ++i) {
            const auto& proc = processes[i];
            file << i + 1 << ". " << proc.name;

            // ===== ВАЖНО: Сохраняем totalTime по ИМЕНИ =====
            if (proc.totalTime > 0) {
                file << " [Time of Active: " << FormatProcessTime(proc.totalTime) << "]";
            }

            // Проверяем, запущен ли процесс сейчас (для отображения текущей сессии)
            for (const auto& [pid, startTime] : processStartTimes) {
                auto nameIt = processNames.find(pid);
                if (nameIt != processNames.end() && nameIt->second == proc.name) {
                    auto duration = std::chrono::steady_clock::now() - startTime;
                    float currentSession = std::chrono::duration<float>(duration).count();
                    if (currentSession > 0) {
                        file << " [NOW: " << FormatProcessTime(currentSession) << "]";
                    }
                    break;
                }
            }

            file << std::endl;
        }
    }
    file << std::endl;

    file << "=== END OF FILE ===" << std::endl;
    file.close();
}

// ===== ЧТЕНИЕ ИЗ ФАЙЛА =====
void MainF::ReadFile() {
    
    std::lock_guard<std::recursive_mutex> lock(dataMutex);

    std::string filePath = GetExePath() + "processes.txt";
    std::ifstream file(filePath);
    if (!file.is_open()) {
        file.open("processes.txt");
        if (!file.is_open()) {
            processes.clear();
            AddProcess("Example.exe");
            WriteFile();
            return;
        }
    }
    processes.clear();

    std::string line;
    bool readingProcesses = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            line = line.substr(0, end + 1);
        }

        if (line.find("--- Processes ---") != std::string::npos) {
            readingProcesses = true;
            continue;
        }

        if (line.find("===") != std::string::npos ||
            line.find("NONE") != std::string::npos ||
            line.find("DATE:") != std::string::npos ||
            line.find("END OF FILE") != std::string::npos) {
            readingProcesses = false;
            continue;
        }

        if (readingProcesses) {
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos && dotPos < 10) {
                std::string rest = line.substr(dotPos + 1);
                size_t startName = rest.find_first_not_of(" \t");
                if (startName != std::string::npos) {
                    // Парсим имя
                    size_t bracketPos = rest.find('[');
                    std::string processName;
                    float savedTotalTime = 0.0f;

                    if (bracketPos != std::string::npos) {
                        processName = rest.substr(startName, bracketPos - startName);
                        size_t endName = processName.find_last_not_of(" \t");
                        if (endName != std::string::npos) {
                            processName = processName.substr(0, endName + 1);
                        }

                        // ===== ПАРСИМ ОБЩЕЕ ВРЕМЯ =====
                        size_t timeStart = rest.find("Time of Active:");
                        if (timeStart != std::string::npos) {
                            timeStart = rest.find(':', timeStart) + 1;
                            size_t timeEnd = rest.find(']', timeStart);
                            if (timeEnd == std::string::npos) {
                                timeEnd = rest.find('[', timeStart);
                            }
                            if (timeEnd != std::string::npos) {
                                std::string timeStr = rest.substr(timeStart, timeEnd - timeStart);
                                size_t ts = timeStr.find_first_not_of(" \t");
                                if (ts != std::string::npos) {
                                    timeStr = timeStr.substr(ts);
                                    size_t te = timeStr.find_last_not_of(" \t");
                                    if (te != std::string::npos) {
                                        timeStr = timeStr.substr(0, te + 1);
                                    }
                                }
                                savedTotalTime = static_cast<float>(ParseTimeString(timeStr));
                            }
                        }
                    }
                    else {
                        processName = rest.substr(startName);
                        size_t endName = processName.find_last_not_of(" \t");
                        if (endName != std::string::npos) {
                            processName = processName.substr(0, endName + 1);
                        }
                    }

                    if (!processName.empty()) {
                        Process proc;
                        proc.name = processName;
                        proc.totalTime = savedTotalTime;  // ===== ЗАГРУЖАЕМ СОХРАНЕННОЕ ВРЕМЯ =====
                        processes.push_back(proc);
                    }
                }
            }
        }
    }

    file.close();
}

// ===== ПАРСИНГ ВРЕМЕНИ =====
long long MainF::ParseTimeString(const std::string& timeStr) {
    long long totalSeconds = 0;
    long long currentValue = 0;

    for (char c : timeStr) {
        if (c >= '0' && c <= '9') {
            currentValue = currentValue * 10 + (c - '0');
        }
        else if (c == 'h' || c == 'm' || c == 's') {
            if (currentValue > 0) {
                if (c == 'h') {
                    totalSeconds += currentValue * 3600;
                }
                else if (c == 'm') {
                    totalSeconds += currentValue * 60;
                }
                else if (c == 's') {
                    totalSeconds += currentValue;
                }
                currentValue = 0;
            }
        }
    }
    return totalSeconds;
}

// ===== ДОБАВЛЕНИЕ ПРОЦЕССА =====
void MainF::AddProcess(const std::string& name) {
    for (const auto& proc : processes) {
        if (proc.name == name) {
            std::cout << "Process " << name << " is in list already!" << std::endl;
            return;
        }
    }

    Process newProc;
    newProc.name = name;
    newProc.totalTime = 0.0f;
    processes.push_back(newProc);
    WriteFile();
    std::cout << "[+] Add to list: " << name << std::endl;
}

// ===== УДАЛЕНИЕ ПРОЦЕССА =====
void MainF::RemoveProcess(const std::string& name) {
    for (auto it = processes.begin(); it != processes.end(); ++it) {
        if (it->name == name) {
            processes.erase(it);
            WriteFile();
            std::cout << "[-] Del from list: " << name << std::endl;
            return;
        }
    }
    std::cout << "Process " << name << " isnt found!" << std::endl;
}

// ===== ОЧИСТКА =====
void MainF::ClearProcesses() {
    processes.clear();
    processStartTimes.clear();
    processNames.clear();
    WriteFile();
    std::cout << "[!] All of processes are removed" << std::endl;
}

// ===== ФОРМАТИРОВАНИЕ ВРЕМЕНИ =====
std::string MainF::FormatProcessTime(float seconds) {
    int hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "h ";
    }
    if (minutes > 0 || hours > 0) {
        oss << minutes << "m ";
    }
    oss << secs << "s";
    return oss.str();
}

// ===== ВЫВОД =====
void MainF::PrintProcesses() {
    if (processes.empty()) {
        std::cout << "list is empty" << std::endl;
        return;
    }

    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& proc = processes[i];
        std::cout << i + 1 << ". " << proc.name;
        if (proc.totalTime > 0) {
            std::cout << " [Time of Active: " << FormatProcessTime(proc.totalTime) << "]";
        }

        // Проверяем, запущен ли процесс сейчас
        for (const auto& [pid, startTime] : processStartTimes) {
            auto nameIt = processNames.find(pid);
            if (nameIt != processNames.end() && nameIt->second == proc.name) {
                auto duration = std::chrono::steady_clock::now() - startTime;
                float currentSession = std::chrono::duration<float>(duration).count();
                if (currentSession > 0) {
                    std::cout << " [Now session: " << FormatProcessTime(currentSession) << "]";
                }
                break;
            }
        }
        std::cout << std::endl;
    }
}

// ===== ПУТЬ К EXE =====
std::string MainF::GetExePath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path.substr(0, pos + 1);
    }
    return ".\\";
}

std::string MainF::BstrToUtf8(BSTR bstr) {
    if (!bstr) return "";

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return "";

    std::string strTo;
    strTo.resize(size_needed);

    int result = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, &strTo[0], size_needed, NULL, NULL);
    if (result <= 0) return "";

    // Убираем лишний нуль-терминатор
    while (!strTo.empty() && strTo.back() == '\0') {
        strTo.pop_back();
    }

    return strTo;
}

// ===== ПОИСК ВСЕХ ЗАПУЩЕННЫХ ПРОЦЕССОВ ===== Подзабыл где это вообще используеться
std::vector<std::pair<DWORD, std::string>> MainF::GetAllRunningProcesses() {
    std::vector<std::pair<DWORD, std::string>> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &processEntry)) {
        do {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, processEntry.szExeFile, -1, name, MAX_PATH, NULL, NULL);
            std::string processName(name);

            if (processName != "System" && processName != "System Idle Process") {
                result.push_back({ processEntry.th32ProcessID, processName });
            }
        } while (Process32Next(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return result;
}

// ===== ПОЛУЧЕНИЕ ВРЕМЕНИ ЗАПУСКА ПРОЦЕССА =====
std::chrono::steady_clock::time_point MainF::GetProcessStartTime(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        return std::chrono::steady_clock::now();
    }

    FILETIME createTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
        CloseHandle(hProcess);
        return std::chrono::steady_clock::now();
    }

    CloseHandle(hProcess);

    ULARGE_INTEGER ul;
    ul.LowPart = createTime.dwLowDateTime;
    ul.HighPart = createTime.dwHighDateTime;

    SYSTEMTIME stNow;
    GetSystemTime(&stNow);
    FILETIME ftNow;
    SystemTimeToFileTime(&stNow, &ftNow);

    ULARGE_INTEGER ulNow;
    ulNow.LowPart = ftNow.dwLowDateTime;
    ulNow.HighPart = ftNow.dwHighDateTime;

    auto diffSeconds = (ulNow.QuadPart - ul.QuadPart) / 10000000ULL;

    return std::chrono::steady_clock::now() - std::chrono::seconds(diffSeconds);
}

// ===== ПОЛУЧЕНИЕ РОДИТЕЛЬСКОГО PID (PPID) =====
DWORD MainF::GetParentProcessId(DWORD pid) {
    DWORD ppid = 0;

    // Способ 1: Через Toolhelp32Snapshot (более надежный)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return ppid;
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &processEntry)) {
        do {
            if (processEntry.th32ProcessID == pid) {
                ppid = processEntry.th32ParentProcessID;
                break;
            }
        } while (Process32Next(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return ppid;
}

// ===== ПОЛУЧЕНИЕ ИМЕНИ ПРОЦЕССА ПО PID =====
std::string MainF::GetProcessNameByPid(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return "";
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &processEntry)) {
        do {
            if (processEntry.th32ProcessID == pid) {
                char name[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, processEntry.szExeFile, -1, name, MAX_PATH, NULL, NULL);
                CloseHandle(snapshot);
                return std::string(name);
            }
        } while (Process32Next(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return "";
}

// ===== ПРОВЕРКА ЯВЛЯЕТСЯ ЛИ ПРОЦЕСС ГЛАВНЫМ =====
bool MainF::IsMainProcess(DWORD pid, const std::string& processName) {
    DWORD ppid = GetParentProcessId(pid);
    if (ppid == 0) return false;

    // Получаем имя родительского процесса
    std::string parentName = GetProcessNameByPid(ppid);

    // Главный процесс - если родитель НЕ является таким же процессом
    // (например, не дочерний процесс того же приложения)
    if (parentName != processName) {
        return true;
    }

    // Если родитель - тот же процесс, проверяем, не является ли он системным
    // или не был ли запущен из explorer.exe (часто признак главного)
    if (parentName == "explorer.exe" ||
        parentName == "cmd.exe" ||
        parentName == "powershell.exe") {
        return true;
    }

    return false;
}

// ===== WMI МОНИТОРИНГ (ИСПРАВЛЕННЫЙ) =====
void MainF::StartWMIMonitoring() {
    HRESULT hr;

    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Error CoInitializeEx: 0x" << std::hex << hr << std::endl;
        return;
    }

    hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        std::cerr << "Error CoInitializeSecurity: 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return;
    }

    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        std::cerr << "Error CoCreateInstance: 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return;
    }

    IWbemServices* pSvc = NULL;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hr)) {
        std::cerr << "Error ConnectServer: 0x" << std::hex << hr << std::endl;
        pLoc->Release();
        CoUninitialize();
        return;
    }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE);
    if (FAILED(hr)) {
        std::cerr << "Error CoSetProxyBlanket: 0x" << std::hex << hr << std::endl;
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return;
    }

    // ===== ЗАПРОС НА СОЗДАНИЕ =====
    IEnumWbemClassObject* pEnumeratorStart = NULL;
    bstr_t wqlStart(L"SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'");
    hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlStart,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumeratorStart);
    if (FAILED(hr)) {
        std::cerr << "Error ExecNotificationQuery (Start): 0x" << std::hex << hr << std::endl;
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return;
    }

    // ===== ЗАПРОС НА ЗАВЕРШЕНИЕ =====
    IEnumWbemClassObject* pEnumeratorStop = NULL;
    bstr_t wqlStop(L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'");
    hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlStop,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumeratorStop);
    if (FAILED(hr)) {
        std::cerr << "Error ExecNotificationQuery (Stop): 0x" << std::hex << hr << std::endl;
        pEnumeratorStop = NULL;
    }

    auto runningProcesses = GetAllRunningProcesses();
    /* Не удобная штука для debug полминуты выводит все процессы
    // ===== ПОКАЗЫВАЕМ ВСЕ ЗАПУЩЕННЫЕ ПРОЦЕССЫ =====
    std::cout << "\n=== All of Processes that are active ===" << std::endl;
    for (const auto& [pid, name] : runningProcesses) {
        // Определяем, является ли процесс главным
        bool isMain = IsMainProcess(pid, name);
        std::cout << "  " << name << " (PID: " << pid
            << ", Main: " << (isMain ? "YES" : "NO")
            << ", PPID: " << GetParentProcessId(pid) << ")" << std::endl;
    }
    std::cout << std::endl;
    */


    // ===== НАХОДИМ УЖЕ ЗАПУЩЕННЫЕ ГЛАВНЫЕ ПРОЦЕССЫ =====
    std::cout << "=== Main processes already running ===" << std::endl;

    // Сначала собираем все главные PID для каждого имени
    std::map<std::string, std::set<DWORD>> mainPidsByName;

    for (const auto& proc : processes) {
        for (const auto& [pid, name] : runningProcesses) {
            if (name == proc.name && IsMainProcess(pid, name)) {
                mainPidsByName[name].insert(pid);
                break;
            }
        }
    }

    // Теперь добавляем главные процессы в отслеживание
    for (const auto& [name, pids] : mainPidsByName) {
        for (DWORD pid : pids) {
            for (auto& proc : processes) {
                if (proc.name == name) {
                    auto startTime = GetProcessStartTime(pid);
                    processStartTimes[pid] = startTime;
                    processNames[pid] = name;
                    processPids[pid] = name;

                    std::cout << "[+] Main process already running: " << name
                        << " (PID: " << pid
                        << ", Total time: " << FormatProcessTime(proc.totalTime) << ")" << std::endl;
                    break;
                }
            }
        }
    }
    std::cout << std::endl;

    std::cout << "WMI was started. Waiting event..." << std::endl;

    IWbemClassObject* pEventObj = NULL;
    ULONG uReturn = 0;

    while (true) {
        //Для остановки службы 
        if (hStopEvent && WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
        // ============================================================
        // 1. ОБРАБОТКА ЗАПУСКА ПРОЦЕССА
        // ============================================================
        hr = pEnumeratorStart->Next(0, 1, &pEventObj, &uReturn);
        if (SUCCEEDED(hr) && uReturn > 0) {
            VARIANT vtInstance, vName, vPid;
            VariantInit(&vtInstance);
            VariantInit(&vName);
            VariantInit(&vPid);

            hr = pEventObj->Get(L"TargetInstance", 0, &vtInstance, 0, 0);
            if (SUCCEEDED(hr) && vtInstance.vt == VT_UNKNOWN) {
                IUnknown* pUnk = vtInstance.punkVal;
                IWbemClassObject* pProcessObj = NULL;
                hr = pUnk->QueryInterface(IID_IWbemClassObject, (LPVOID*)&pProcessObj);

                if (SUCCEEDED(hr) && pProcessObj) {
                    pProcessObj->Get(L"Name", 0, &vName, 0, 0);
                    pProcessObj->Get(L"ProcessId", 0, &vPid, 0, 0);

                    if (vName.vt == VT_BSTR && vName.bstrVal) {
                        std::string processName = BstrToUtf8(vName.bstrVal);
                        DWORD pid = 0;
                        if (vPid.vt == VT_UI4 || vPid.vt == VT_I4) {
                            pid = vPid.uintVal;
                        }

                        // Проверяем, есть ли такой процесс в списке для отслеживания
                        if (IsMainProcess(pid, processName)) {
                            for (auto& proc : processes) {
                                if (proc.name == processName) {
                                    // Запоминаем время запуска этого PID
                                    processStartTimes[pid] = std::chrono::steady_clock::now();
                                    processNames[pid] = processName;

                                    std::cout << "[+] Opened: " << processName
                                        << " (PID: " << pid
                                        << ", Time of Active: " << FormatProcessTime(proc.totalTime) << ")" << std::endl;

                                    // Сразу сохраняем в файл
                                    WriteFile();
                                    break;
                                }
                            }
                        }
                        
                    }
                    pProcessObj->Release();
                }
            }
            VariantClear(&vtInstance);
            VariantClear(&vName);
            VariantClear(&vPid);
            pEventObj->Release();
        }

        // ============================================================
        // 2. ОБРАБОТКА ЗАКРЫТИЯ ПРОЦЕССА
        // ============================================================
        if (pEnumeratorStop) {
            hr = pEnumeratorStop->Next(0, 1, &pEventObj, &uReturn);
            if (SUCCEEDED(hr) && uReturn > 0) {
                VARIANT vtInstance, vName, vPid;
                VariantInit(&vtInstance);
                VariantInit(&vName);
                VariantInit(&vPid);

                hr = pEventObj->Get(L"TargetInstance", 0, &vtInstance, 0, 0);
                if (SUCCEEDED(hr) && vtInstance.vt == VT_UNKNOWN) {
                    IUnknown* pUnk = vtInstance.punkVal;
                    IWbemClassObject* pProcessObj = NULL;
                    hr = pUnk->QueryInterface(IID_IWbemClassObject, (LPVOID*)&pProcessObj);

                    if (SUCCEEDED(hr) && pProcessObj) {
                        pProcessObj->Get(L"Name", 0, &vName, 0, 0);
                        pProcessObj->Get(L"ProcessId", 0, &vPid, 0, 0);

                        if (vName.vt == VT_BSTR && vName.bstrVal) {
                            std::string processName = BstrToUtf8(vName.bstrVal);
                            DWORD pid = 0;
                            if (vPid.vt == VT_UI4 || vPid.vt == VT_I4) {
                                pid = vPid.uintVal;
                            }


                            for (auto& proc : processes) {
                                if (proc.name == processName) {
                                        // Проверяем, есть ли этот PID в запущенных
                                        auto it = processStartTimes.find(pid);
                                        if (it != processStartTimes.end()) {
                                            // Считаем сколько работал
                                            auto duration = std::chrono::steady_clock::now() - it->second;
                                            float currentSessionTime = std::chrono::duration<float>(duration).count();

                                            // ===== ГЛАВНОЕ: ДОБАВЛЯЕМ К ОБЩЕМУ ВРЕМЕНИ =====
                                            proc.totalTime += currentSessionTime;

                                            std::cout << "[-] Closed: " << processName
                                                << " (PID: " << pid
                                                << ", Session: " << FormatProcessTime(currentSessionTime)
                                                << ", Time of Active: " << FormatProcessTime(proc.totalTime) << ")" << std::endl;

                                            // Удаляем из списка запущенных
                                            processStartTimes.erase(pid);
                                            processNames.erase(pid);

                                            // ===== СОХРАНЯЕМ В ФАЙЛ =====
                                            WriteFile();
                                        }
                                        break;
                                }
                            }
                            
                        }
                        pProcessObj->Release();
                    }
                }
                VariantClear(&vtInstance);
                VariantClear(&vName);
                VariantClear(&vPid);
                pEventObj->Release();
            }
        }

        // ============================================================
        // 3. КАЖДЫЕ 10 СЕКУНД СОХРАНЯЕМ ТЕКУЩЕЕ ВРЕМЯ
        // ============================================================
        static int saveCounter = 0;
        saveCounter++;
        if (saveCounter >= 10) {
            // Обновляем totalTime для запущенных процессов
            for (auto& proc : processes) {
                for (const auto& [pid, startTime] : processStartTimes) {
                    auto nameIt = processNames.find(pid);
                    if (nameIt != processNames.end() && nameIt->second == proc.name) {
                        auto duration = std::chrono::steady_clock::now() - startTime;
                        float currentSession = std::chrono::duration<float>(duration).count();

                        // Для сохранения: proc.totalTime уже содержит накопленное
                        // Мы НЕ меняем proc.totalTime здесь, только сохраняем
                        break;
                    }
                }
            }
            WriteFile();
            saveCounter = 0;
        }

        Sleep(1000);
    }

    pEnumeratorStart->Release();
    if (pEnumeratorStop) pEnumeratorStop->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
}