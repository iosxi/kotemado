/* ==================================================================
 * ui_rule.c - ルールの編集
 *
 *  照準(左上の丸)をクリックし、続けてウィンドウをクリックすると、その
 *  プロセス名・クラス名・キャプションを入れる。新しいルールなら、
 *  そのウィンドウの今の位置と大きさも入れる。
 *  「先にウィンドウを置きたい場所へ置いてから照準を当てる」だけで
 *  ルールができる、という流れを想定している。
 * ================================================================== */

#include "kotemado.h"
#include "resource.h"
#include <shlwapi.h>

typedef struct {
    Rule    r;
    BOOL    isNew;
    HWND    picked;         /* 照準で選んだウィンドウ */
    HWND    hover;          /* 選択中にマウスの下にあるウィンドウ */
    BOOL    picking;        /* 照準をクリックして、選ぶウィンドウを待っている */
    DlgLook look;
} RuleDlg;

static HWND g_ruleDlg;
HWND ui_rule_dialog(void) { return g_ruleDlg; }

static const int k_headings[] = { IDC_H_TARGET, IDC_H_PLACE, IDC_H_TIMING };
static const int k_modeIds[F_COUNT] = { IDC_M_EXE, IDC_M_CLASS, IDC_M_TITLE };
static const int k_textIds[F_COUNT] = { IDC_T_EXE, IDC_T_CLASS, IDC_T_TITLE };

static void combo_add(HWND dlg, int id, const WCHAR *text, LPARAM data)
{
    int i = (int)SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, (LPARAM)text);
    SendDlgItemMessageW(dlg, id, CB_SETITEMDATA, i, data);
}

static void combo_select_data(HWND dlg, int id, LPARAM data)
{
    int i, n = (int)SendDlgItemMessageW(dlg, id, CB_GETCOUNT, 0, 0);
    for (i = 0; i < n; i++)
        if (SendDlgItemMessageW(dlg, id, CB_GETITEMDATA, i, 0) == data) {
            SendDlgItemMessageW(dlg, id, CB_SETCURSEL, i, 0);
            return;
        }
}

