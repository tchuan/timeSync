#pragma region Includes and Manifest Dependencies

#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <windows.h>
#include <windowsx.h>
#include <shlObj.h>
#include <tchar.h>
#include <strsafe.h>
#include <time.h>

#include "Resource.h"
#include "NTPSupport.h"
#include "UACSupport.h"

#if defined _M_IX86
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#pragma endregion

HINSTANCE g_hInstance;
const wchar_t* g_AppName = L"tTimeSync";
bool g_IsSyncing = false;
HANDLE g_ThreadID = INVALID_HANDLE_VALUE;
NOTIFYICONDATA g_NID;

#define UPDATE_TIME_EVENT 1
#define SYNC_TIME_EVENT 2
#define WM_SHOWTASK WM_USER + 1

SOCKET g_socket;
time_t g_NTPTime = 0;
float  g_Splitseconds = 0.0;
bool   g_bSyncTime = false;
bool   g_bSyncSucceed = false;

void SyncLocalTime(wchar_t* time_string, size_t length, bool bSetLocalTime)
{
    SYSTEMTIME newtime;
    struct tm LocalTime;
    localtime_s(&LocalTime, &g_NTPTime);

    // 获取新的时间
    newtime.wYear      = LocalTime.tm_year + 1900;
    newtime.wMonth     = LocalTime.tm_mon + 1;
    newtime.wDayOfWeek = LocalTime.tm_wday;
    newtime.wDay       = LocalTime.tm_mday;
    newtime.wHour      = LocalTime.tm_hour;
    newtime.wMinute    = LocalTime.tm_min;
    newtime.wSecond    = LocalTime.tm_sec;
    newtime.wMilliseconds = (unsigned short)g_Splitseconds;

    if (bSetLocalTime)
    {
        SetLocalTime(&newtime);
    }

    StringCchPrintf(time_string, length, L"%04d-%02d-%02d %02d:%02d:%02d:%04d",
                    newtime.wYear,
                    newtime.wMonth,
                    newtime.wDay,
                    newtime.wHour,
                    newtime.wMinute,
                    newtime.wSecond,
                    newtime.wMilliseconds);
}

DWORD CALLBACK GetNTPTime(LPVOID lpParameter)
{
    HWND hWnd = (HWND)lpParameter;
    WORD wVersionRequested;
    WSADATA wsaData;
    NTP_Packet NTP_Send,NTP_Recv; 

    // 初始化版本
    wVersionRequested = MAKEWORD(2, 2);
    if (0 != WSAStartup(wVersionRequested, &wsaData)) 
    {
        WSACleanup();
        return FALSE;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) 
    {
        WSACleanup();
        return FALSE; 
    }

    try
    {
        SetWindowText(GetDlgItem(hWnd, IDC_STATIC_NETTIME), L"Checking");

        int SerNums = sizeof(g_NTPServers)/sizeof(g_NTPServers[0]);
        for (int i = 0; i < SerNums; ++i)
        {
            SetWindowTextA(GetDlgItem(hWnd, IDC_STATIC_NETHOST), g_NTPServers[i]);

            HOSTENT* host = gethostbyname( g_NTPServers[i] );
            if (host == NULL)
            {
                continue;
            }

            g_socket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
            sockaddr_in addrSrv;
            addrSrv.sin_addr.S_un.S_addr = inet_addr(inet_ntoa( *(IN_ADDR*)host->h_addr));
            addrSrv.sin_family = AF_INET;
            addrSrv.sin_port = htons(123);

            //设置超时时间
            int timeout = 1000;
            setsockopt(g_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

            DWORD dwError = SOCKET_ERROR;
            if (SOCKET_ERROR == sendto(g_socket, (const char*)&NTP_Send, sizeof(NTP_Send), 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv)))
            {
                throw dwError;
            }
            int sockaddr_Size = sizeof(addrSrv);
            if (SOCKET_ERROR == recvfrom(g_socket, (char*)&NTP_Recv, sizeof(NTP_Recv), 0, (struct sockaddr*)&addrSrv, &sockaddr_Size))
            {
                if (WSAETIMEDOUT == WSAGetLastError())
                {
                    continue;
                }
                throw dwError;
            }
            closesocket(g_socket);
            WSACleanup();

            // 获取时间服务器的时间
            g_NTPTime = ntohl(NTP_Recv.transmit_timestamp_seconds)-2208988800;

            // 设置时间精度
            g_Splitseconds = 1000.0f * 0.000000000200f * (float)ntohl(NTP_Recv.transmit_timestamp_fractions);

            g_bSyncSucceed = true;
            break;
        }
    }
    catch (DWORD dwError)
    {
        closesocket(g_socket);
        WSACleanup();
        ::SetWindowText(::GetDlgItem(hWnd, IDC_STATIC_NETTIME), _T("Get NTPtime Failed"));
    }

    if (g_bSyncSucceed && g_bSyncTime)
    {
        wchar_t time[36];
        SyncLocalTime(time, 36, true);
        SetWindowText(GetDlgItem(hWnd, IDC_STATIC_NETTIME), time);
    }

    return TRUE;
}

