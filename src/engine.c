/* ==================================================================
 * engine.c - ウィンドウの監視と配置
 *
 *  ポーリングはしない。SetWinEventHook(WINEVENT_OUTOFCONTEXT)で
 *  「表示された」「名前(キャプション)が変わった」「破棄された」の
 *  通知だけを受け、その場で照合して動かす。待っている間は CPU を使わない。
 *
 *  専用スレッドで動かす。他プロセスのウィンドウへの SetWindowPos は
 *  相手の応答を待つので、設定画面と同じスレッドだと画面が固まりうる。
 *
 *  照合の材料(クラス名・キャプション・プロセス名)は、ルールが
 *  必要としたものだけ取りに行く。キャプションは InternalGetWindowText で
 *  読む。GetWindowText と違って相手にメッセージを送らないので、
 *  応答しないウィンドウでも待たされない。
 * ================================================================== */

#include "kotemado.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <wchar.h>

/* ------------------------------------------------------------------ */
/*  照合                                                                */
/* ------------------------------------------------------------------ */

void wininfo_init(WinInfo *wi, HWND hwnd)
{
    wi->hwnd = hwnd;
    ZeroMemory(wi->have, sizeof(wi->have));
    wi->path[0] = 0;
}

static void fetch(WinInfo *wi, int f)
{
    WCHAR *raw = wi->raw[f];

    raw[0] = 0;
    switch (f) {
    case F_CLASS:
        GetClassNameW(wi->hwnd, raw, COND_MAX);
        break;
    case F_TITLE:
        InternalGetWindowText(wi->hwnd, raw, COND_MAX);
        break;
    case F_EXE: {
        DWORD  pid = 0, n = MAX_PATH;
        HANDLE p;
        GetWindowThreadProcessId(wi->hwnd, &pid);
        p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (p) {
            if (QueryFullProcessImageNameW(p, 0, wi->path, &n)) {
                const WCHAR *b = wi->path, *s;
                for (s = wi->path; *s; s++) if (*s == L'\\') b = s + 1;
                lstrcpynW(raw, b, COND_MAX);
                CharLowerBuffW(wi->path, lstrlenW(wi->path));
            } else
                wi->path[0] = 0;
            CloseHandle(p);
        }
        break;
    }
    }
    lstrcpynW(wi->val[f], raw, COND_MAX);
    CharLowerBuffW(wi->val[f], lstrlenW(wi->val[f]));
    wi->have[f] = 1;
}

const WCHAR *wininfo_raw(WinInfo *wi, int field)
{
    if (!wi->have[field]) fetch(wi, field);
    return wi->raw[field];
}

/* * と ? だけのワイルドカード。戻りは * の位置からやり直す方式で、再帰しない */
static BOOL wild(const WCHAR *p, const WCHAR *s)
{
    const WCHAR *star = NULL, *mark = s;

    while (*s) {
        if (*p == L'?' || (*p && *p != L'*' && *p == *s)) { p++; s++; }
        else if (*p == L'*') { star = p++; mark = s; }
        else if (star)       { p = star + 1; s = ++mark; }
        else return FALSE;
    }
    while (*p == L'*') p++;
    return *p == 0;
}

static BOOL str_match(int mode, const WCHAR *pat, const WCHAR *s)
{
    size_t np, ns;

    switch (mode) {
    case M_EXACT:    return wcscmp(s, pat) == 0;
    case M_PREFIX:   return wcsncmp(s, pat, wcslen(pat)) == 0;
    case M_SUFFIX:
        np = wcslen(pat); ns = wcslen(s);
        return ns >= np && wcscmp(s + ns - np, pat) == 0;
    case M_CONTAINS: return wcsstr(s, pat) != NULL;
    case M_WILDCARD: return wild(pat, s);
    }
    return TRUE;
}

