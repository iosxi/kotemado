/* ==================================================================
 * main.c - 常駐本体(トレイ、起動引数、多重起動の防止)
 *
 *  kotemado.exe                 常駐を始める。すでに動いていれば設定画面を出させる
 *  kotemado.exe -settings       常駐を始めて設定画面を出す
 *  kotemado.exe -apply          動いている kotemado に「今すぐ適用」させる
 *  kotemado.exe -exit           動いている kotemado を終わらせる
 *  kotemado.exe -ini <path>     設定ファイルを指定する(既定は exe と同じ場所)
 *  kotemado.exe -log            動作を <設定ファイル名>.log に書く
 *
 *  多重起動の判定は設定ファイルごと。-ini で別の設定を指定すれば
 *  並べて動かせる(検証用)。
 * ================================================================== */

#include "kotemado.h"
#include "resource.h"
#include <shlobj.h>
#include <stdarg.h>

HINSTANCE g_inst;
WCHAR     g_exePath[MAX_PATH];
WCHAR     g_iniPath[MAX_PATH];
BOOL      g_customIni;
HWND      g_trayWnd;

static WCHAR           g_logPath[MAX_PATH];
static SRWLOCK         g_logLock = SRWLOCK_INIT;
static volatile LONG   g_paused;
static NOTIFYICONDATAW g_nid;
static UINT            g_wmTaskbarCreated;

#define TRAY_CLASS L"kotemado.tray"

/* ------------------------------------------------------------------ */
/*  ログ                                                                */
/* ------------------------------------------------------------------ */

void log_printf(const WCHAR *fmt, ...)
{
    WCHAR      body[1100], line[1200];
    char       u8[3600];
    SYSTEMTIME st;
    va_list    ap;
    HANDLE     f;
    int        n;
    DWORD      wr;

    if (!g_cfg.log || !g_logPath[0]) return;
    va_start(ap, fmt);
    wvsprintfW(body, fmt, ap);          /* 最大 1024 文字で切れる */
    va_end(ap);
    GetLocalTime(&st);
    wsprintfW(line, L"%04d-%02d-%02d %02d:%02d:%02d.%03d %s\r\n", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
    n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof(u8), NULL, NULL);
    if (n <= 1) return;

    AcquireSRWLockExclusive(&g_logLock);
    f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, u8, (DWORD)(n - 1), &wr, NULL);
        CloseHandle(f);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

/* ------------------------------------------------------------------ */
/*  スタートアップ(スタートアップ フォルダのショートカット)            */
/* ------------------------------------------------------------------ */

static BOOL startup_link(WCHAR *out)
{
    PWSTR p = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Startup, 0, NULL, &p))) return FALSE;
    wsprintfW(out, L"%s\\kotemado.lnk", p);
    CoTaskMemFree(p);
    return TRUE;
}

BOOL startup_enabled(void)
{
    WCHAR lnk[MAX_PATH + 32];
    return startup_link(lnk) && GetFileAttributesW(lnk) != INVALID_FILE_ATTRIBUTES;
}

BOOL startup_set(BOOL on)
{
    WCHAR         lnk[MAX_PATH + 32], dir[MAX_PATH], args[MAX_PATH + 16];
    IShellLinkW  *sl = NULL;
    IPersistFile *pf = NULL;
    HRESULT       hr;
    WCHAR        *s;

    if (!startup_link(lnk)) return FALSE;
    if (!on) return DeleteFileW(lnk) || GetLastError() == ERROR_FILE_NOT_FOUND;

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    if (FAILED(hr)) return FALSE;
    lstrcpynW(dir, g_exePath, MAX_PATH);
    for (s = dir + lstrlenW(dir); s > dir && s[-1] != L'\\'; s--) ;
    if (s > dir) s[-1] = 0;
    sl->lpVtbl->SetPath(sl, g_exePath);
    sl->lpVtbl->SetWorkingDirectory(sl, dir);
    sl->lpVtbl->SetDescription(sl, L"kotemado - ウィンドウの位置と大きさを固定");
    if (g_customIni) {
        wsprintfW(args, L"-ini \"%s\"", g_iniPath);
        sl->lpVtbl->SetArguments(sl, args);
    }
    hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (SUCCEEDED(hr)) {
        hr = pf->lpVtbl->Save(pf, lnk, TRUE);
        pf->lpVtbl->Release(pf);
    }
    sl->lpVtbl->Release(sl);
    return SUCCEEDED(hr);
}