BOOL OnInitDialog(HWND hWnd, HWND hwndFocus, LPARAM lParam)
{
    OSVERSIONINFO osver = { sizeof(osver) };
    if (GetVersionEx(&osver) && osver.dwMajorVersion >= 6)
    {// Running Windows Vista or later (major version >= 6).
        try
        {// Get and display the process elevation information.
            BOOL const fIsElevated = IsProcessElevated();
            HWND hElevateBtn = GetDlgItem(hWnd, IDC_BTN_SYNC);
            Button_SetElevationRequiredState(hElevateBtn, !fIsElevated);
        }
        catch (DWORD dwError)
        {
        }
    }

    //是否已经设置自启动
    HKEY KEYRun;
    DWORD VauleType;
    DWORD DataLen;
    TCHAR Valuedata[128];
    ::RegOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &KEYRun);
    if (::RegQueryValueEx(KEYRun, g_AppName, 0, &VauleType, reinterpret_cast<BYTE*>(Valuedata), &DataLen) == ERROR_SUCCESS)
    {
        Button_SetCheck(GetDlgItem(hWnd, IDC_CHK_AUTOSTART), TRUE);
    }

    SetWindowText(GetDlgItem(hWnd, IDC_STATIC_LOCALHOST), L"UnKnown");
    SetWindowText(GetDlgItem(hWnd, IDC_STATIC_LOCALTIME), L"UnKnown");
    SetWindowText(GetDlgItem(hWnd, IDC_STATIC_NETHOST), L"UnKnown");
    SetWindowText(GetDlgItem(hWnd, IDC_STATIC_NETTIME), L"UnKnown");

    //获取hostname
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
    {
        char hostname[MAX_PATH];
        gethostname(hostname, MAX_PATH);
        SetWindowTextA(GetDlgItem(hWnd, IDC_STATIC_LOCALHOST), hostname);
        WSACleanup();
    }
    //更新时间定时器
    SetTimer(hWnd, UPDATE_TIME_EVENT, 1000, NULL);
    SetTimer(hWnd, SYNC_TIME_EVENT, 1000*60*10, NULL);

    int x = GetSystemMetrics(SM_CXSCREEN) / 2;
    int y = GetSystemMetrics(SM_CYSCREEN) / 2;
    RECT rect;
    GetWindowRect(hWnd, &rect);
    MoveWindow(hWnd, x, y, rect.right, rect.bottom, FALSE);

    return TRUE;
}

void BtnCheck(HWND hWnd)
{
    if (g_IsSyncing)
    {//Abort
        closesocket(g_socket);
        g_IsSyncing = false;
        EnableWindow(GetDlgItem(hWnd, IDC_BTN_CHECK), TRUE);
        SetWindowText(GetDlgItem(hWnd, IDC_BTN_CHECK), L"Check");
    }
    else
    {//Check
        DWORD dwThreadID;
        g_ThreadID = ::CreateThread(NULL, 0, GetNTPTime, (LPVOID)hWnd, 0, &dwThreadID);

        EnableWindow(GetDlgItem(hWnd, IDC_BTN_CHECK), FALSE);
        SetWindowText(GetDlgItem(hWnd, IDC_BTN_CHECK), L"Abort");
        g_IsSyncing = true;
    }
}

void BtnSync(HWND hWnd)
{
    BOOL fIsRunAsAdmin;
    try
    {
        fIsRunAsAdmin = IsRunAsAdmin();
    }
    catch (DWORD dwError)
    {
        return;
    }

    // Elevate the process if it is not run as administrator.
    if (!fIsRunAsAdmin)
    {
        wchar_t szPath[MAX_PATH];
        if (GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath)))
        {
            // Launch itself as administrator.
            SHELLEXECUTEINFO sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = szPath;
            sei.hwnd   = hWnd;
            sei.nShow  = SW_NORMAL;

            if (ShellExecuteEx(&sei))
            {
                EndDialog(hWnd, TRUE);  // Quit itself
                return;
            }
        }
    }

    closesocket(g_socket);

    DWORD  dwThreadID;
    g_ThreadID = CreateThread(NULL, 0, GetNTPTime, (LPVOID)hWnd, 0, &dwThreadID);

    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SYNC), TRUE);
    SetWindowText(GetDlgItem(hWnd, IDC_BTN_CHECK), L"Abort");
    g_IsSyncing = true;
    g_bSyncTime = true;
}

