/* ==================================================================
 * ui_main.c - 設定画面(ルールの一覧と全般の設定)
 *
 *  変更はその場で kotemado.ini に保存し、監視にもすぐ効かせる。
 *  「OK / 適用」の区別は設けない。
 * ================================================================== */

#include "kotemado.h"
#include "resource.h"

static HWND    g_main;
static DlgLook g_look;
static BOOL    g_filling;       /* 一覧を作り直している間はチェックの変化を無視する */

static const int k_headings[] = { IDC_H_RULES, IDC_H_GENERAL };

static int  selected(void) { return ListView_GetNextItem(GetDlgItem(g_main, IDC_LIST), -1, LVNI_SELECTED); }

static void update_buttons(void)
{
    int s = selected(), n = g_cfg.count;
    EnableWindow(GetDlgItem(g_main, IDC_EDITRULE), s >= 0);
    EnableWindow(GetDlgItem(g_main, IDC_DUP),      s >= 0);
    EnableWindow(GetDlgItem(g_main, IDC_DEL),      s >= 0);
    EnableWindow(GetDlgItem(g_main, IDC_UP),       s > 0);
    EnableWindow(GetDlgItem(g_main, IDC_DOWN),     s >= 0 && s < n - 1);
}

static void fill_list(int sel)
{
    HWND   lv = GetDlgItem(g_main, IDC_LIST);
    WCHAR  buf[512];
    LVITEMW it;
    int    i;

    g_filling = TRUE;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    for (i = 0; i < g_cfg.count; i++) {
        const Rule *r = &g_cfg.rules[i];
        ZeroMemory(&it, sizeof(it));
        it.mask    = LVIF_TEXT;
        it.iItem   = i;
        it.pszText = r->name[0] ? (WCHAR *)r->name : L"（名前なし）";
        ListView_InsertItem(lv, &it);
        ui_cond_summary(r, buf, ARRAYSIZE(buf));
        ListView_SetItemText(lv, i, 1, buf);
        ui_place_summary(r, buf, ARRAYSIZE(buf));
        ListView_SetItemText(lv, i, 2, buf);
        ListView_SetCheckState(lv, i, r->enabled);
    }
    if (sel >= g_cfg.count) sel = g_cfg.count - 1;
    if (sel >= 0) {
        ListView_SetItemState(lv, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(lv, sel, FALSE);
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, NULL, TRUE);
    g_filling = FALSE;
    update_buttons();
}

static void setup_list(void)
{
    HWND      lv = GetDlgItem(g_main, IDC_LIST);
    LVCOLUMNW col;
    RECT      rc;
    int       w;

    ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                          LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    GetClientRect(lv, &rc);
    w = rc.right - GetSystemMetrics(SM_CXVSCROLL);

    ZeroMemory(&col, sizeof(col));
    col.mask    = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = L"名前";     col.cx = w * 26 / 100; ListView_InsertColumn(lv, 0, &col);
    col.pszText = L"条件";     col.cx = w * 40 / 100; ListView_InsertColumn(lv, 1, &col);
    col.pszText = L"配置";     col.cx = w - w * 66 / 100; ListView_InsertColumn(lv, 2, &col);
}

/* ------------------------------------------------------------------ */

static void do_add(void)
{
    Rule r;
    int  s = selected();
    rule_defaults(&r);
    if (!ui_edit_rule(g_main, &r, TRUE)) return;
    s = s >= 0 ? s + 1 : g_cfg.count;
    config_insert_rule(s, &r);
    fill_list(s);
}

static void do_edit(void)
{
    Rule r;
    int  s = selected();
    if (s < 0) return;
    r = g_cfg.rules[s];
    if (!ui_edit_rule(g_main, &r, FALSE)) return;
    config_replace_rule(s, &r);
    fill_list(s);
}

static void do_dup(void)
{
    Rule r;
    int  s = selected();
    if (s < 0) return;
    r = g_cfg.rules[s];
    r.id = rule_new_id();
    if (lstrlenW(r.name) + 6 < NAME_MAX_) lstrcatW(r.name, L" のコピー");
    config_insert_rule(s + 1, &r);
    fill_list(s + 1);
}

static void do_delete(void)
{
    WCHAR msg[160];
    int   s = selected();
    if (s < 0) return;
    wsprintfW(msg, L"ルール「%s」を削除しますか？",
              g_cfg.rules[s].name[0] ? g_cfg.rules[s].name : L"（名前なし）");
    if (ui_message(g_main, msg, NULL, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, NULL) != IDYES) return;
    config_delete_rule(s);
    fill_list(s);
}

static void do_move(int d)
{
    int s = selected();
    if (s < 0 || s + d < 0 || s + d >= g_cfg.count) return;
    config_swap_rules(s, s + d);
    fill_list(s + d);
    SetFocus(GetDlgItem(g_main, IDC_LIST));
}

/* ------------------------------------------------------------------ */

static void set_icons(HWND h)
{
    HICON big = NULL, small = NULL;
    LoadIconMetric(g_inst, MAKEINTRESOURCEW(IDI_APP), LIM_LARGE, &big);
    LoadIconMetric(g_inst, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &small);
    if (big)   SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (small) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

static INT_PTR CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        WCHAR t[MAX_PATH + 32];
        g_main = h;
        wsprintfW(t, L"%s %s", APP_NAME, APP_VERSION);
        SetWindowTextW(h, t);
        set_icons(h);
        dlg_look_init(h, &g_look, k_headings, ARRAYSIZE(k_headings), IDCANCEL);
        setup_list();
        fill_list(g_cfg.count ? 0 : -1);
        CheckDlgButton(h, IDC_APPLYEXIST, g_cfg.applyExisting ? BST_CHECKED : BST_UNCHECKED);
        wsprintfW(t, L"設定ファイル: %s", g_iniPath);
        SetDlgItemTextW(h, IDC_INIPATH, t);
        theme_apply_dialog(h);
        return TRUE;
    }

    case WM_DPICHANGED:
        PostMessageW(h, WM_APP_RELOOK, 0, 0);
        return FALSE;
    case WM_APP_RELOOK:
        dlg_look_free(&g_look);
        dlg_look_init(h, &g_look, k_headings, ARRAYSIZE(k_headings), IDCANCEL);
        InvalidateRect(h, NULL, TRUE);
        return TRUE;

    case WM_ERASEBKGND:
        dlg_look_erase(h, (HDC)wp, &g_look);
        SetWindowLongPtrW(h, DWLP_MSGRESULT, 1);
        return TRUE;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        int id = GetDlgCtrlID((HWND)lp);
        return (INT_PTR)theme_ctlcolor(msg, (HDC)wp, (HWND)lp,
                                       id == IDC_HINT_RULES || id == IDC_INIPATH);
    }

    case WM_NOTIFY: {
        NMHDR  *n = (NMHDR *)lp;
        LRESULT res;
        if (n->code == NM_CUSTOMDRAW && theme_custom_draw_button((NMCUSTOMDRAW *)lp, &res)) {
            SetWindowLongPtrW(h, DWLP_MSGRESULT, res);
            return TRUE;
        }
        if (n->idFrom != IDC_LIST) break;
        switch (n->code) {
        case LVN_ITEMCHANGED: {
            NMLISTVIEW *v = (NMLISTVIEW *)lp;
            if (g_filling) break;
            if ((v->uChanged & LVIF_STATE) && ((v->uNewState ^ v->uOldState) & LVIS_STATEIMAGEMASK) &&
                v->iItem >= 0 && v->iItem < g_cfg.count)
                config_set_enabled(v->iItem, ListView_GetCheckState(n->hwndFrom, v->iItem));
            update_buttons();
            break;
        }
        case NM_DBLCLK:
            if (((NMITEMACTIVATE *)lp)->iItem >= 0) do_edit();
            break;
        case LVN_KEYDOWN:
            if (((NMLVKEYDOWN *)lp)->wVKey == VK_DELETE) do_delete();
            break;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:          /* Enter。一覧にいれば編集 */
            if (GetFocus() == GetDlgItem(h, IDC_LIST)) do_edit();
            return TRUE;
        case IDCANCEL:      DestroyWindow(h); return TRUE;
        case IDC_ADD:       do_add();    return TRUE;
        case IDC_EDITRULE:  do_edit();   return TRUE;
        case IDC_DUP:       do_dup();    return TRUE;
        case IDC_DEL:       do_delete(); return TRUE;
        case IDC_UP:        do_move(-1); return TRUE;
        case IDC_DOWN:      do_move(+1); return TRUE;
        case IDC_IDENTIFY:  ui_identify_displays(); return TRUE;
        case IDC_APPLYNOW:  engine_apply_all();     return TRUE;
        case IDC_APPLYEXIST:
            config_set_apply_existing(IsDlgButtonChecked(h, IDC_APPLYEXIST) == BST_CHECKED);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(h);
        return TRUE;

    case WM_DESTROY:
        dlg_look_free(&g_look);
        RemovePropW(h, L"kotemado.footer");
        g_main = NULL;
        return TRUE;
    }
    return FALSE;
}

void ui_open_main(void)
{
    if (!g_main) {
        CreateDialogParamW(g_inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, main_proc, 0);
        if (!g_main) return;
        ShowWindow(g_main, SW_SHOW);
    } else if (IsIconic(g_main)) {
        ShowWindow(g_main, SW_RESTORE);
    }
    SetForegroundWindow(GetLastActivePopup(g_main));
}

BOOL ui_dialog_message(MSG *m)
{
    return g_main && IsDialogMessageW(g_main, m);
}

extern HWND ui_rule_dialog(void);

void ui_theme_changed(void)
{
    HWND r = ui_rule_dialog();
    if (g_main) theme_apply_dialog(g_main);
    if (r)      theme_apply_dialog(r);
}