BOOL rule_match(const Rule *r, WinInfo *wi)
{
    int  f;
    BOOL any = FALSE;

    for (f = 0; f < F_COUNT; f++) {
        const Cond  *c = &r->cond[f];
        const WCHAR *s;
        if (c->mode == M_ANY) continue;
        any = TRUE;
        if (!wi->have[f]) fetch(wi, f);
        s = wi->val[f];
        /* プロセス名に \ を含めたらフルパスと比べる */
        if (f == F_EXE && wcschr(c->low, L'\\')) s = wi->path;
        if (!str_match(c->mode, c->low, s)) return FALSE;
    }
    return any;     /* 条件が 1 つも無いルールは、どのウィンドウにも合わせない */
}

/* ------------------------------------------------------------------ */
/*  モニタ                                                              */
/* ------------------------------------------------------------------ */

static UINT mon_dpi(HMONITOR m)
{
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &x, &y))) x = 96;
    return x;
}

static int display_number(const WCHAR *dev)
{
    const WCHAR *p = dev + lstrlenW(dev);
    int n = 0, mul = 1;
    while (p > dev && p[-1] >= L'0' && p[-1] <= L'9') { p--; n += (*p - L'0') * mul; mul *= 10; }
    return n;
}

/* 設定画面に出す名前。ドライバが名前を返さない内蔵パネルは「内蔵ディスプレイ」 */
static void friendly_name(const WCHAR *gdi, WCHAR *out, int cch)
{
    UINT32 np = 0, nm = 0, i;
    DISPLAYCONFIG_PATH_INFO *paths;
    DISPLAYCONFIG_MODE_INFO *modes;

    out[0] = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return;
    paths = (DISPLAYCONFIG_PATH_INFO *)HeapAlloc(GetProcessHeap(), 0, np * sizeof(*paths) + 1);
    modes = (DISPLAYCONFIG_MODE_INFO *)HeapAlloc(GetProcessHeap(), 0, nm * sizeof(*modes) + 1);
    if (paths && modes &&
        QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, NULL) == ERROR_SUCCESS) {
        for (i = 0; i < np; i++) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME src;
            DISPLAYCONFIG_TARGET_DEVICE_NAME tgt;

            ZeroMemory(&src, sizeof(src));
            src.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            src.header.size      = sizeof(src);
            src.header.adapterId = paths[i].sourceInfo.adapterId;
            src.header.id        = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
            if (lstrcmpiW(src.viewGdiDeviceName, gdi)) continue;

            ZeroMemory(&tgt, sizeof(tgt));
            tgt.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            tgt.header.size      = sizeof(tgt);
            tgt.header.adapterId = paths[i].targetInfo.adapterId;
            tgt.header.id        = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS &&
                tgt.monitorFriendlyDeviceName[0]) {
                lstrcpynW(out, tgt.monitorFriendlyDeviceName, cch);
            } else {
                DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY t = paths[i].targetInfo.outputTechnology;
                if (t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                    t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                    t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED)
                    lstrcpynW(out, L"内蔵ディスプレイ", cch);
            }
            break;
        }
    }
    if (paths) HeapFree(GetProcessHeap(), 0, paths);
    if (modes) HeapFree(GetProcessHeap(), 0, modes);
}

static BOOL monitor_info_ex(HMONITOR h, MonInfo *m, BOOL withName)
{
    MONITORINFOEXW mi;

    ZeroMemory(m, sizeof(*m));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, (MONITORINFO *)&mi)) return FALSE;
    m->h       = h;
    m->rcMon   = mi.rcMonitor;
    m->rcWork  = mi.rcWork;
    m->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m->num     = display_number(mi.szDevice);
    m->dpi     = mon_dpi(h);
    if (withName) {
        friendly_name(mi.szDevice, m->name, ARRAYSIZE(m->name));
        if (!m->name[0]) lstrcpynW(m->name, L"ディスプレイ", ARRAYSIZE(m->name));
    }
    return TRUE;
}

BOOL monitor_info(HMONITOR h, MonInfo *out) { return monitor_info_ex(h, out, TRUE); }

typedef struct { MonInfo *out; int n, max; BOOL names; } MonEnum;

static BOOL CALLBACK mon_enum_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp)
{
    MonEnum *e = (MonEnum *)lp;
    (void)dc; (void)rc;
    if (e->n < e->max && monitor_info_ex(h, &e->out[e->n], e->names)) e->n++;
    return TRUE;
}