/* ------------------------------------------------------------------ */
/*  一時停止とトレイ                                                    */
/* ------------------------------------------------------------------ */

BOOL app_paused(void) { return g_paused != 0; }

void app_set_paused(BOOL on)
{
    InterlockedExchange(&g_paused, on ? 1 : 0);
    tray_update();
    log_printf(on ? L"一時停止しました" : L"再開しました");
}

void tray_update(void)
{
    if (!g_nid.hWnd) return;
    g_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    lstrcpynW(g_nid.szTip, g_paused ? L"kotemado（一時停止中）" : L"kotemado", ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_trayWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    LoadIconMetric(g_inst, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &g_nid.hIcon);
    lstrcpynW(g_nid.szTip, g_paused ? L"kotemado（一時停止中）" : L"kotemado", ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void tray_menu(int x, int y)
{
    HMENU m = CreatePopupMenu();
    UINT  cmd;

    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"設定を開く(&S)");
    AppendMenuW(m, MF_STRING, IDM_APPLY,    L"今すぐ適用(&A)");
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), IDM_PAUSE, L"一時停止(&P)");
    AppendMenuW(m, MF_STRING, IDM_IDENTIFY, L"ディスプレイ番号を表示(&I)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT,     L"終了(&X)");
    SetMenuDefaultItem(m, IDM_SETTINGS, FALSE);

    SetForegroundWindow(g_trayWnd);
    cmd = (UINT)TrackPopupMenuEx(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY |
                                    (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
                                 x, y, g_trayWnd, NULL);
    PostMessageW(g_trayWnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_trayWnd, WM_COMMAND, cmd, 0);
}

static LRESULT CALLBACK tray_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ui_open_main();
            break;
        case WM_CONTEXTMENU:
            tray_menu((short)LOWORD(wp), (short)HIWORD(wp));
            break;
        }
        return 0;

    case WM_APP_COMMAND:
        switch (wp) {
        case CMD_SETTINGS: ui_open_main(); break;
        case CMD_APPLY:    engine_apply_all(); break;
        case CMD_EXIT:     DestroyWindow(h); break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SETTINGS: ui_open_main(); break;
        case IDM_APPLY:    engine_apply_all(); break;
        case IDM_PAUSE:    app_set_paused(!g_paused); break;
        case IDM_IDENTIFY: ui_identify_displays(); break;
        case IDM_EXIT:     DestroyWindow(h); break;
        }
        return 0;

    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpiW((const WCHAR *)lp, L"ImmersiveColorSet") && theme_refresh()) {
            theme_allow_dark(h);
            ui_theme_changed();
        }
        return 0;

    case WM_ENDSESSION:
        if (wp) {
            engine_stop();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_nid.hWnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_wmTaskbarCreated && msg) {     /* エクスプローラが再起動した */
        tray_add();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  起動                                                                */
/* ------------------------------------------------------------------ */

static BOOL dir_writable(const WCHAR *dir)
{
    WCHAR  p[MAX_PATH + 32];
    HANDLE f;
    wsprintfW(p, L"%s\\kotemado-%lu.tmp", dir, GetCurrentProcessId());
    f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(f);
    return TRUE;
}

/* 既定は exe と同じ場所。書き込めない場所(Program Files など)に
   置かれたときだけ %APPDATA%\kotemado に置く。 */
static void default_ini(void)
{
    WCHAR dir[MAX_PATH], app[MAX_PATH], *s;
    PWSTR p = NULL;

    lstrcpynW(dir, g_exePath, MAX_PATH);
    for (s = dir + lstrlenW(dir); s > dir && s[-1] != L'\\'; s--) ;
    if (s > dir) s[-1] = 0;
    wsprintfW(g_iniPath, L"%s\\kotemado.ini", dir);
    if (GetFileAttributesW(g_iniPath) != INVALID_FILE_ATTRIBUTES) return;

    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &p))) {
        wsprintfW(app, L"%s\\kotemado\\kotemado.ini", p);
        if (GetFileAttributesW(app) != INVALID_FILE_ATTRIBUTES || !dir_writable(dir)) {
            wsprintfW(app, L"%s\\kotemado", p);
            CreateDirectoryW(app, NULL);
            wsprintfW(g_iniPath, L"%s\\kotemado\\kotemado.ini", p);
        }
        CoTaskMemFree(p);
    }
}

