#define _WIN32_DCOM // Разрешает использование DCOM (необходимо для WMI)
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include "MainF.h"
#include <comdef.h>
#include <Wbemidl.h>
#include <fcntl.h>
#include <io.h>
#include <chrono>
#include <fstream>

MainF::MainF() {
    // Загружаем задачи из файла
    TaskData taskdata = ReadFile();
    Tasks = taskdata.tasks;
    Tasks_active = taskdata.activeProcesses;

    HRESULT hr;

    // 1. Инициализация COM
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Ошибка CoInitializeEx: 0x" << std::hex << hr << std::endl;
        return;
    }

    // 2. Настройка безопасности
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        std::cerr << "Ошибка CoInitializeSecurity: 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return;
    }

    // 3. Локатор
    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        std::cerr << "Ошибка CoCreateInstance: 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return;
    }

    // 4. Подключение к WMI
    IWbemServices* pSvc = NULL;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hr)) {
        std::cerr << "Ошибка ConnectServer: 0x" << std::hex << hr << std::endl;
        pLoc->Release();
        CoUninitialize();
        return;
    }

    // 5. Настройка безопасности для WMI
    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE);
    if (FAILED(hr)) {
        std::cerr << "Ошибка CoSetProxyBlanket: 0x" << std::hex << hr << std::endl;
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return;
    }

    // ===== ЗАПРОС НА СОЗДАНИЕ ПРОЦЕССОВ =====
    IEnumWbemClassObject* pEnumeratorStart = NULL;
    bstr_t wqlStart(L"SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'");
    hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlStart,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumeratorStart);
    if (FAILED(hr)) {
        std::cerr << "Ошибка ExecNotificationQuery (Creation): 0x" << std::hex << hr << std::endl;
        std::cerr << "Попытка использовать альтернативный запрос..." << std::endl;

        bstr_t wqlAlt(L"SELECT * FROM __InstanceCreationEvent WITHIN 2 WHERE TargetInstance ISA 'Win32_Process'");
        hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlAlt,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &pEnumeratorStart);
        if (FAILED(hr)) {
            std::cerr << "Ошибка ExecNotificationQuery (Alt): 0x" << std::hex << hr << std::endl;
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            return;
        }
    }

    // ===== ДОБАВЛЕНО: ЗАПРОС НА ЗАВЕРШЕНИЕ ПРОЦЕССОВ =====
    IEnumWbemClassObject* pEnumeratorStop = NULL;
    bstr_t wqlStop(L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'");
    hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlStop,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumeratorStop);
    if (FAILED(hr)) {
        std::cerr << "Ошибка ExecNotificationQuery (Deletion): 0x" << std::hex << hr << std::endl;
        // Не критично, продолжаем без отслеживания закрытия
        pEnumeratorStop = NULL;
    }

    std::cout << "WMI Мониторинг запущен. Ожидание событий..." << std::endl;
    std::cout << "Отслеживаемые процессы:" << std::endl;
    for (const auto& task : Tasks) {
        std::cout << "  - " << task << std::endl;
    }
    std::cout << std::endl;

    // ===== ЦИКЛ ОБРАБОТКИ СОБЫТИЙ =====
    IWbemClassObject* pEventObj = NULL;
    ULONG uReturn = 0;

    while (true) {
        // ===== ОБРАБОТКА СОЗДАНИЯ ПРОЦЕССОВ =====
        hr = pEnumeratorStart->Next(0, 1, &pEventObj, &uReturn);
        if (SUCCEEDED(hr) && uReturn > 0) {
            VARIANT vtInstance;
            VariantInit(&vtInstance);
            VARIANT vName;
            VariantInit(&vName);
            VARIANT vPid;
            VariantInit(&vPid);

            hr = pEventObj->Get(L"TargetInstance", 0, &vtInstance, 0, 0);
            if (SUCCEEDED(hr) && vtInstance.vt == VT_UNKNOWN) {
                IUnknown* pUnk = vtInstance.punkVal;
                IWbemClassObject* pProcessObj = NULL;
                hr = pUnk->QueryInterface(IID_IWbemClassObject, (LPVOID*)&pProcessObj);

                if (SUCCEEDED(hr) && pProcessObj) {
                    hr = pProcessObj->Get(L"Name", 0, &vName, 0, 0);
                    hr = pProcessObj->Get(L"ProcessId", 0, &vPid, 0, 0);

                    if (vName.vt == VT_BSTR && vName.bstrVal) {
                        std::string processName = BstrToUtf8(vName.bstrVal);
                        DWORD pid = 0;
                        if (vPid.vt == VT_UI4 || vPid.vt == VT_I4) {
                            pid = vPid.uintVal;
                        }

                        std::cout << "[ + ЗАПУЩЕН ] " << processName << " (PID: " << pid << ")" << std::endl;
                        CheckNameProcess(processName, pid, 0);
                    }

                    pProcessObj->Release();
                }
            }

            VariantClear(&vtInstance);
            VariantClear(&vName);
            VariantClear(&vPid);
            pEventObj->Release();
        }

        // ===== ДОБАВЛЕНО: ОБРАБОТКА ЗАВЕРШЕНИЯ ПРОЦЕССОВ =====
        if (pEnumeratorStop) {
            hr = pEnumeratorStop->Next(0, 1, &pEventObj, &uReturn);
            if (SUCCEEDED(hr) && uReturn > 0) {
                VARIANT vtInstance;
                VariantInit(&vtInstance);
                VARIANT vName;
                VariantInit(&vName);
                VARIANT vPid;
                VariantInit(&vPid);

                hr = pEventObj->Get(L"TargetInstance", 0, &vtInstance, 0, 0);
                if (SUCCEEDED(hr) && vtInstance.vt == VT_UNKNOWN) {
                    IUnknown* pUnk = vtInstance.punkVal;
                    IWbemClassObject* pProcessObj = NULL;
                    hr = pUnk->QueryInterface(IID_IWbemClassObject, (LPVOID*)&pProcessObj);

                    if (SUCCEEDED(hr) && pProcessObj) {
                        hr = pProcessObj->Get(L"Name", 0, &vName, 0, 0);
                        hr = pProcessObj->Get(L"ProcessId", 0, &vPid, 0, 0);

                        if (vName.vt == VT_BSTR && vName.bstrVal) {
                            std::string processName = BstrToUtf8(vName.bstrVal);
                            DWORD pid = 0;
                            if (vPid.vt == VT_UI4 || vPid.vt == VT_I4) {
                                pid = vPid.uintVal;
                            }

                            std::cout << "[ - ЗАКРЫТ ] " << processName << " (PID: " << pid << ")" << std::endl;
                            CheckNameProcess(processName, pid, 1);
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

        Sleep(100); // Небольшая задержка
    }

    // Очистка
    pEnumeratorStart->Release();
    if (pEnumeratorStop) pEnumeratorStop->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
}

std::string MainF::BstrToUtf8(BSTR bstr) {
    if (!bstr) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void MainF::CheckNameProcess(std::string Name, DWORD id, int type) {
    auto startTime = std::chrono::steady_clock::now();
    if (type == 0) {
        bool found = false;
        if (std::find(Tasks.begin(), Tasks.end(), Name) != Tasks.end()){
            for (auto it = Tasks_active.begin(); it != Tasks_active.end(); ++it) {
                if (it->pid == id) {
                    found = true;
                }
            }
            if (found != true) {
                Tasks_active.push_back({ id, Name, startTime });
            }
        }
    }
    else if (type == 1) {
        for (auto it = Tasks_active.begin(); it != Tasks_active.end(); ++it) {
            if (it->pid == id) {
                // Вычисляем разницу во времени (в секундах)
                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - it->startTime).count();

                std::cout << "[ - ЗАКРЫТ  ] " << it->name
                        << " (PID: " << it->pid << ") работал: "
                        << duration << " сек." << std::endl;
                WriteFile();
                // Удаляем из вектора активных задач
                Tasks_active.erase(it);
                break;
            }
        }
    }
    
}
std::string MainF::FormatDuration(const std::chrono::steady_clock::time_point& startTime) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now - startTime;

    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

    std::ostringstream oss;
    if (hours.count() > 0) {
        oss << hours.count() << "ч ";
    }
    if (minutes.count() > 0 || hours.count() > 0) {
        oss << minutes.count() << "м ";
    }
    oss << seconds.count() << "с";

    return oss.str();
}

// Получение пути к исполняемому файлу
std::string MainF::GetExePath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t pos = path.find_last_of("\\/");
    return path.substr(0, pos + 1);
}

void MainF::WriteFile() {
    std::string filePath = GetExePath() + "tasks.txt";
    std::ofstream file(filePath);

    if (!file.is_open()) {
        file.open("tasks.txt");
        if (!file.is_open()) {
            return;
        }
    }

    // ===== ИСПРАВЛЕНО: Правильное получение времени =====
    time_t now_time = time(nullptr);  // Получаем текущее время
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_time);  // Безопасная версия

    // Заголовок
    file << "=== Список задач и активных процессов ===" << std::endl;
    file << "Дата: " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    file << std::endl;

    // Записываем все задачи из Tasks
    file << "--- Задачи (Tasks) ---" << std::endl;
    if (Tasks.empty()) {
        file << "Нет задач" << std::endl;
    }
    else {
        for (size_t i = 0; i < Tasks.size(); ++i) {
            file << i + 1 << ". " << Tasks[i] << std::endl;
        }
    }
    file << std::endl;

    // Записываем активные процессы с временем работы
    file << "--- Активные процессы (Tasks_active) ---" << std::endl;
    if (Tasks_active.empty()) {
        file << "Нет активных процессов" << std::endl;
    }
    else {
        for (size_t i = 0; i < Tasks_active.size(); ++i) {
            const auto& process = Tasks_active[i];
            file << i + 1 << ". " << process.name;

            auto now = std::chrono::steady_clock::now();
            auto duration = now - process.startTime;

            auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
            duration -= hours;
            auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
            duration -= minutes;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

            file << " (PID: " << process.pid << ", Время работы: ";
            if (hours.count() > 0) {
                file << hours.count() << "ч ";
            }
            if (minutes.count() > 0 || hours.count() > 0) {
                file << minutes.count() << "м ";
            }
            file << seconds.count() << "с)";

            file << std::endl;
        }
    }
    file << std::endl;

    file << "=== Конец файла ===" << std::endl;
    file.close();
}


MainF::TaskData MainF::ReadFile() {
    TaskData result;
    std::string filePath = GetExePath() + "tasks.txt";
    std::ifstream file(filePath);

    if (!file.is_open()) {
        file.open("tasks.txt");
        if (!file.is_open()) {
            WriteFile();
            return result;
        }
    }

    std::string line;
    enum class Section { None, Tasks, Active } currentSection = Section::None;

    while (std::getline(file, line)) {
        // Убираем пробелы в начале и конце
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue; // Пустая строка
        line = line.substr(start);

        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            line = line.substr(0, end + 1);
        }

        // Определяем секцию
        if (line.find("--- Задачи (Tasks) ---") != std::string::npos ||
            line.find("ЗАДАЧИ (TASKS)") != std::string::npos ||
            line.find("=== Задачи (Tasks) ===") != std::string::npos) {
            currentSection = Section::Tasks;
            continue;
        }

        if (line.find("--- Активные процессы (Tasks_active) ---") != std::string::npos ||
            line.find("АКТИВНЫЕ ПРОЦЕССЫ") != std::string::npos ||
            line.find("=== Активные процессы (Tasks_active) ===") != std::string::npos) {
            currentSection = Section::Active;
            continue;
        }

        // Пропускаем заголовки и разделители
        if (line.find("===") != std::string::npos ||
            line.find("╔") != std::string::npos ||
            line.find("║") != std::string::npos ||
            line.find("╚") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos ||
            line.find("│") != std::string::npos ||
            line.find("Нет задач") != std::string::npos ||
            line.find("Нет активных") != std::string::npos ||
            line.find("Список задач") != std::string::npos ||
            line.find("Дата:") != std::string::npos ||
            line.find("Конец файла") != std::string::npos) {
            continue;
        }

        switch (currentSection) {
        case Section::Tasks: {
            // Парсим задачи
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos && dotPos < 10) {
                std::string taskName = line.substr(dotPos + 1);
                // Убираем пробелы в начале и конце
                size_t s = taskName.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    taskName = taskName.substr(s);
                    size_t e = taskName.find_last_not_of(" \t");
                    if (e != std::string::npos) {
                        taskName = taskName.substr(0, e + 1);
                    }
                    if (!taskName.empty()) {
                        result.tasks.push_back(taskName);
                    }
                }
            }
            break;
        }

        case Section::Active: {
            // Парсим активные процессы
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos && dotPos < 10) {
                // Извлекаем имя процесса
                size_t nameStart = dotPos + 1;
                size_t nameEnd = line.find('(', nameStart);
                if (nameEnd != std::string::npos) {
                    std::string processName = line.substr(nameStart, nameEnd - nameStart);
                    size_t s = processName.find_first_not_of(" \t");
                    if (s != std::string::npos) {
                        processName = processName.substr(s);
                        size_t e = processName.find_last_not_of(" \t");
                        if (e != std::string::npos) {
                            processName = processName.substr(0, e + 1);
                        }
                    }

                    // Извлекаем PID
                    size_t pidStart = line.find("PID:", nameEnd);
                    if (pidStart == std::string::npos) {
                        pidStart = line.find("PID:", 0);
                    }
                    if (pidStart != std::string::npos) {
                        pidStart += 4; // пропускаем "PID:"
                        size_t pidEnd = line.find(',', pidStart);
                        if (pidEnd == std::string::npos) {
                            pidEnd = line.find(')', pidStart);
                        }
                        if (pidEnd != std::string::npos) {
                            std::string pidStr = line.substr(pidStart, pidEnd - pidStart);
                            // Убираем пробелы
                            size_t ps = pidStr.find_first_not_of(" \t");
                            if (ps != std::string::npos) {
                                pidStr = pidStr.substr(ps);
                                size_t pe = pidStr.find_last_not_of(" \t");
                                if (pe != std::string::npos) {
                                    pidStr = pidStr.substr(0, pe + 1);
                                }
                            }
                            try {
                                DWORD pid = std::stoul(pidStr);
                                Process proc;
                                proc.pid = pid;
                                proc.name = processName;
                                proc.startTime = std::chrono::steady_clock::now();
                                result.activeProcesses.push_back(proc);
                            }
                            catch (...) {
                                // Игнорируем ошибки парсинга PID
                            }
                        }
                    }
                }
            }
            break;
        }

        case Section::None:
        default:
            break;
        }
    }

    file.close();
    return result;
}