void BtnAutoStart(HWND hWnd)
{
    HKEY keyRun;
    ::RegOpenKey(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), &keyRun);
    if (Button_GetCheck(GetDlgItem(hWnd, IDC_CHK_AUTOSTART)))
    {
        wchar_t* appname = NULL;
        _get_tpgmptr(&appname);
        if ( ::RegSetValueEx(keyRun, g_AppName, 0, REG_SZ, reinterpret_cast<BYTE*>(appname), (_tcslen(appname)+1)*sizeof(wchar_t)) != ERROR_SUCCESS )
        {
            MessageBox(hWnd, L"Reg AutoStart Failed", L"Error", MB_OK);
        }
    }
    else
    {
        ::RegDeleteValue(keyRun, g_AppName);
    }
    RegCloseKey(keyRun);
}

void MinisizeToTray(HWND hWnd)
{
    g_NID.cbSize = sizeof(g_NID);
    g_NID.hWnd = hWnd;
    g_NID.uID = IDD_TTIMESYNC;
    g_NID.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_NID.uCallbackMessage = WM_SHOWTASK;
    g_NID.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON));
    StringCchCopy(g_NID.szTip, 128, g_AppName);
    Shell_NotifyIcon(NIM_ADD, &g_NID);

    ShowWindow(hWnd, SW_HIDE);
}

void OnCommand(HWND hWnd, int id, HWND hwndCtl, UINT codeNotify)
{
    switch (LOWORD(id))
    {
    case IDC_BTN_CHECK :
        BtnCheck(hWnd);
        break;
    case IDC_BTN_SYNC :
        BtnSync(hWnd);
        break;
    case IDC_CHK_MIN :
        break;
    case IDC_CHK_AUTOSTART :
        BtnAutoStart(hWnd);
        break;
    }
}

void OnTimer(HWND hWnd, UINT nIDEvent)
{
    switch (nIDEvent)
    {
    case UPDATE_TIME_EVENT :
        {
            // display localtime
            SYSTEMTIME systemtime;
            GetLocalTime(&systemtime);
            wchar_t time[36] = L"";
            StringCchPrintf(time, 36, L"%04d-%02d-%02d %02d:%02d:%02d:%04d",
                            systemtime.wYear,
                            systemtime.wMonth,
                            systemtime.wDay,
                            systemtime.wHour,
                            systemtime.wMinute,
                            systemtime.wSecond,
                            systemtime.wMilliseconds);
            SetWindowText(GetDlgItem(hWnd, IDC_STATIC_LOCALTIME), time);
        
            if (g_bSyncSucceed)
            {// sync localtime and display it
                g_NTPTime += 1;
                wchar_t time[36];
                SyncLocalTime(time, 36, false);
                SetWindowText(GetDlgItem(hWnd, IDC_STATIC_NETTIME), time);
            }
            break;
        }

    case SYNC_TIME_EVENT :
        break;

    default:
        ;
    }
}

void OnClose(HWND hWnd)
{
    if (Button_GetCheck(GetDlgItem(hWnd, IDC_CHK_MIN)))
    {
        MinisizeToTray(hWnd);
        return;
    }
    EndDialog(hWnd, 0);
}

LRESULT OnShowTask(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    if (wParam != IDD_TTIMESYNC)
    {
        return 1;
    }
    switch (lParam)
    {
    case WM_LBUTTONDBLCLK :
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
        Shell_NotifyIcon(NIM_DELETE, &g_NID);
        break;
    }
    return 0;
}

INT_PTR CALLBACK DialogProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        HANDLE_MSG(hWnd, WM_INITDIALOG, OnInitDialog);

        HANDLE_MSG(hWnd, WM_COMMAND, OnCommand);

        HANDLE_MSG(hWnd, WM_CLOSE, OnClose);

        HANDLE_MSG(hWnd, WM_TIMER, OnTimer);

    case WM_SHOWTASK :
        OnShowTask(hWnd, wParam, lParam);
        break;
    default:
        return FALSE;
    }
    return 0;
}

int APIENTRY wWinMain(__in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, __in LPWSTR lpCmdLine, __in int nShowCmd)
{
    g_hInstance = hInstance;
    return DialogBox(hInstance, MAKEINTRESOURCE(IDD_TTIMESYNC), NULL, DialogProc);
}