static int monitors_enum(MonInfo *out, int max, BOOL names)
{
    MonEnum e;
    int i, j;
    e.out = out; e.n = 0; e.max = max; e.names = names;
    EnumDisplayMonitors(NULL, NULL, mon_enum_cb, (LPARAM)&e);
    /* 番号順に並べる */
    for (i = 1; i < e.n; i++)
        for (j = i; j > 0 && out[j - 1].num > out[j].num; j--) {
            MonInfo t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return e.n;
}

int monitors_get(MonInfo *out, int max) { return monitors_enum(out, max, TRUE); }

static HMONITOR resolve_monitor(int display, HMONITOR cur)
{
    MonInfo m[MON_MAX];
    POINT   o = { 0, 0 };
    int     i, n;

    if (display == DISP_CURRENT) return cur;
    if (display == DISP_MAIN)    return MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    n = monitors_enum(m, MON_MAX, FALSE);
    for (i = 0; i < n; i++) if (m[i].num == display) return m[i].h;
    return NULL;
}

BOOL window_visible_rect(HWND h, RECT *out)
{
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, out, sizeof(*out))) &&
        out->right > out->left && out->bottom > out->top)
        return TRUE;
    return GetWindowRect(h, out);
}

/* ------------------------------------------------------------------ */
/*  配置                                                                */
/* ------------------------------------------------------------------ */

typedef struct { int l, t, r, b; } Margins;

static void base_rect(HMONITOR m, BOOL work, RECT *out)
{
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(m, &mi);
    *out = work ? mi.rcWork : mi.rcMonitor;
}

/* WINDOWPLACEMENT.rcNormalPosition は「ワークスペース座標」。
   ツールウィンドウ以外は、メインのモニタの作業領域の左上が原点になる。 */
static POINT workspace_offset(HWND h)
{
    POINT o = { 0, 0 }, z = { 0, 0 };
    if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(z, MONITOR_DEFAULTTOPRIMARY), &mi);
        o.x = mi.rcWork.left - mi.rcMonitor.left;
        o.y = mi.rcWork.top  - mi.rcMonitor.top;
    }
    return o;
}

/* Windows 10/11 の窓は、見た目の枠の外側に透明なつまみ代を持っている。
   その幅を測る。最大化・最小化中は測れないので、枠の太さから見積もる。 */
static void frame_margins(HWND h, BOOL normalNow, UINT dpi, Margins *m)
{
    LONG st;
    ZeroMemory(m, sizeof(*m));

    if (normalNow) {
        RECT w, f;
        GetWindowRect(h, &w);
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f))) &&
            f.right > f.left) {
            m->l = f.left - w.left;  m->t = f.top - w.top;
            m->r = w.right - f.right; m->b = w.bottom - f.bottom;
            if (m->l < 0 || m->t < 0 || m->r < 0 || m->b < 0 ||
                m->l > 64 || m->t > 64 || m->r > 64 || m->b > 64)
                ZeroMemory(m, sizeof(*m));
        }
        return;
    }

    st = GetWindowLongW(h, GWL_STYLE);
    if (st & WS_THICKFRAME) {
        int b = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) - MulDiv(1, dpi, 96);
        if (b < 0) b = 0;
        m->l = m->r = m->b = b;
        m->t = ((st & WS_CAPTION) == WS_CAPTION) ? 0 : b;
    }
}

static int eval_size(const Val *v, int span, int keep)
{
    int n;
    switch (v->kind) {
    case V_PX:  n = v->v; break;
    case V_PCT: n = MulDiv(span, v->v, 100); break;
    default:    return keep;
    }
    return n < 1 ? 1 : n;
}

static int eval_pos(const Val *v, int origin, int span, int size, int keep)
{
    switch (v->kind) {
    case V_PX:     return origin + v->v;
    case V_PCT:    return origin + MulDiv(span, v->v, 100);
    case V_CENTER: return origin + (span - size) / 2;
    }
    return keep;
}

