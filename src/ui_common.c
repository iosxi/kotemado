/* ==================================================================
 * ui_common.c - ダイアログ共通の見た目と小物
 *
 *  ・見出しの太字と、Windows 11 のタスク ダイアログ風のフッタ帯
 *  ・確認やお知らせ(TaskDialog。絵柄の付いた古いメッセージ ボックスは使わない)
 *  ・ディスプレイ番号の表示(Windows の「識別」と同じ要領)
 *  ・照準でなぞっているウィンドウを囲む枠
 * ================================================================== */

#include "kotemado.h"
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#define CORNER_ROUND 2

/* ------------------------------------------------------------------ */
/*  見出しとフッタ帯                                                    */
/* ------------------------------------------------------------------ */

void dlg_look_init(HWND dlg, DlgLook *lk, const int *ids, int n, int anchorId)
{
    LOGFONTW lf;
    HFONT    base = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
    RECT     r, pad = { 0, 0, 0, 7 };
    int      i;

    lk->heading = NULL;
    if (base && GetObjectW(base, sizeof(lf), &lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        lf.lfHeight = MulDiv(lf.lfHeight, 118, 100);
        lk->heading = CreateFontIndirectW(&lf);
    }
    for (i = 0; i < n; i++)
        if (lk->heading) SendDlgItemMessageW(dlg, ids[i], WM_SETFONT, (WPARAM)lk->heading, TRUE);

    lk->footerTop = 0;
    if (anchorId) {
        GetWindowRect(GetDlgItem(dlg, anchorId), &r);
        MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
        MapDialogRect(dlg, &pad);
        lk->footerTop = r.top - pad.bottom;
    }
    SetPropW(dlg, L"kotemado.footer", (HANDLE)(INT_PTR)lk->footerTop);
}

void dlg_look_free(DlgLook *lk)
{
    if (lk->heading) DeleteObject(lk->heading);
    lk->heading = NULL;
}

BOOL dlg_look_erase(HWND dlg, HDC dc, const DlgLook *lk)
{
    RECT c, f;
    GetClientRect(dlg, &c);
    f = c;
    if (lk->footerTop > 0) {
        c.bottom = lk->footerTop;
        f.top    = lk->footerTop;
        FillRect(dc, &f, theme_footer_brush());
        /* 帯の上の区切り線 */
        {
            HBRUSH line = CreateSolidBrush(theme_line());
            RECT   l = f;
            l.bottom = l.top + 1;
            FillRect(dc, &l, line);
            DeleteObject(line);
        }
    }
    FillRect(dc, &c, theme_back_brush());
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  お知らせ・確認                                                      */
/* ------------------------------------------------------------------ */

int ui_message(HWND owner, const WCHAR *main, const WCHAR *content,
               TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon)
{
    TASKDIALOGCONFIG tc;
    int pressed = IDCANCEL;

    ZeroMemory(&tc, sizeof(tc));
    tc.cbSize             = sizeof(tc);
    tc.hwndParent         = owner;
    tc.hInstance          = g_inst;
    tc.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW |
                            TDF_SIZE_TO_CONTENT;
    tc.pszWindowTitle     = APP_NAME;
    tc.pszMainIcon        = icon;
    tc.pszMainInstruction = main;
    tc.pszContent         = content;
    tc.dwCommonButtons    = buttons ? buttons : TDCBF_OK_BUTTON;
    if (FAILED(TaskDialogIndirect(&tc, &pressed, NULL, NULL))) pressed = IDCANCEL;
    return pressed;
}

/* ------------------------------------------------------------------ */
/*  一覧に出す要約                                                      */
/* ------------------------------------------------------------------ */

void ui_cond_summary(const Rule *r, WCHAR *buf, int cch)
{
    static const WCHAR *const labels[F_COUNT] = { L"プロセス", L"クラス", L"キャプション" };
    WCHAR part[COND_MAX + 32];
    int   f;

    buf[0] = 0;
    for (f = 0; f < F_COUNT; f++) {
        const Cond *c = &r->cond[f];
        const WCHAR *t = c->text;
        if (c->mode == M_ANY) continue;
        switch (c->mode) {
        case M_EXACT:
            if (!t[0]) wsprintfW(part, L"%s なし", labels[f]);
            else       wsprintfW(part, L"%s %s", labels[f], t);
            break;
        case M_PREFIX:   wsprintfW(part, L"%s %s…", labels[f], t); break;
        case M_SUFFIX:   wsprintfW(part, L"%s …%s", labels[f], t); break;
        case M_CONTAINS: wsprintfW(part, L"%s …%s…", labels[f], t); break;
        default:         wsprintfW(part, L"%s %s", labels[f], t); break;
        }
        if (buf[0] && lstrlenW(buf) + 3 < cch) lstrcatW(buf, L"、");
        if (lstrlenW(buf) + lstrlenW(part) < cch) lstrcatW(buf, part);
    }
    if (!buf[0]) lstrcpynW(buf, L"（条件なし・使われません）", cch);
}

void ui_place_summary(const Rule *r, WCHAR *buf, int cch)
{
    WCHAR a[16], b[16], part[96];

#define ADD(s) do { if (buf[0] && lstrlenW(buf) + 2 < cch) lstrcatW(buf, L"  "); \
                    if (lstrlenW(buf) + lstrlenW(s) < cch) lstrcatW(buf, s); } while (0)
    buf[0] = 0;
    if (r->display == DISP_MAIN)      ADD(L"メイン");
    else if (r->display >= 1) { wsprintfW(part, L"ディスプレイ %d", r->display); ADD(part); }

    if (r->x.kind || r->y.kind) {
        val_format(&r->x, a, ARRAYSIZE(a)); if (!a[0]) lstrcpyW(a, L"―");
        val_format(&r->y, b, ARRAYSIZE(b)); if (!b[0]) lstrcpyW(b, L"―");
        wsprintfW(part, L"位置 %s, %s", a, b); ADD(part);
    }
    if (r->w.kind || r->h.kind) {
        val_format(&r->w, a, ARRAYSIZE(a)); if (!a[0]) lstrcpyW(a, L"―");
        val_format(&r->h, b, ARRAYSIZE(b)); if (!b[0]) lstrcpyW(b, L"―");
        wsprintfW(part, L"大きさ %s×%s", a, b); ADD(part);
    }
    if (r->state == ST_NORMAL) ADD(L"通常");
    if (r->state == ST_MAX)    ADD(L"最大化");
    if (r->state == ST_MIN)    ADD(L"最小化");
    if (r->topmost == TOP_ON)  ADD(L"最前面");
    if (r->topmost == TOP_OFF) ADD(L"最前面を解除");
    if (r->delay > 0) { wsprintfW(part, L"%dms 後", r->delay); ADD(part); }
    if (!buf[0]) lstrcpynW(buf, L"（何もしない）", cch);
#undef ADD
}

/* ------------------------------------------------------------------ */
/*  ディスプレイ番号の表示                                              */
/* ------------------------------------------------------------------ */

#define IDENT_CLASS  L"kotemado.identify"
#define IDENT_MS     4000
static HWND g_ident[MON_MAX];
static int  g_nIdent;

static void ident_close_all(void)
{
    int i;
    for (i = 0; i < g_nIdent; i++) if (g_ident[i]) DestroyWindow(g_ident[i]);
    g_nIdent = 0;
}

static LRESULT CALLBACK ident_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    MonInfo *m = (MonInfo *)GetWindowLongPtrW(h, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC    dc = BeginPaint(h, &ps);
        RECT   c, t;
        HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
        HFONT  big, small, of;
        WCHAR  num[8], line[160];
        int    s = m ? (int)m->dpi : 96;

        GetClientRect(h, &c);
        FillRect(dc, &c, bg);
        DeleteObject(bg);
        if (m) {
            big   = CreateFontW(-MulDiv(120, s, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            small = CreateFontW(-MulDiv(15, s, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            if (m->num) wsprintfW(num, L"%d", m->num); else lstrcpyW(num, L"?");
            of = (HFONT)SelectObject(dc, big);
            t = c; t.bottom = c.top + (c.bottom - c.top) * 70 / 100;
            DrawTextW(dc, num, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, small);
            wsprintfW(line, L"%s\n%d × %d%s", m->name,
                      m->rcMon.right - m->rcMon.left, m->rcMon.bottom - m->rcMon.top,
                      m->primary ? L"（メイン）" : L"");
            SetTextColor(dc, RGB(200, 200, 200));
            t = c; t.top = c.top + (c.bottom - c.top) * 64 / 100;
            DrawTextW(dc, line, -1, &t, DT_CENTER | DT_NOPREFIX);
            SelectObject(dc, of);
            DeleteObject(big);
            DeleteObject(small);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_TIMER:
        ident_close_all();
        return 0;
    case WM_NCDESTROY:
        if (m) HeapFree(GetProcessHeap(), 0, m);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void ui_identify_displays(void)
{
    static BOOL reg;
    MonInfo     mons[MON_MAX];
    int         i, n;

    if (!reg) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc   = ident_proc;
        wc.hInstance     = g_inst;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = IDENT_CLASS;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    ident_close_all();

    n = monitors_get(mons, MON_MAX);
    for (i = 0; i < n; i++) {
        MonInfo *m = (MonInfo *)HeapAlloc(GetProcessHeap(), 0, sizeof(MonInfo));
        int      w, hgt, x, y;
        HWND     h;
        DWORD    corner = CORNER_ROUND;

        if (!m) continue;
        *m  = mons[i];
        w   = MulDiv(280, m->dpi, 96);
        hgt = MulDiv(240, m->dpi, 96);
        x   = m->rcMon.left + (m->rcMon.right - m->rcMon.left - w) / 2;
        y   = m->rcMon.top  + (m->rcMon.bottom - m->rcMon.top - hgt) / 2;
        h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                            IDENT_CLASS, L"kotemado", WS_POPUP, x, y, w, hgt, NULL, NULL, g_inst, NULL);
        if (!h) { HeapFree(GetProcessHeap(), 0, m); continue; }
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)m);
        SetLayeredWindowAttributes(h, 0, 236, LWA_ALPHA);
        DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        ShowWindow(h, SW_SHOWNOACTIVATE);
        if (g_nIdent == 0) SetTimer(h, 1, IDENT_MS, NULL);
        g_ident[g_nIdent++] = h;
    }
}

/* ------------------------------------------------------------------ */
/*  照準の枠                                                            */
/* ------------------------------------------------------------------ */

#define HL_CLASS L"kotemado.highlight"
static HWND g_hl;

static LRESULT CALLBACK hl_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

void highlight_show(const RECT *r)
{
    int  t, w, hgt;
    HRGN outer, inner;

    if (!g_hl) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc   = hl_proc;
        wc.hInstance     = g_inst;
        wc.hbrBackground = CreateSolidBrush(RGB(255, 72, 32));
        wc.lpszClassName = HL_CLASS;
        RegisterClassW(&wc);
        g_hl = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                               HL_CLASS, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_inst, NULL);
        if (!g_hl) return;
    }
    {
        MonInfo m;
        UINT    dpi = 96;
        if (monitor_info(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &m)) dpi = m.dpi;
        t = MulDiv(3, dpi, 96);
    }
    w   = r->right - r->left + 2 * t;
    hgt = r->bottom - r->top + 2 * t;
    outer = CreateRectRgn(0, 0, w, hgt);
    inner = CreateRectRgn(t, t, w - t, hgt - t);
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    SetWindowRgn(g_hl, outer, FALSE);       /* 領域はウィンドウが持つので消さない */
    SetWindowPos(g_hl, HWND_TOPMOST, r->left - t, r->top - t, w, hgt,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hl, NULL, TRUE);
}

void highlight_hide(void)
{
    if (g_hl) ShowWindow(g_hl, SW_HIDE);
}
