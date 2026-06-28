#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "MainF.h"

#pragma comment(lib, "advapi32.lib")
// Добавь в начало main.cpp (после #include)
#ifdef NDEBUG
    #pragma comment(lib, "wbemuuid.lib")
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "oleaut32.lib")
#endif

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
/* ВОТ НАХУЙ ЕЕ ОНА МНЕ ОШИБКУ ДАЕТ ПРИ УСТАНОВКИ СЛУЖБЫ
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
*/
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
        //WriteLog("=== ЗАПУСК В РЕЖИМЕ ОТЛАДКИ ===");
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
        //WriteLog("ОШИБКА StartServiceCtrlDispatcher: %d", dwError);
        return dwError;
    }
    return 0;
}

// --- Основная функция службы ---
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    //WriteLog("SvcMain: Запуск");

    // ===== ИСПРАВЛЕНО: Регистрируем обработчик =====
    gSvcStatusHandle = RegisterServiceCtrlHandler(g_szServiceName, SvcCtrlHandler);
    if (!gSvcStatusHandle)
    {
        //WriteLog("SvcMain: Ошибка RegisterServiceCtrlHandler: %d", GetLastError());
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
    //WriteLog("SvcInit: Начало инициализации");

    // Создаём событие для сигнала остановки.
    ghSvcStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ghSvcStopEvent == NULL)
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcInit: Ошибка CreateEvent: %d", dwError);
        ReportSvcStatus(SERVICE_STOPPED, dwError, 0);
        return;
    }

    // Сообщаем, что служба успешно запущена.
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    //WriteLog("SvcInit: Служба запущена успешно");

    // *** ЗДЕСЬ РАЗМЕЩАЕТСЯ ОСНОВНАЯ ЛОГИКА ВАШЕЙ ПРОГРАММЫ ***
    try {
        // Создаём экземпляр вашего класса
        MainF mainf(ghSvcStopEvent);
        WaitForSingleObject(ghSvcStopEvent, INFINITE);

        //WriteLog("SvcInit: Запуск MainF");
        //WriteLog("SvcInit: Получен сигнал остановки");
    }
    catch (...) {
        //WriteLog("SvcInit: Исключение в основной логике");
    }
    // Закрываем событие
    if (ghSvcStopEvent) {
        CloseHandle(ghSvcStopEvent);
        ghSvcStopEvent = NULL;
    }
    // Сигнал остановки получен, завершаем работу.
    //WriteLog("SvcInit: Завершение работы");
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}
// ===== ПРОВЕРКА ПРАВ АДМИНИСТРАТОРА =====
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin != FALSE;
}

// --- Обработчик команд от SCM ---
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    //WriteLog("SvcCtrlHandler: Получена команда %d", dwCtrl);

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        // ===== СИГНАЛИМ О ОСТАНОВКЕ =====
        if (ghSvcStopEvent) {
            SetEvent(ghSvcStopEvent);
        }
        break;

    case SERVICE_CONTROL_INTERROGATE:
        //WriteLog("SvcCtrlHandler: Команда INTERROGATE");
        break;

    case SERVICE_CONTROL_PAUSE:
        //WriteLog("SvcCtrlHandler: Команда PAUSE");
        // Здесь можно приостановить работу
        break;

    case SERVICE_CONTROL_CONTINUE:
        //WriteLog("SvcCtrlHandler: Команда CONTINUE");
        // Здесь можно возобновить работу
        break;

    default:
        //WriteLog("SvcCtrlHandler: Неизвестная команда %d", dwCtrl);
        break;
    }
}

VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    wchar_t szPath[MAX_PATH];

    //WriteLog("SvcInstall: Начало установки");

    // Проверяем права
    if (!IsRunningAsAdmin()) {
        printf("Error: Administrator rights required!\n");
        printf("Run the program as Administrator.\n");
        return;
    }

    // Получаем полный путь к .exe файлу
    if (!GetModuleFileNameW(NULL, szPath, MAX_PATH))
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcInstall: Ошибка GetModuleFileName: %d", dwError);
        printf("Не удалось получить путь к исполняемому файлу (%d)\n", dwError);
        return;
    }
    //WriteLog("SvcInstall: Путь к файлу: %S", szPath);

    // Открываем диспетчер управления службами
    schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (NULL == schSCManager)
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcInstall: Ошибка OpenSCManager: %d", dwError);

        // ===== ПОДРОБНЫЕ СООБЩЕНИЯ ОБ ОШИБКАХ =====
        switch (dwError) {
        case ERROR_ACCESS_DENIED:  // 5
            printf("Error: You dont go forward (type 5)\n");
            printf("You arent Administrator\n");
            break;
        case ERROR_NOT_ENOUGH_MEMORY:  // 8
            printf("Error: Memory small\n");
            break;
        case ERROR_INVALID_HANDLE:  // 6
            printf("Error: Bad descriptor\n");
            break;
        default:
            printf("OpenSCManager error(%d)\n", dwError);
            break;
        }
        return;
    }

    // Создаём службу
    schService = CreateServiceW(
        schSCManager,
        g_szServiceName,
        g_szServiceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        szPath,
        NULL, NULL, NULL,
        NULL,  // LocalSystem
        NULL);

    if (schService == NULL)
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcInstall: Ошибка CreateService: %d", dwError);

        switch (dwError) {
        case ERROR_SERVICE_EXISTS:  // 1073
            printf("Service already exists!\n");
            printf("To reinstall, first uninstall it: %S uninstall\n", g_szServiceName);
            break;
        case ERROR_ACCESS_DENIED:  // 5
            printf("ERROR: Access denied when creating service (code 5)\n");
            printf("Run this program as Administrator!\n");
            break;
        case ERROR_PATH_NOT_FOUND:  // 3
            printf("ERROR: File path not found\n");
            printf("Make sure the executable exists at the specified path.\n");
            break;
        default:
            printf("CreateService failed (%d)\n", dwError);
            break;
        }
    }
    else
    {
        printf("Service was installed\n");
        printf("Service name: %S\n", g_szServiceName);
        printf("\nFor start Service:\n");
        printf("  net start %S\n", g_szServiceName);
        printf("\nfor stop Service:\n");
        printf("  net stop %S\n", g_szServiceName);
        //WriteLog("SvcInstall: Служба успешно установлена");

        CloseServiceHandle(schService);
    }

    CloseServiceHandle(schSCManager);
    //WriteLog("SvcInstall: Завершение установки");
}

VOID SvcUninstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    //WriteLog("SvcUninstall: Начало удаления");

    // Проверяем права
    if (!IsRunningAsAdmin()) {
        printf("Error: Administrator rights required!\n");
        return;
    }

    schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager)
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcUninstall: Ошибка OpenSCManager: %d", dwError);
        printf("OpenSCManager не удался (%d)\n", dwError);
        return;
    }

    schService = OpenServiceW(schSCManager, g_szServiceName, SERVICE_ALL_ACCESS);
    if (schService == NULL)
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcUninstall: Ошибка OpenService: %d", dwError);

        if (dwError == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("Service %S wasnt installed!\n", g_szServiceName);
        }
        else {
            printf("OpenService не удался (%d)\n", dwError);
        }

        CloseServiceHandle(schSCManager);
        return;
    }

    // Останавливаем службу
    SERVICE_STATUS svcStatus;
    if (ControlService(schService, SERVICE_CONTROL_STOP, &svcStatus))
    {
        //WriteLog("SvcUninstall: Служба остановлена");
        printf("Служба остановлена\n");
        Sleep(2000);
    }
    else
    {
        //WriteLog("SvcUninstall: Служба уже остановлена или недоступна");
    }

    // Удаляем службу
    if (!DeleteService(schService))
    {
        DWORD dwError = GetLastError();
        //WriteLog("SvcUninstall: Ошибка DeleteService: %d", dwError);
        printf("DeleteService не удался (%d)\n", dwError);
    }
    else
    {
        //WriteLog("SvcUninstall: Служба успешно удалена");
        printf("Service %S is deleted sucsessfull\n", g_szServiceName);
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    //WriteLog("SvcUninstall: Завершение удаления");
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