static void mutex_name(WCHAR *out)
{
    WCHAR low[MAX_PATH];
    DWORD h = 2166136261u;
    int   i;
    lstrcpynW(low, g_iniPath, MAX_PATH);
    CharLowerBuffW(low, lstrlenW(low));
    for (i = 0; low[i]; i++) { h ^= low[i]; h *= 16777619u; }      /* FNV-1a */
    wsprintfW(out, L"Local\\kotemado.%08lX", h);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WCHAR   mname[64];
    HANDLE  mutex;
    WNDCLASSW wc;
    MSG     msg;
    LPWSTR *argv;
    int     argc, i, cmd = 0;
    BOOL    openSettings = FALSE, first;
    INITCOMMONCONTROLSEX icc;

    (void)prev; (void)cmdline; (void)show;
    g_inst = inst;
    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (i = 1; argv && i < argc; i++) {
        const WCHAR *a = argv[i];
        if (a[0] == L'/' ) a++;
        else if (a[0] == L'-') { a++; if (a[0] == L'-') a++; }
        if (!lstrcmpiW(a, L"ini") && i + 1 < argc) {
            GetFullPathNameW(argv[++i], MAX_PATH, g_iniPath, NULL);
            g_customIni = TRUE;
        }
        else if (!lstrcmpiW(a, L"log"))      g_cfg.log = TRUE;
        else if (!lstrcmpiW(a, L"exit"))     cmd = CMD_EXIT;
        else if (!lstrcmpiW(a, L"apply"))    cmd = CMD_APPLY;
        else if (!lstrcmpiW(a, L"settings")) openSettings = TRUE;
    }
    if (argv) LocalFree(argv);
    if (!g_customIni) default_ini();

    /* 同じ設定ファイルで動いているものがあれば、そちらに頼んで終わる */
    mutex_name(mname);
    mutex = CreateMutexW(NULL, FALSE, mname);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(TRAY_CLASS, g_iniPath);
        if (other) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_APP_COMMAND, cmd ? cmd : CMD_SETTINGS, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    if (cmd) {                      /* -exit / -apply の相手がいない */
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    lstrcpynW(g_logPath, g_iniPath, MAX_PATH);
    {
        int n = lstrlenW(g_logPath);
        if (n > 4 && !lstrcmpiW(g_logPath + n - 4, L".ini")) g_logPath[n - 4] = 0;
        if (lstrlenW(g_logPath) + 5 < MAX_PATH) lstrcatW(g_logPath, L".log");
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    first = GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES;
    config_load();
    theme_init();                   /* ini の theme= を見るので読み込みの後 */
    if (first) {
        config_save();              /* 次からは黙って常駐を始めるように */
        openSettings = TRUE;
    }
    log_printf(L"kotemado %s 起動 (設定 %s)", APP_VERSION, g_iniPath);

    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = tray_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = TRAY_CLASS;
    RegisterClassW(&wc);
    /* 別のインスタンスが探せるよう、タイトルに設定ファイルのパスを入れておく */
    g_trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW, TRAY_CLASS, g_iniPath, WS_POPUP,
                                0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!g_trayWnd) return 1;
    ChangeWindowMessageFilterEx(g_trayWnd, g_wmTaskbarCreated, MSGFLT_ALLOW, NULL);
    theme_allow_dark(g_trayWnd);
    tray_add();

    engine_start();
    if (openSettings) ui_open_main();

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (ui_dialog_message(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    engine_stop();
    log_printf(L"kotemado 終了");
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