/* 置き先のウィンドウ矩形(スクリーン座標)を求める。
   wr は今の「元に戻したときの」ウィンドウ矩形。 */
static void calc_target(HWND h, const Rule *r, BOOL normalNow, const RECT *wr,
                        HMONITOR cm, HMONITOR tm, RECT *T)
{
    RECT    bc, bt;
    Margins m;
    UINT    dc = mon_dpi(cm), dt = mon_dpi(tm);
    int     vx, vy, vw, vh, w, hh, x, y, bw, bh;

    base_rect(cm, r->work, &bc);
    base_rect(tm, r->work, &bt);
    bw = bt.right - bt.left;
    bh = bt.bottom - bt.top;

    if (r->visframe) frame_margins(h, normalNow, dc, &m);
    else             ZeroMemory(&m, sizeof(m));

    vx = wr->left + m.l;
    vy = wr->top  + m.t;
    vw = (wr->right  - m.r) - vx;
    vh = (wr->bottom - m.b) - vy;

    if (dc != dt) {
        /* DPI の違うモニタへ移ると、ウィンドウ側(または OS)が拡大縮小する */
        m.l = MulDiv(m.l, dt, dc); m.t = MulDiv(m.t, dt, dc);
        m.r = MulDiv(m.r, dt, dc); m.b = MulDiv(m.b, dt, dc);
        vw  = MulDiv(vw, dt, dc);  vh  = MulDiv(vh, dt, dc);
    }

    w  = eval_size(&r->w, bw, vw);
    hh = eval_size(&r->h, bh, vh);
    x  = eval_pos(&r->x, bt.left, bw, w,  cm == tm ? vx : bt.left + (vx - bc.left));
    y  = eval_pos(&r->y, bt.top,  bh, hh, cm == tm ? vy : bt.top  + (vy - bc.top));

    if (cm != tm) {
        /* 別のディスプレイへ移すときは、位置を指定していない軸もはみ出さないよう収める */
        if (r->x.kind == V_KEEP) {
            if (x + w > bt.right) x = bt.right - w;
            if (x < bt.left)      x = bt.left;
        }
        if (r->y.kind == V_KEEP) {
            if (y + hh > bt.bottom) y = bt.bottom - hh;
            if (y < bt.top)         y = bt.top;
        }
    }

    T->left   = x - m.l;
    T->top    = y - m.t;
    T->right  = x + w + m.r;
    T->bottom = y + hh + m.b;
}