static LPARAM combo_data(HWND dlg, int id)
{
    int i = (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return i < 0 ? 0 : SendDlgItemMessageW(dlg, id, CB_GETITEMDATA, i, 0);
}

static void set_val(HWND dlg, int id, const Val *v)
{
    WCHAR b[16];
    val_format(v, b, ARRAYSIZE(b));
    SetDlgItemTextW(dlg, id, b);
}

static void fill_displays(HWND dlg, int want)
{
    MonInfo m[MON_MAX];
    WCHAR   t[160];
    int     i, n;
    BOOL    found = want < 1;

    SendDlgItemMessageW(dlg, IDC_DISPLAY, CB_RESETCONTENT, 0, 0);
    combo_add(dlg, IDC_DISPLAY, L"ウィンドウが今あるディスプレイ（移さない）", DISP_CURRENT);
    combo_add(dlg, IDC_DISPLAY, L"メイン ディスプレイ", DISP_MAIN);
    n = monitors_get(m, MON_MAX);
    for (i = 0; i < n; i++) {
        if (m[i].num < 1) continue;
        wsprintfW(t, L"%d:  %s　%d × %d%s", m[i].num, m[i].name,
                  m[i].rcMon.right - m[i].rcMon.left, m[i].rcMon.bottom - m[i].rcMon.top,
                  m[i].primary ? L"（メイン）" : L"");
        combo_add(dlg, IDC_DISPLAY, t, m[i].num);
        if (m[i].num == want) found = TRUE;
    }
    if (!found) {
        wsprintfW(t, L"%d:  （今は接続されていません）", want);
        combo_add(dlg, IDC_DISPLAY, t, want);
    }
    combo_select_data(dlg, IDC_DISPLAY, want);
}

static void sync_enable(HWND dlg)
{
    int f;
    for (f = 0; f < F_COUNT; f++)
        EnableWindow(GetDlgItem(dlg, k_textIds[f]), combo_data(dlg, k_modeIds[f]) != M_ANY);
}

/* 間隔は「定期的に」のときだけ使う */
static void sync_when(HWND dlg)
{
    BOOL on = combo_data(dlg, IDC_WHEN) == WHEN_PERIODIC;
    EnableWindow(GetDlgItem(dlg, IDC_INTERVAL), on);
    EnableWindow(GetDlgItem(dlg, IDC_INTERVAL_LBL), on);
    EnableWindow(GetDlgItem(dlg, IDC_INTERVAL_UNIT), on);
}

static void load_fields(HWND dlg, const Rule *r)
{
    static const WCHAR *const modes[M_COUNT] = {
        L"指定しない", L"完全一致", L"前方一致", L"後方一致", L"部分一致", L"ワイルドカード"
    };
    int f, m;

    SetDlgItemTextW(dlg, IDC_NAME, r->name);
    SendDlgItemMessageW(dlg, IDC_NAME, EM_SETLIMITTEXT, NAME_MAX_ - 1, 0);
    CheckDlgButton(dlg, IDC_ENABLED, r->enabled ? BST_CHECKED : BST_UNCHECKED);

    for (f = 0; f < F_COUNT; f++) {
        for (m = 0; m < M_COUNT; m++) combo_add(dlg, k_modeIds[f], modes[m], m);
        combo_select_data(dlg, k_modeIds[f], r->cond[f].mode);
        SetDlgItemTextW(dlg, k_textIds[f], r->cond[f].text);
        SendDlgItemMessageW(dlg, k_textIds[f], EM_SETLIMITTEXT, COND_MAX - 1, 0);
    }
    sync_enable(dlg);

    fill_displays(dlg, r->display);
    combo_add(dlg, IDC_BASE, L"画面全体", 0);
    combo_add(dlg, IDC_BASE, L"作業領域（タスクバーを除く）", 1);
    combo_select_data(dlg, IDC_BASE, r->work ? 1 : 0);

    set_val(dlg, IDC_X, &r->x);
    set_val(dlg, IDC_Y, &r->y);
    set_val(dlg, IDC_W, &r->w);
    set_val(dlg, IDC_HGT, &r->h);
    CheckDlgButton(dlg, IDC_VISFRAME, r->visframe ? BST_CHECKED : BST_UNCHECKED);

    combo_add(dlg, IDC_STATE, L"変えない", ST_KEEP);
    combo_add(dlg, IDC_STATE, L"通常", ST_NORMAL);
    combo_add(dlg, IDC_STATE, L"最大化", ST_MAX);
    combo_add(dlg, IDC_STATE, L"最小化", ST_MIN);
    combo_select_data(dlg, IDC_STATE, r->state);

    combo_add(dlg, IDC_TOPMOST, L"変えない", TOP_KEEP);
    combo_add(dlg, IDC_TOPMOST, L"最前面に固定する", TOP_ON);
    combo_add(dlg, IDC_TOPMOST, L"最前面を解除する", TOP_OFF);
    combo_select_data(dlg, IDC_TOPMOST, r->topmost);

    combo_add(dlg, IDC_WHEN, L"ウィンドウごとに最初の 1 回だけ", WHEN_ONCE);
    combo_add(dlg, IDC_WHEN, L"定期的に（ずれたら戻す）", WHEN_PERIODIC);
    combo_select_data(dlg, IDC_WHEN, r->when);
    SetDlgItemInt(dlg, IDC_INTERVAL, (UINT)r->interval, FALSE);
    SendDlgItemMessageW(dlg, IDC_INTERVAL, EM_SETLIMITTEXT, 6, 0);
    SetDlgItemInt(dlg, IDC_DELAY, (UINT)r->delay, FALSE);
    SendDlgItemMessageW(dlg, IDC_DELAY, EM_SETLIMITTEXT, 5, 0);
    sync_when(dlg);
}

static BOOL bad(HWND dlg, int id, const WCHAR *msg)
{
    ui_message(dlg, msg, NULL, 0, TD_WARNING_ICON);
    SetFocus(GetDlgItem(dlg, id));
    SendDlgItemMessageW(dlg, id, EM_SETSEL, 0, -1);
    return FALSE;
}

/* 画面の内容を out に読み取る。誤りがあれば知らせて FALSE */
static BOOL collect(HWND dlg, Rule *out)
{
    static const struct { int id; BOOL center; const WCHAR *what; } vals[] = {
        { IDC_X, TRUE, L"X 座標" }, { IDC_Y, TRUE, L"Y 座標" },
        { IDC_W, FALSE, L"幅" },    { IDC_HGT, FALSE, L"高さ" },
    };
    Val  *dst[4];
    WCHAR t[64], msg[160];
    int   f, i, any = 0;
    BOOL  ok;
    UINT  d;

    GetDlgItemTextW(dlg, IDC_NAME, out->name, NAME_MAX_);
    out->enabled = IsDlgButtonChecked(dlg, IDC_ENABLED) == BST_CHECKED;
    for (f = 0; f < F_COUNT; f++) {
        out->cond[f].mode = (int)combo_data(dlg, k_modeIds[f]);
        GetDlgItemTextW(dlg, k_textIds[f], out->cond[f].text, COND_MAX);
        if (out->cond[f].mode != M_ANY) any++;
    }
    if (!any) return bad(dlg, IDC_M_EXE,
        L"プロセス名・クラス名・キャプションのうち、少なくとも 1 つを指定してください。");
    if (out->cond[F_EXE].mode != M_ANY && !out->cond[F_EXE].text[0])
        return bad(dlg, IDC_T_EXE, L"プロセス名が空欄です。");
    if (out->cond[F_CLASS].mode != M_ANY && !out->cond[F_CLASS].text[0])
        return bad(dlg, IDC_T_CLASS, L"クラス名が空欄です。");
    rule_prepare(out);

    out->display = (int)combo_data(dlg, IDC_DISPLAY);
    out->work    = combo_data(dlg, IDC_BASE) == 1;

    dst[0] = &out->x; dst[1] = &out->y; dst[2] = &out->w; dst[3] = &out->h;
    for (i = 0; i < 4; i++) {
        GetDlgItemTextW(dlg, vals[i].id, t, ARRAYSIZE(t));
        if (!val_parse(t, dst[i], vals[i].center)) {
            wsprintfW(msg, L"%s の指定が読み取れません。", vals[i].what);
            return bad(dlg, vals[i].id, msg);
        }
        if (i >= 2 && dst[i]->kind == V_PX && dst[i]->v < 1) {
            wsprintfW(msg, L"%s は 1 以上にしてください。", vals[i].what);
            return bad(dlg, vals[i].id, msg);
        }
    }
    out->visframe = IsDlgButtonChecked(dlg, IDC_VISFRAME) == BST_CHECKED;
    out->state    = (int)combo_data(dlg, IDC_STATE);
    out->topmost  = (int)combo_data(dlg, IDC_TOPMOST);
    out->when     = (int)combo_data(dlg, IDC_WHEN);
    d = GetDlgItemInt(dlg, IDC_INTERVAL, &ok, FALSE);
    if (out->when == WHEN_PERIODIC && (!ok || d < INTERVAL_MIN || d > INTERVAL_MAX)) {
        wsprintfW(msg, L"間隔は %d〜%d ミリ秒で指定してください。", INTERVAL_MIN, INTERVAL_MAX);
        return bad(dlg, IDC_INTERVAL, msg);
    }
    if (ok && d >= INTERVAL_MIN && d <= INTERVAL_MAX) out->interval = (int)d;
    d = GetDlgItemInt(dlg, IDC_DELAY, &ok, FALSE);
    if (!ok) d = 0;
    if (d > 60000) return bad(dlg, IDC_DELAY, L"遅延は 60000 ミリ秒までです。");
    out->delay = (int)d;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  開いているウィンドウを調べる                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const Rule *r;
    HWND        found[64];
    int         n, total;
} Scan;

static BOOL CALLBACK scan_cb(HWND h, LPARAM lp)
{
    Scan   *s = (Scan *)lp;
    WinInfo wi;
    DWORD   pid = 0;

    if (!IsWindowVisible(h)) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;
    wininfo_init(&wi, h);
    if (rule_match(s->r, &wi)) {
        if (s->n < (int)ARRAYSIZE(s->found)) s->found[s->n++] = h;
        s->total++;
    }
    return TRUE;
}

static void describe(HWND h, WCHAR *buf, int cch)
{
    WinInfo      wi;
    const WCHAR *t;
    wininfo_init(&wi, h);
    t = wininfo_raw(&wi, F_TITLE);
    wnsprintfW(buf, cch, L"%s ｜ %s ｜ %s", wininfo_raw(&wi, F_EXE), wininfo_raw(&wi, F_CLASS),
               t[0] ? t : L"（キャプションなし）");
}

static void do_check(HWND dlg, RuleDlg *d)
{
    Scan  s;
    Rule  r = d->r;
    WCHAR body[4096], line[600], head[96];
    int   i;

    if (!collect(dlg, &r)) return;
    ZeroMemory(&s, sizeof(s));
    s.r = &r;
    EnumWindows(scan_cb, (LPARAM)&s);
    if (!s.total) {
        ui_message(dlg, L"今開いているウィンドウには、該当するものがありません。",
                   L"表示されていないウィンドウは数えていません。", 0, TD_INFORMATION_ICON);
        return;
    }
    body[0] = 0;
    for (i = 0; i < s.n && i < 15; i++) {
        describe(s.found[i], line, ARRAYSIZE(line));
        if (lstrlenW(body) + lstrlenW(line) + 2 >= (int)ARRAYSIZE(body)) break;
        lstrcatW(body, line);
        lstrcatW(body, L"\n");
    }
    if (s.total > i) {
        wsprintfW(line, L"ほか %d 個", s.total - i);
        lstrcatW(body, line);
    }
    wsprintfW(head, L"%d 個のウィンドウが該当します。", s.total);
    ui_message(dlg, head, body, 0, TD_INFORMATION_ICON);
}

static void do_try(HWND dlg, RuleDlg *d)
{
    Scan  s;
    Rule  r = d->r;
    WCHAR head[96];
    int   i, ok = 0, fail = 0, nodisp = 0;

    if (!collect(dlg, &r)) return;
    ZeroMemory(&s, sizeof(s));
    s.r = &r;
    EnumWindows(scan_cb, (LPARAM)&s);
    for (i = 0; i < s.n; i++) {
        switch (rule_apply(s.found[i], &r)) {
        case AP_OK:
        case AP_SAME:      ok++;     break;
        case AP_NODISPLAY: nodisp++; break;
        case AP_GONE:      break;
        default:           fail++;   break;
        }
    }
    if (!s.total) {
        ui_message(dlg, L"該当するウィンドウがありません。", NULL, 0, TD_INFORMATION_ICON);
        return;
    }
    wsprintfW(head, L"%d 個のウィンドウに適用しました。", ok);
    ui_message(dlg, head,
               nodisp ? L"指定したディスプレイが今は接続されていないため、動かせなかったものがあります。" :
               fail   ? L"動かせなかったものがあります。管理者として実行しているウィンドウは、"
                        L"kotemado も管理者として実行しないと動かせません。" : NULL,
               0, (fail || nodisp) ? TD_WARNING_ICON : TD_INFORMATION_ICON);
}

/* ------------------------------------------------------------------ */
/*  ウィンドウから取り込む                                              */
/* ------------------------------------------------------------------ */

static void grab_position(HWND dlg, HWND w)
{
    WINDOWPLACEMENT wp;
    MonInfo m;
    RECT    vis, base;
    Val     v;
    BOOL    work = combo_data(dlg, IDC_BASE) == 1;
    BOOL    visframe = IsDlgButtonChecked(dlg, IDC_VISFRAME) == BST_CHECKED;

    wp.length = sizeof(wp);
    if (!GetWindowPlacement(w, &wp)) return;
    if (IsIconic(w) || IsZoomed(w)) {
        /* 最大化・最小化中は「元に戻したときの」位置を使う */
        MONITORINFO pm;
        POINT       z = { 0, 0 };
        vis = wp.rcNormalPosition;
        if (!(GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
            pm.cbSize = sizeof(pm);
            GetMonitorInfoW(MonitorFromPoint(z, MONITOR_DEFAULTTOPRIMARY), &pm);
            OffsetRect(&vis, pm.rcWork.left - pm.rcMonitor.left, pm.rcWork.top - pm.rcMonitor.top);
        }
        combo_select_data(dlg, IDC_STATE, IsZoomed(w) ? ST_MAX : ST_KEEP);
    } else if (visframe) {
        window_visible_rect(w, &vis);
    } else {
        GetWindowRect(w, &vis);
    }

    if (!monitor_info(MonitorFromRect(&vis, MONITOR_DEFAULTTONEAREST), &m)) return;
    base = work ? m.rcWork : m.rcMon;
    if (m.num >= 1) {
        fill_displays(dlg, m.num);
    }
    v.kind = V_PX;
    v.v = vis.left - base.left;          set_val(dlg, IDC_X, &v);
    v.v = vis.top - base.top;            set_val(dlg, IDC_Y, &v);
    v.v = vis.right - vis.left;          set_val(dlg, IDC_W, &v);
    v.v = vis.bottom - vis.top;          set_val(dlg, IDC_HGT, &v);
}

static void on_picked(HWND dlg, RuleDlg *d, HWND w)
{
    WinInfo wi;
    WCHAR   t[COND_MAX];
    int     f;

    d->picked = w;
    wininfo_init(&wi, w);
    for (f = 0; f < F_COUNT; f++) {
        SetDlgItemTextW(dlg, k_textIds[f], wininfo_raw(&wi, f));
        /* 3 項目とも完全一致で入れ、利用者に削ってもらう。キャプションが
           空なら「完全一致・空欄」= キャプションのないウィンドウの指定になる。
           すでに一致方法を選んであれば、それは変えない */
        if (combo_data(dlg, k_modeIds[f]) == M_ANY)
            combo_select_data(dlg, k_modeIds[f], M_EXACT);
    }
    sync_enable(dlg);

    GetDlgItemTextW(dlg, IDC_NAME, t, ARRAYSIZE(t));
    if (!t[0]) {
        const WCHAR *exe = wininfo_raw(&wi, F_EXE);
        int n;
        lstrcpynW(t, exe[0] ? exe : wininfo_raw(&wi, F_CLASS), NAME_MAX_);
        n = lstrlenW(t);
        if (n > 4 && !lstrcmpiW(t + n - 4, L".exe")) t[n - 4] = 0;
        SetDlgItemTextW(dlg, IDC_NAME, t);
    }

    if (d->isNew) {
        WCHAR a[4][16];
        GetDlgItemTextW(dlg, IDC_X, a[0], 16);  GetDlgItemTextW(dlg, IDC_Y, a[1], 16);
        GetDlgItemTextW(dlg, IDC_W, a[2], 16);  GetDlgItemTextW(dlg, IDC_HGT, a[3], 16);
        if (!a[0][0] && !a[1][0] && !a[2][0] && !a[3][0]) grab_position(dlg, w);
    }
}

static void do_grab(HWND dlg, RuleDlg *d)
{
    HWND w = (d->picked && IsWindow(d->picked)) ? d->picked : NULL;
    if (!w) {
        Scan s;
        Rule r = d->r;
        if (!collect(dlg, &r)) return;
        ZeroMemory(&s, sizeof(s));
        s.r = &r;
        EnumWindows(scan_cb, (LPARAM)&s);
        if (s.n) w = s.found[0];
    }
    if (!w) {
        ui_message(dlg, L"取り込むウィンドウがありません。",
                   L"照準でウィンドウを選ぶか、条件に合うウィンドウを開いてからやり直してください。",
                   0, TD_INFORMATION_ICON);
        return;
    }
    grab_position(dlg, w);
}

/* ------------------------------------------------------------------ */
/*  照準                                                                */
/* ------------------------------------------------------------------ */

static void show_hover_info(HWND dlg, HWND w)
{
    WCHAR line[600];
    if (w) describe(w, line, ARRAYSIZE(line));
    else   line[0] = 0;
    SetDlgItemTextW(dlg, IDC_FINDER_INFO, line);
}

/*  照準は 2 クリックで使う。照準をクリックすると「選択中」になり、
 *  次にクリックしたウィンドウを選ぶ。ボタンを離した後は SetCapture では
 *  ほかのアプリの上のマウスを受け取れないので、選択中だけ低レベル
 *  マウス フック(WH_MOUSE_LL)を掛けて動きを追う。選ぶためのクリックは、
 *  相手のアプリに届かないよう押下も離上も握りつぶす。
 *  右クリック・Esc・kotemado 自身の画面の上でのクリックで中止。
 *  フックはこのスレッド(設定画面)のメッセージ処理の中で呼ばれる。 */

#define WM_APP_PICKMOVE  (WM_APP + 22)
#define WM_APP_PICKCLICK (WM_APP + 23)   /* wParam: 1 = 左(選ぶ) / 0 = 右(中止)、lParam: 位置 */
#define WM_APP_UNHOOK    (WM_APP + 24)

static HHOOK g_mouseHook;
static HWND  g_hookDlg;         /* 知らせる先 */
static BOOL  g_hookPicking;     /* 選択中 */
static BOOL  g_swallowUp;       /* 握りつぶした押下に対応する離上も握りつぶす */
static BOOL  g_movePosted;      /* 動きの知らせが処理待ち(何通も溜めない) */

static LRESULT CALLBACK mouse_ll(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_hookDlg) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;
        switch (wp) {
        case WM_MOUSEMOVE:
            if (g_hookPicking && !g_movePosted) {
                g_movePosted = TRUE;
                PostMessageW(g_hookDlg, WM_APP_PICKMOVE, 0, 0);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (g_hookPicking) {
                g_hookPicking = FALSE;
                g_swallowUp   = TRUE;
                PostMessageW(g_hookDlg, WM_APP_PICKCLICK, wp == WM_LBUTTONDOWN,
                             MAKELPARAM((WORD)m->pt.x, (WORD)m->pt.y));
                return 1;
            }
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            if (g_swallowUp) {
                g_swallowUp = FALSE;
                PostMessageW(g_hookDlg, WM_APP_UNHOOK, 0, 0);
                return 1;
            }
            break;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static void pick_unhook(void)
{
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    g_mouseHook   = NULL;
    g_hookDlg     = NULL;
    g_hookPicking = FALSE;
    g_swallowUp   = FALSE;
}

/* pt の下にあるトップレベル ウィンドウ。kotemado 自身のものなら NULL */
static HWND window_at(POINT pt)
{
    HWND  w = WindowFromPoint(pt);
    DWORD pid = 0;
    if (w) w = GetAncestor(w, GA_ROOT);
    if (w) {
        GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId()) w = NULL;
    }
    return w;
}

#define FINDER_HINT L"左の照準をクリックし、続けて目的のウィンドウをクリックすると、下の欄が埋まります。"

static void pick_start(HWND dlg, RuleDlg *d)
{
    if (d->picking) return;
    if (!g_mouseHook) g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouse_ll, g_inst, 0);
    if (!g_mouseHook) {
        ui_message(dlg, L"ウィンドウを選ぶ準備ができませんでした。", NULL, 0, TD_ERROR_ICON);
        return;
    }
    g_hookDlg     = dlg;
    g_hookPicking = TRUE;
    g_swallowUp   = FALSE;
    g_movePosted  = FALSE;
    d->picking    = TRUE;
    d->hover      = NULL;
    SetFocus(GetDlgItem(dlg, IDC_FINDER));      /* Esc を受けるため */
    SetDlgItemTextW(dlg, IDC_FINDER_HINT,
                    L"選びたいウィンドウをクリックしてください。右クリックか Esc で中止します。");
    InvalidateRect(GetDlgItem(dlg, IDC_FINDER), NULL, TRUE);
}

/* 選択中を終える。chosen が NULL なら中止 */
static void pick_end(HWND dlg, RuleDlg *d, HWND chosen)
{
    if (!d->picking) return;
    d->picking    = FALSE;
    g_hookPicking = FALSE;
    if (!g_swallowUp) pick_unhook();            /* 握りつぶす離上が残っていれば、それを待って外す */
    highlight_hide();
    SetDlgItemTextW(dlg, IDC_FINDER_HINT, FINDER_HINT);
    show_hover_info(dlg, chosen);
    InvalidateRect(GetDlgItem(dlg, IDC_FINDER), NULL, TRUE);
    if (chosen && IsWindow(chosen)) on_picked(dlg, d, chosen);
}

static void pick_move(HWND dlg, RuleDlg *d)
{
    POINT pt;
    HWND  w;

    g_movePosted = FALSE;
    if (!d->picking) return;
    GetCursorPos(&pt);
    w = window_at(pt);
    if (w != d->hover) {
        RECT r;
        d->hover = w;
        if (w && window_visible_rect(w, &r)) highlight_show(&r);
        else highlight_hide();
        show_hover_info(dlg, w);
    }
}

static LRESULT CALLBACK finder_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    RuleDlg *d   = (RuleDlg *)ref;
    HWND     dlg = GetParent(h);

    switch (msg) {
    case WM_LBUTTONUP:              /* 1 回目のクリック。次のクリックで選ぶ */
        pick_start(dlg, d);
        return 0;

    case WM_KEYDOWN:
        if (d->picking && wp == VK_ESCAPE) {
            pick_end(dlg, d, NULL);
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        if (d->picking) return DLGC_WANTALLKEYS;
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(h, finder_proc, id);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* 照準の絵。選択中は照準がカーソルに移ったように見せるため、空の丸だけにする */
static void draw_finder(const DRAWITEMSTRUCT *di, BOOL dragging)
{
    RECT   r = di->rcItem;
    int    cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int    rad = min(r.right - r.left, r.bottom - r.top) / 2 - 2;
    int    pw  = max(1, rad / 8);
    HPEN   pen = CreatePen(PS_SOLID, pw, theme_text());
    HPEN   acc = CreatePen(PS_SOLID, pw, RGB(255, 72, 32));
    HGDIOBJ op, ob;

    FillRect(di->hDC, &r, theme_back_brush());
    op = SelectObject(di->hDC, dragging ? pen : acc);
    ob = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    Ellipse(di->hDC, cx - rad, cy - rad, cx + rad + 1, cy + rad + 1);
    if (!dragging) {
        int in = rad * 45 / 100;
        SelectObject(di->hDC, pen);
        MoveToEx(di->hDC, cx - rad - 1, cy, NULL); LineTo(di->hDC, cx - in, cy);
        MoveToEx(di->hDC, cx + in, cy, NULL);      LineTo(di->hDC, cx + rad + 2, cy);
        MoveToEx(di->hDC, cx, cy - rad - 1, NULL); LineTo(di->hDC, cx, cy - in);
        MoveToEx(di->hDC, cx, cy + in, NULL);      LineTo(di->hDC, cx, cy + rad + 2);
        SelectObject(di->hDC, acc);
        Ellipse(di->hDC, cx - pw, cy - pw, cx + pw + 1, cy + pw + 1);
    }
    SelectObject(di->hDC, ob);
    SelectObject(di->hDC, op);
    DeleteObject(pen);
    DeleteObject(acc);
}

/* ------------------------------------------------------------------ */

static INT_PTR CALLBACK rule_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    RuleDlg *d = (RuleDlg *)GetWindowLongPtrW(h, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        d = (RuleDlg *)lp;
        SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)d);
        g_ruleDlg = h;
        SetWindowTextW(h, d->isNew ? L"ルールの追加" : L"ルールの編集");
        dlg_look_init(h, &d->look, k_headings, ARRAYSIZE(k_headings), IDOK);
        load_fields(h, &d->r);
        SetWindowSubclass(GetDlgItem(h, IDC_FINDER), finder_proc, 1, (DWORD_PTR)d);
        theme_apply_dialog(h);
        return TRUE;

    case WM_DPICHANGED:
        PostMessageW(h, WM_APP_RELOOK, 0, 0);
        return FALSE;
    case WM_APP_RELOOK:
        dlg_look_free(&d->look);
        dlg_look_init(h, &d->look, k_headings, ARRAYSIZE(k_headings), IDOK);
        InvalidateRect(h, NULL, TRUE);
        return TRUE;

    case WM_ERASEBKGND:
        dlg_look_erase(h, (HDC)wp, &d->look);
        SetWindowLongPtrW(h, DWLP_MSGRESULT, 1);
        return TRUE;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        int id = GetDlgCtrlID((HWND)lp);
        BOOL dim = id == IDC_HINT_COND || id == IDC_HINT_POS || id == IDC_HINT_DELAY ||
                   id == IDC_FINDER_HINT;
        return (INT_PTR)theme_ctlcolor(msg, (HDC)wp, (HWND)lp, dim);
    }

    case WM_DRAWITEM:
        if (wp == IDC_FINDER) {
            draw_finder((const DRAWITEMSTRUCT *)lp, d && d->picking);
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        LRESULT res;
        if (((NMHDR *)lp)->code == NM_CUSTOMDRAW &&
            theme_custom_draw_button((NMCUSTOMDRAW *)lp, &res)) {
            SetWindowLongPtrW(h, DWLP_MSGRESULT, res);
            return TRUE;
        }
        break;
    }

    case WM_APP_PICKED:
        if (IsWindow((HWND)wp)) on_picked(h, d, (HWND)wp);
        return TRUE;

    case WM_APP_PICKMOVE:
        pick_move(h, d);
        return TRUE;
    case WM_APP_PICKCLICK: {
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        pick_end(h, d, wp ? window_at(pt) : NULL);
        return TRUE;
    }
    case WM_APP_UNHOOK:
        if (!d->picking) pick_unhook();
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_M_EXE: case IDC_M_CLASS: case IDC_M_TITLE:
            if (HIWORD(wp) == CBN_SELCHANGE) sync_enable(h);
            return TRUE;
        case IDC_WHEN:
            if (HIWORD(wp) == CBN_SELCHANGE) sync_when(h);
            return TRUE;
        case IDC_CHECK: do_check(h, d); return TRUE;
        case IDC_GRAB:  do_grab(h, d);  return TRUE;
        case IDC_TRY:   do_try(h, d);   return TRUE;
        case IDOK: {
            Rule r = d->r;
            if (!collect(h, &r)) return TRUE;
            if (!r.name[0]) {
                int f;
                for (f = 0; f < F_COUNT && !r.name[0]; f++)
                    if (r.cond[f].mode != M_ANY) lstrcpynW(r.name, r.cond[f].text, NAME_MAX_);
            }
            d->r = r;
            EndDialog(h, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        pick_unhook();
        highlight_hide();
        if (d) dlg_look_free(&d->look);
        RemovePropW(h, L"kotemado.footer");
        g_ruleDlg = NULL;
        break;
    }
    return FALSE;
}

BOOL ui_edit_rule(HWND owner, Rule *r, BOOL isNew)
{
    RuleDlg d;
    ZeroMemory(&d, sizeof(d));
    d.r     = *r;
    d.isNew = isNew;
    if (DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_RULE), owner, rule_proc, (LPARAM)&d) != IDOK)
        return FALSE;
    *r = d.r;
    return TRUE;
}
