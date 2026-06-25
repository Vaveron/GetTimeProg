#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "MainF.h"

#pragma comment(lib, "advapi32.lib")

// ===== ИСПРАВЛЕНО: Используем массив для избежания проблем с const =====
wchar_t g_szServiceName[] = L"GetTimeProg";           // Внутреннее имя
wchar_t g_szServiceDisplayName[] = L"GetTime Service"; // Отображаемое имя

SERVICE_STATUS          gSvcStatus = { 0 };
SERVICE_STATUS_HANDLE   gSvcStatusHandle = NULL;
HANDLE                  ghSvcStopEvent = NULL;

// --- Прототипы функций ---
VOID WINAPI SvcMain(DWORD, LPTSTR*);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInstall();
VOID SvcUninstall();

// --- Функция для записи логов (добавлена) ---
void WriteLog(const char* format, ...) {
    // Создаём папку для логов, если её нет
    CreateDirectoryW(L"C:\\ProgramData\\GetTimeProg", NULL);

    wchar_t logPath[MAX_PATH];
    wsprintfW(logPath, L"C:\\ProgramData\\GetTimeProg\\service.log");

    HANDLE hFile = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, NULL, FILE_END);

        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // Добавляем время
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuffer[128];
        sprintf_s(timeBuffer, sizeof(timeBuffer), "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buffer);

        DWORD bytesWritten;
        WriteFile(hFile, timeBuffer, (DWORD)strlen(timeBuffer), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

// --- Точка входа ---
int __cdecl _tmain(int argc, TCHAR* argv[])
{
    // Если передан параметр "install", устанавливаем службу.
    if (argc > 1 && lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        SvcInstall();
        return 0;
    }
    // Если передан параметр "uninstall", удаляем службу.
    if (argc > 1 && lstrcmpi(argv[1], TEXT("uninstall")) == 0)
    {
        SvcUninstall();
        return 0;
    }
    if (argc > 1 && lstrcmpi(argv[1], TEXT("debug")) == 0)
    {
        WriteLog("=== ЗАПУСК В РЕЖИМЕ ОТЛАДКИ ===");
        SvcInit(argc, argv);
        return 0;
    }

    // Если параметров нет, запускаемся как служба.
    // ===== ИСПРАВЛЕНО: Приводим типы =====
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { g_szServiceName, (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    // Эта функция связывает нашу программу с SCM.
    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        DWORD dwError = GetLastError();
        WriteLog("ОШИБКА StartServiceCtrlDispatcher: %d", dwError);
        return dwError;
    }
    return 0;
}

// --- Основная функция службы ---
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    WriteLog("SvcMain: Запуск");

    // ===== ИСПРАВЛЕНО: Регистрируем обработчик =====
    gSvcStatusHandle = RegisterServiceCtrlHandler(g_szServiceName, SvcCtrlHandler);
    if (!gSvcStatusHandle)
    {
        WriteLog("SvcMain: Ошибка RegisterServiceCtrlHandler: %d", GetLastError());
        return;
    }

    // Сообщаем SCM, что служба запускается.
    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Выполняем инициализацию службы.
    SvcInit(dwArgc, lpszArgv);
}

// --- Инициализация и основная логика службы ---
VOID SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
    WriteLog("SvcInit: Начало инициализации");

    // Создаём событие для сигнала остановки.
    ghSvcStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ghSvcStopEvent == NULL)
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcInit: Ошибка CreateEvent: %d", dwError);
        ReportSvcStatus(SERVICE_STOPPED, dwError, 0);
        return;
    }

    // Сообщаем, что служба успешно запущена.
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    WriteLog("SvcInit: Служба запущена успешно");

    // *** ЗДЕСЬ РАЗМЕЩАЕТСЯ ОСНОВНАЯ ЛОГИКА ВАШЕЙ ПРОГРАММЫ ***
    try {
        // Создаём экземпляр вашего класса
        MainF mainf;

        WriteLog("SvcInit: Запуск MainF");

        WriteLog("SvcInit: Получен сигнал остановки");
    }
    catch (...) {
        WriteLog("SvcInit: Исключение в основной логике");
    }

    // Сигнал остановки получен, завершаем работу.
    WriteLog("SvcInit: Завершение работы");
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

// --- Обработчик команд от SCM ---
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    WriteLog("SvcCtrlHandler: Получена команда %d", dwCtrl);

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        WriteLog("SvcCtrlHandler: Команда STOP");
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(ghSvcStopEvent);
        // Не вызываем ReportSvcStatus здесь - SvcInit сам сообщит о остановке
        break;

    case SERVICE_CONTROL_INTERROGATE:
        WriteLog("SvcCtrlHandler: Команда INTERROGATE");
        break;

    case SERVICE_CONTROL_PAUSE:
        WriteLog("SvcCtrlHandler: Команда PAUSE");
        // Здесь можно приостановить работу
        break;

    case SERVICE_CONTROL_CONTINUE:
        WriteLog("SvcCtrlHandler: Команда CONTINUE");
        // Здесь можно возобновить работу
        break;

    default:
        WriteLog("SvcCtrlHandler: Неизвестная команда %d", dwCtrl);
        break;
    }
}

