/* ==================================================================
 * target.c - kotemado の検証用ウィンドウ
 *
 *  指定したクラス名・キャプション・位置で窓を出し、決めた時間が
 *  たったら自分の状態を 1 行書いて終わる。kotemado がどう動かしたかを
 *  画面を撮らずに数字で確かめるためのもの。
 *
 *  target.exe -out FILE [-class C] [-title T | -notitle] [-x X -y Y -w W -h H]
 *             [-life MS] [-max] [-unaware] [-popup] [-topmost]
 *             [-retitle MS TEXT]      MS 後にキャプションを変える
 *             [-selfmove MS X Y]      MS 後に自分で動く(表示直後に位置を戻すアプリの真似)
 *             [-reshow MS]            MS 後にいったん隠して (0,0) へ動き、また出す
 *
 *  出力: rect=L,T,R,B frame=L,T,R,B zoomed=0/1 iconic=0/1 topmost=0/1
 *        normal=L,T,R,B dpi=N wpc=N(WM_WINDOWPOSCHANGING を受けた数)
 * ================================================================== */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>

static WCHAR g_out[MAX_PATH];
static WCHAR g_retitle[256];
static int   g_wpc;             /* 受けた WM_WINDOWPOSCHANGING の数 */
static int   g_retitleMs = -1, g_selfMs = -1, g_selfX, g_selfY, g_reshowMs = -1;

static void dump(HWND h)
{
    RECT r, f;
    WINDOWPLACEMENT wp;
    FILE *fp;

    GetWindowRect(h, &r);
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f)))) f = r;
    wp.length = sizeof(wp);
    GetWindowPlacement(h, &wp);
    fp = _wfopen(g_out, L"w");
    if (!fp) return;
    fprintf(fp, "rect=%ld,%ld,%ld,%ld frame=%ld,%ld,%ld,%ld zoomed=%d iconic=%d topmost=%d "
                "normal=%ld,%ld,%ld,%ld dpi=%u wpc=%d\n",
            r.left, r.top, r.right, r.bottom, f.left, f.top, f.right, f.bottom,
            IsZoomed(h) ? 1 : 0, IsIconic(h) ? 1 : 0,
            (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) ? 1 : 0,
            wp.rcNormalPosition.left, wp.rcNormalPosition.top,
            wp.rcNormalPosition.right, wp.rcNormalPosition.bottom, GetDpiForWindow(h), g_wpc);
    fclose(fp);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_WINDOWPOSCHANGING:
        g_wpc++;
        break;
    case WM_TIMER:
        if (wp == 1) { dump(h); DestroyWindow(h); }
        if (wp == 2) { KillTimer(h, 2); SetWindowTextW(h, g_retitle); }
        if (wp == 3) { KillTimer(h, 3);
                       SetWindowPos(h, NULL, g_selfX, g_selfY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
        if (wp == 4) { KillTimer(h, 4);
                       ShowWindow(h, SW_HIDE);
                       SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                       ShowWindow(h, SW_SHOWNOACTIVATE); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cl, int show)
{
    WCHAR cls[256] = L"KtmTarget", title[256] = L"kotemado target";
    int   x = 50, y = 50, w = 400, hgt = 300, life = 1500, i, argc;
    BOOL  max = FALSE, unaware = FALSE, popup = FALSE, topmost = FALSE;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    WNDCLASSW wc = { 0 };
    HWND  h;
    MSG   msg;
    (void)prev; (void)cl; (void)show;

    for (i = 1; i < argc; i++) {
        const WCHAR *a = argv[i];
        if (!wcscmp(a, L"-out") && i + 1 < argc)     lstrcpynW(g_out, argv[++i], MAX_PATH);
        else if (!wcscmp(a, L"-class") && i + 1 < argc) lstrcpynW(cls, argv[++i], 256);
        else if (!wcscmp(a, L"-title") && i + 1 < argc) lstrcpynW(title, argv[++i], 256);
        else if (!wcscmp(a, L"-notitle"))   title[0] = 0;
        else if (!wcscmp(a, L"-x") && i + 1 < argc) x = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-y") && i + 1 < argc) y = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-w") && i + 1 < argc) w = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-h") && i + 1 < argc) hgt = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-life") && i + 1 < argc) life = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-max"))     max = TRUE;
        else if (!wcscmp(a, L"-unaware")) unaware = TRUE;
        else if (!wcscmp(a, L"-popup"))   popup = TRUE;
        else if (!wcscmp(a, L"-topmost")) topmost = TRUE;
        else if (!wcscmp(a, L"-retitle") && i + 2 < argc) {
            g_retitleMs = _wtoi(argv[++i]); lstrcpynW(g_retitle, argv[++i], 256);
        }
        else if (!wcscmp(a, L"-selfmove") && i + 3 < argc) {
            g_selfMs = _wtoi(argv[++i]); g_selfX = _wtoi(argv[++i]); g_selfY = _wtoi(argv[++i]);
        }
        else if (!wcscmp(a, L"-reshow") && i + 1 < argc) g_reshowMs = _wtoi(argv[++i]);
    }
    SetProcessDpiAwarenessContext(unaware ? DPI_AWARENESS_CONTEXT_UNAWARE
                                          : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wc.lpfnWndProc   = proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);
    h = CreateWindowExW(topmost ? WS_EX_TOPMOST : 0, cls, title, popup ? (WS_POPUP | WS_BORDER) : WS_OVERLAPPEDWINDOW,
                        x, y, w, hgt, NULL, NULL, inst, NULL);
    ShowWindow(h, max ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
    SetTimer(h, 1, life, NULL);
    if (g_retitleMs >= 0) SetTimer(h, 2, g_retitleMs, NULL);
    if (g_selfMs >= 0)    SetTimer(h, 3, g_selfMs, NULL);
    if (g_reshowMs >= 0)  SetTimer(h, 4, g_reshowMs, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