static BOOL set_pos(HWND h, const RECT *T)
{
    return SetWindowPos(h, NULL, T->left, T->top, T->right - T->left, T->bottom - T->top,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

/* 通常表示のウィンドウを T へ動かす。DPI の違うモニタへ移ったときは
   ウィンドウ自身が大きさを変えるので、落ち着いてからもう一度合わせる。 */
static BOOL move_normal(HWND h, const Rule *r, const RECT *T, HMONITOR tm)
{
    HMONITOR before = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    RECT     now, T2;

    if (!set_pos(h, T)) return FALSE;
    if (mon_dpi(before) == mon_dpi(tm)) return TRUE;

    GetWindowRect(h, &now);
    calc_target(h, r, TRUE, &now, MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), tm, &T2);
    if (!EqualRect(&now, &T2)) set_pos(h, &T2);
    return TRUE;
}

int rule_apply(HWND h, const Rule *r)
{
    WINDOWPLACEMENT wp;
    HMONITOR cm, tm;
    RECT     wr, T;
    int      cur, want;
    BOOL     ok = TRUE;

    if (!IsWindow(h)) return AP_GONE;
    if (IsHungAppWindow(h)) return AP_HUNG;

    wp.length = sizeof(wp);
    if (!GetWindowPlacement(h, &wp)) return AP_FAILED;
    cur  = IsIconic(h) ? ST_MIN : IsZoomed(h) ? ST_MAX : ST_NORMAL;
    want = r->state == ST_KEEP ? cur : r->state;

    if (cur == ST_NORMAL)
        GetWindowRect(h, &wr);
    else {
        POINT o = workspace_offset(h);
        wr = wp.rcNormalPosition;
        OffsetRect(&wr, o.x, o.y);
    }

    cm = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
    tm = resolve_monitor(r->display, cm);
    if (!tm) return AP_NODISPLAY;

    if (r->x.kind || r->y.kind || r->w.kind || r->h.kind || tm != cm || want != cur) {
        calc_target(h, r, cur == ST_NORMAL, &wr, cm, tm, &T);

        if (cur == ST_NORMAL) {
            ok = move_normal(h, r, &T, tm);
            if (want == ST_MAX)      ShowWindow(h, SW_MAXIMIZE);
            else if (want == ST_MIN) ShowWindow(h, SW_SHOWMINNOACTIVE);
        } else {
            POINT o = workspace_offset(h);
            wp.rcNormalPosition = T;
            OffsetRect(&wp.rcNormalPosition, -o.x, -o.y);

            if (want == cur && (cur == ST_MIN || tm == cm)) {
                /* 状態はそのまま、元に戻したときの位置だけ差し替える */
                wp.showCmd = SW_SHOWNA;
                ok = SetWindowPlacement(h, &wp);
            } else if (want == ST_MIN) {
                wp.showCmd = SW_SHOWMINNOACTIVE;
                ok = SetWindowPlacement(h, &wp);
            } else {
                /* いったん元の大きさに戻して目的のディスプレイへ移してから、必要なら最大化 */
                wp.showCmd = SW_SHOWNOACTIVATE;
                ok = SetWindowPlacement(h, &wp);
                if (ok) ok = move_normal(h, r, &T, tm);
                if (want == ST_MAX) ShowWindow(h, SW_MAXIMIZE);
            }
        }
    }

    if (r->topmost != TOP_KEEP) {
        if (!SetWindowPos(h, r->topmost == TOP_ON ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            ok = FALSE;
    }
    return ok ? AP_OK : AP_FAILED;
}

/* ------------------------------------------------------------------ */
/*  監視スレッド                                                        */
/* ------------------------------------------------------------------ */

#define WM_ENG_CONFIG   (WM_APP + 10)
#define WM_ENG_APPLYALL (WM_APP + 11)
#define TIMER_PENDING   1

static HANDLE g_thread;
static DWORD  g_tid;
static HWND   g_engWnd;
static HWND   g_desktop;
static BOOL   g_anyTitle;       /* キャプションを条件にしたルールがあるか */

/* ウィンドウごとに、最後に当てたルール。「最初の 1 回だけ」の判定に使う */
typedef struct { HWND h; UINT id; } Seen;
#define SEEN_MAX 1024
static Seen g_seen[SEEN_MAX];
static int  g_nSeen;

/* 遅延して当てる予定 */
typedef struct { HWND h; UINT id; DWORD due; } Pend;
#define PEND_MAX 128
static Pend g_pend[PEND_MAX];
static int  g_nPend;

static int seen_find(HWND h)
{
    int i;
    for (i = 0; i < g_nSeen; i++) if (g_seen[i].h == h) return i;
    return -1;
}

static void seen_del(HWND h)
{
    int i = seen_find(h);
    if (i >= 0) g_seen[i] = g_seen[--g_nSeen];
}

static void seen_set(HWND h, UINT id)
{
    int i = seen_find(h);
    if (i >= 0) { g_seen[i].id = id; return; }
    if (g_nSeen == SEEN_MAX) {
        /* 破棄の通知を取りこぼしたものを掃除する。それでも満杯なら古いものを捨てる */
        int j = 0;
        for (i = 0; i < g_nSeen; i++) if (IsWindow(g_seen[i].h)) g_seen[j++] = g_seen[i];
        g_nSeen = j;
        if (g_nSeen == SEEN_MAX) {
            MoveMemory(g_seen, g_seen + SEEN_MAX / 4, (SEEN_MAX - SEEN_MAX / 4) * sizeof(Seen));
            g_nSeen -= SEEN_MAX / 4;
        }
    }
    g_seen[g_nSeen].h  = h;
    g_seen[g_nSeen].id = id;
    g_nSeen++;
}

static void pend_arm(void)
{
    DWORD now = GetTickCount(), wait = INFINITE;
    int   i;
    for (i = 0; i < g_nPend; i++) {
        LONG d = (LONG)(g_pend[i].due - now);
        DWORD w = d < 0 ? 0 : (DWORD)d;
        if (w < wait) wait = w;
    }
    if (wait == INFINITE) KillTimer(g_engWnd, TIMER_PENDING);
    else SetTimer(g_engWnd, TIMER_PENDING, wait < USER_TIMER_MINIMUM ? USER_TIMER_MINIMUM : wait, NULL);
}

static void pend_add(HWND h, UINT id, int delay)
{
    int i;
    for (i = 0; i < g_nPend; i++) if (g_pend[i].h == h) break;
    if (i == g_nPend) {
        if (g_nPend == PEND_MAX) {
            MoveMemory(g_pend, g_pend + 1, (PEND_MAX - 1) * sizeof(Pend));
            g_nPend--;
            i = g_nPend;
        }
        g_nPend++;
    }
    g_pend[i].h   = h;
    g_pend[i].id  = id;
    g_pend[i].due = GetTickCount() + (DWORD)delay;
    pend_arm();
}

static BOOL rule_by_id(UINT id, Rule *out)
{
    BOOL found = FALSE;
    int  i;
    AcquireSRWLockShared(&g_cfgLock);
    for (i = 0; i < g_cfg.count; i++)
        if (g_cfg.rules[i].id == id) {
            if (g_cfg.rules[i].enabled) { *out = g_cfg.rules[i]; found = TRUE; }
            break;
        }
    ReleaseSRWLockShared(&g_cfgLock);
    return found;
}

static void apply_logged(HWND h, const Rule *r, WinInfo *wi, const WCHAR *why)
{
    DWORD t0 = GetTickCount();
    int   rc = rule_apply(h, r);
    if (g_cfg.log) {
        static const WCHAR *const names[] = { L"OK", L"消滅", L"応答なし", L"ディスプレイなし", L"失敗" };
        log_printf(L"%s: 「%s」 0x%08lX exe=%s class=%s title=%s → %s (%lums)",
                   why, r->name, (DWORD)(ULONG_PTR)h, wininfo_raw(wi, F_EXE), wininfo_raw(wi, F_CLASS),
                   wininfo_raw(wi, F_TITLE), names[rc], GetTickCount() - t0);
    }
}

static void pend_run(void)
{
    DWORD now = GetTickCount();
    int   i = 0;

    while (i < g_nPend) {
        if ((LONG)(now - g_pend[i].due) >= 0) {
            Pend    p = g_pend[i];
            Rule    r;
            WinInfo wi;
            g_pend[i] = g_pend[--g_nPend];
            if (!app_paused() && IsWindow(p.h) && rule_by_id(p.id, &r)) {
                wininfo_init(&wi, p.h);
                apply_logged(p.h, &r, &wi, L"適用(遅延)");
            }
        } else
            i++;
    }
    pend_arm();
}

/* 最初に条件が合った有効なルールを写し取る */
static BOOL find_rule(WinInfo *wi, Rule *out)
{
    BOOL found = FALSE;
    int  i;
    AcquireSRWLockShared(&g_cfgLock);
    for (i = 0; i < g_cfg.count; i++) {
        const Rule *r = &g_cfg.rules[i];
        if (r->enabled && rule_match(r, wi)) { *out = *r; found = TRUE; break; }
    }
    ReleaseSRWLockShared(&g_cfgLock);
    return found;
}

enum { EV_SHOW, EV_NAME, EV_ALL, EV_FORCE };

static void evaluate(HWND h, int ev)
{
    WinInfo wi;
    Rule    r;
    int     si;
    UINT    last;

    wininfo_init(&wi, h);
    if (!find_rule(&wi, &r)) return;

    si   = seen_find(h);
    last = si >= 0 ? g_seen[si].id : 0;

    switch (ev) {
    case EV_SHOW:
    case EV_ALL:
        if (r.when == WHEN_ONCE && last == r.id) return;
        break;
    case EV_NAME:
        /* キャプションが変わって、当てはまるルールが変わったときだけ */
        if (last == r.id) return;
        break;
    }
    seen_set(h, r.id);

    if (r.delay > 0 && ev != EV_FORCE)
        pend_add(h, r.id, r.delay);
    else
        apply_logged(h, &r, &wi, ev == EV_NAME ? L"適用(名前変更)" : L"適用");
}

static void CALLBACK on_event(HWINEVENTHOOK hk, DWORD ev, HWND h, LONG obj, LONG child,
                              DWORD tid, DWORD time)
{
    (void)hk; (void)tid; (void)time;

    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    if (ev == EVENT_OBJECT_DESTROY) { seen_del(h); return; }
    if (app_paused()) return;
    if (ev == EVENT_OBJECT_NAMECHANGE && !g_anyTitle) return;
    if (GetAncestor(h, GA_PARENT) != g_desktop) return;     /* トップレベルだけ */
    if (ev == EVENT_OBJECT_NAMECHANGE && !IsWindowVisible(h)) return;

    evaluate(h, ev == EVENT_OBJECT_SHOW ? EV_SHOW : EV_NAME);
}

static BOOL CALLBACK enum_cb(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    if (!IsWindowVisible(h)) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;
    evaluate(h, (int)lp);
    return TRUE;
}

static void update_flags(void)
{
    int i;
    BOOL any = FALSE;
    AcquireSRWLockShared(&g_cfgLock);
    for (i = 0; i < g_cfg.count; i++)
        if (g_cfg.rules[i].enabled && g_cfg.rules[i].cond[F_TITLE].mode != M_ANY) any = TRUE;
    ReleaseSRWLockShared(&g_cfgLock);
    g_anyTitle = any;
}

static LRESULT CALLBACK eng_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_PENDING) pend_run();
        return 0;
    case WM_ENG_CONFIG:
        update_flags();
        return 0;
    case WM_ENG_APPLYALL:
        if (!app_paused()) EnumWindows(enum_cb, EV_FORCE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static DWORD WINAPI eng_thread(LPVOID param)
{
    HANDLE        ready = (HANDLE)param;
    WNDCLASSW     wc;
    HWINEVENTHOOK hk1, hk2;
    MSG           msg;
    DWORD         t0 = GetTickCount();

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = eng_proc;
    wc.hInstance     = g_inst;
    wc.lpszClassName = L"kotemado.engine";
    RegisterClassW(&wc);
    g_engWnd  = CreateWindowExW(0, wc.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_inst, NULL);
    g_desktop = GetDesktopWindow();
    update_flags();

    /* DESTROY(0x8001)と SHOW(0x8002)は連番なので 1 本で受ける。
       NAMECHANGE(0x800C)との間にある LOCATIONCHANGE などの頻繁な通知は
       範囲に入れたくないので、こちらは別に掛ける。 */
    hk1 = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, NULL, on_event, 0, 0,
                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hk2 = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, on_event, 0, 0,
                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetEvent(ready);

    if (g_cfg.applyExisting && !app_paused()) EnumWindows(enum_cb, EV_ALL);
    log_printf(L"監視を開始しました(ルール %d 件、既存ウィンドウの処理 %lums)",
               g_cfg.count, GetTickCount() - t0);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) DispatchMessageW(&msg);

    if (hk1) UnhookWinEvent(hk1);
    if (hk2) UnhookWinEvent(hk2);
    DestroyWindow(g_engWnd);
    return 0;
}

void engine_start(void)
{
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_thread = CreateThread(NULL, 0, eng_thread, ready, 0, &g_tid);
    if (g_thread) WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
}

void engine_stop(void)
{
    if (!g_thread) return;
    PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = NULL;
}

void engine_config_changed(void) { if (g_engWnd) PostMessageW(g_engWnd, WM_ENG_CONFIG, 0, 0); }
void engine_apply_all(void)      { if (g_engWnd) PostMessageW(g_engWnd, WM_ENG_APPLYALL, 0, 0); }