// --- УСТАНОВКА СЛУЖБЫ (ДОБАВЛЕНА) ---
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    wchar_t szPath[MAX_PATH];

    WriteLog("SvcInstall: Начало установки");

    // Получаем полный путь к .exe файлу
    if (!GetModuleFileNameW(NULL, szPath, MAX_PATH))
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcInstall: Ошибка GetModuleFileName: %d", dwError);
        printf("Не удалось получить путь к исполняемому файлу (%d)\n", dwError);
        return;
    }
    WriteLog("SvcInstall: Путь к файлу: %S", szPath);

    // Открываем диспетчер управления службами
    schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (NULL == schSCManager)
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcInstall: Ошибка OpenSCManager: %d", dwError);
        printf("OpenSCManager не удался (%d)\n", dwError);
        return;
    }

    // Создаём службу
    schService = CreateServiceW(
        schSCManager,              // Дескриптор SCM
        g_szServiceName,           // Внутреннее имя службы
        g_szServiceDisplayName,    // Отображаемое имя
        SERVICE_ALL_ACCESS,        // Права доступа
        SERVICE_WIN32_OWN_PROCESS, // Тип службы (свой процесс)
        SERVICE_AUTO_START,        // ТИП ЗАПУСКА: АВТОМАТИЧЕСКИЙ
        SERVICE_ERROR_NORMAL,      // Тип контроля ошибок
        szPath,                    // Путь к .exe
        NULL,                      // Группа загрузки
        NULL,                      // Тег
        NULL,                      // Зависимости
        NULL,                      // Учётная запись (LocalSystem)
        NULL);                     // Пароль

    if (schService == NULL)
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcInstall: Ошибка CreateService: %d", dwError);
        printf("CreateService не удался (%d)\n", dwError);

        if (dwError == ERROR_SERVICE_EXISTS) {
            printf("Служба уже существует!\n");
        }
    }
    else
    {
        printf("Служба успешно установлена!\n");
        WriteLog("SvcInstall: Служба успешно установлена");

        // ===== ОПЦИОНАЛЬНО: Включить отложенный запуск =====
        // Для отложенного запуска раскомментируйте:
        /*
        HKEY hKey;
        wchar_t keyPath[256];
        wsprintfW(keyPath, L"SYSTEM\\CurrentControlSet\\Services\\%s", g_szServiceName);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
            DWORD delayedStart = 1;
            RegSetValueExW(hKey, L"DelayedAutoStart", 0, REG_DWORD,
                          (const BYTE*)&delayedStart, sizeof(DWORD));
            RegCloseKey(hKey);
            printf("Включен отложенный автозапуск\n");
        }
        */

        CloseServiceHandle(schService);
    }

    CloseServiceHandle(schSCManager);
    WriteLog("SvcInstall: Завершение установки");
}

// --- УДАЛЕНИЕ СЛУЖБЫ (ДОБАВЛЕНА) ---
VOID SvcUninstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    WriteLog("SvcUninstall: Начало удаления");

    // Открываем диспетчер управления службами
    schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager)
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcUninstall: Ошибка OpenSCManager: %d", dwError);
        printf("OpenSCManager не удался (%d)\n", dwError);
        return;
    }

    // Открываем службу
    schService = OpenServiceW(schSCManager, g_szServiceName, SERVICE_ALL_ACCESS);
    if (schService == NULL)
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcUninstall: Ошибка OpenService: %d", dwError);
        printf("OpenService не удался (%d)\n", dwError);

        if (dwError == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("Служба не найдена!\n");
        }

        CloseServiceHandle(schSCManager);
        return;
    }

    // Останавливаем службу, если она запущена
    SERVICE_STATUS svcStatus;
    if (ControlService(schService, SERVICE_CONTROL_STOP, &svcStatus))
    {
        WriteLog("SvcUninstall: Служба остановлена");
        printf("Служба остановлена\n");
        Sleep(2000); // Даём время на остановку
    }
    else
    {
        WriteLog("SvcUninstall: Служба уже остановлена или недоступна");
    }

    // Удаляем службу
    if (!DeleteService(schService))
    {
        DWORD dwError = GetLastError();
        WriteLog("SvcUninstall: Ошибка DeleteService: %d", dwError);
        printf("DeleteService не удался (%d)\n", dwError);
    }
    else
    {
        WriteLog("SvcUninstall: Служба успешно удалена");
        printf("Служба успешно удалена!\n");
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    WriteLog("SvcUninstall: Завершение удаления");
}

// --- Вспомогательная функция для отправки статуса в SCM ---
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else
        gSvcStatus.dwCheckPoint = dwCheckPoint++;

    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}