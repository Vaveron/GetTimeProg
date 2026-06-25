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

MainF::MainF() {
    HRESULT hr;

    // 2. Инициализация COM
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr));

    // 3. Настройка безопасности
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hr)) { CoUninitialize(); }

    // 4. Локатор
    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) { CoUninitialize(); }

    // 5. Подключение к WMI
    IWbemServices* pSvc = NULL;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hr)) { pLoc->Release(); CoUninitialize(); }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hr)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); }

    // 6. Запрос событий
    IEnumWbemClassObject* pEnumerator = NULL;
    bstr_t wqlQuery(L"SELECT * FROM __InstanceOperationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'");
    hr = pSvc->ExecNotificationQuery(_bstr_t("WQL"), wqlQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    if (FAILED(hr)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); }

    std::cout << "WMI Мониторинг запущен на UTF-8. Русский язык активен!" << std::endl;

    IWbemClassObject* pEventObj = NULL;
    ULONG uReturn = 0;

    // 7. Цикл обработки событий
    while (true) {
        hr = pEnumerator->Next(WBEM_INFINITE, 1, &pEventObj, &uReturn);

        if (SUCCEEDED(hr) && uReturn > 0) {
            VARIANT vtProp;

            pEventObj->Get(L"__Class", 0, &vtProp, 0, 0);
            std::wstring eventType = vtProp.bstrVal;
            VariantClear(&vtProp);

            pEventObj->Get(L"TargetInstance", 0, &vtProp, 0, 0);
            IUnknown* pUnk = vtProp.punkVal;
            IWbemClassObject* pProcessObj = NULL;
            pUnk->QueryInterface(IID_IWbemClassObject, (LPVOID*)&pProcessObj);
            VariantClear(&vtProp);

            VARIANT vName, vPid;
            pProcessObj->Get(L"Name", 0, &vName, 0, 0);
            pProcessObj->Get(L"ProcessId", 0, &vPid, 0, 0);

            // Конвертируем имя процесса из широких символов в UTF-8 string
            std::string processName = BstrToUtf8(vName.bstrVal);

            // Выводим через обычный std::cout (так как теперь вся консоль в UTF-8)
            if (eventType == L"__InstanceCreationEvent") {
                std::cout << "[ + ОТКРЫТ ] Имя: " << processName << " | PID: " << vPid.uintVal << std::endl;
                CheckNameProcess(processName, vPid.uintVal, 0);
            }
            else if (eventType == L"__InstanceDeletionEvent") {
                std::cout << "[ - ЗАКРЫТ ] Имя: " << processName << " | PID: " << vPid.uintVal << std::endl;
                CheckNameProcess(processName, vPid.uintVal, 1);
            }

            VariantClear(&vName);
            VariantClear(&vPid);
            pProcessObj->Release();
            pEventObj->Release();
        }
    }
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
                // Удаляем из вектора активных задач
                Tasks_active.erase(it);
                break;
            }
        }
    }
    
}
