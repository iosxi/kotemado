/* ==================================================================
 * kotemado.h - 共通定義
 * ================================================================== */
#ifndef KOTEMADO_H
#define KOTEMADO_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef  WINVER
#define WINVER       0x0A00

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#define APP_NAME     L"kotemado"
#define APP_VERSION  L"1.0.0"

/* ------------------------------------------------------------------ */
/*  ルール                                                             */
/* ------------------------------------------------------------------ */

#define COND_MAX  260
#define NAME_MAX_ 64

/* 文字列の一致方法 */
enum { M_ANY, M_EXACT, M_PREFIX, M_SUFFIX, M_CONTAINS, M_WILDCARD, M_COUNT };
/* 判定に使う項目 */
enum { F_EXE, F_CLASS, F_TITLE, F_COUNT };
/* 位置・大きさの値の種類 */
enum { V_KEEP, V_PX, V_PCT, V_CENTER };
/* ウィンドウの状態 */
enum { ST_KEEP, ST_NORMAL, ST_MAX, ST_MIN };
/* 最前面 */
enum { TOP_KEEP, TOP_ON, TOP_OFF };
/* 適用するタイミング */
enum { WHEN_ONCE, WHEN_SHOW };

/* display の特別な値。1 以上は \\.\DISPLAYn の n */
#define DISP_MAIN     0
#define DISP_CURRENT  (-1)

typedef struct {
    int   mode;
    WCHAR text[COND_MAX];
    WCHAR low[COND_MAX];    /* 照合用に小文字化したもの(rule_prepare が作る) */
} Cond;

typedef struct {
    int kind;   /* V_* */
    int v;      /* V_PX はピクセル、V_PCT は百分率 */
} Val;

typedef struct {
    UINT  id;               /* 実行中だけの通し番号(ini には書かない) */
    BOOL  enabled;
    WCHAR name[NAME_MAX_];
    Cond  cond[F_COUNT];
    int   display;          /* DISP_MAIN / DISP_CURRENT / 1.. */
    BOOL  work;             /* 基準を作業領域(タスクバーを除く)にする */
    BOOL  visframe;         /* 見た目の枠で合わせる */
    Val   x, y, w, h;
    int   state, topmost, when, delay;
} Rule;

typedef struct {
    Rule *rules;
    int   count, cap;
    BOOL  applyExisting;    /* 起動時に開いているウィンドウにも適用する */
    BOOL  log;
    int   theme;            /* 0 = システムに合わせる / 1 = ライト / 2 = ダーク(ini のみ) */
} Config;

/* ------------------------------------------------------------------ */
/*  main.c                                                             */
/* ------------------------------------------------------------------ */

extern HINSTANCE g_inst;
extern WCHAR     g_exePath[MAX_PATH];
extern WCHAR     g_iniPath[MAX_PATH];
extern BOOL      g_customIni;       /* -ini で指定された */
extern HWND      g_trayWnd;

#define WM_APP_TRAY      (WM_APP + 1)
#define WM_APP_COMMAND   (WM_APP + 2)   /* 別インスタンスからの指示 */
#define CMD_SETTINGS     1
#define CMD_EXIT         2
#define CMD_APPLY        3

void log_printf(const WCHAR *fmt, ...);
void app_set_paused(BOOL on);
BOOL app_paused(void);
void tray_update(void);

/* スタートアップフォルダのショートカット(レジストリは使わない) */
BOOL startup_enabled(void);
BOOL startup_set(BOOL on);

/* ------------------------------------------------------------------ */
/*  config.c                                                           */
/* ------------------------------------------------------------------ */

extern Config  g_cfg;
extern SRWLOCK g_cfgLock;   /* 書き換えは UI スレッドだけ。書くときに排他を取る */

void  config_load(void);
BOOL  config_save(void);
void  rule_defaults(Rule *r);
void  rule_prepare(Rule *r);
UINT  rule_new_id(void);
BOOL  val_parse(const WCHAR *s, Val *v, BOOL allowCenter);
void  val_format(const Val *v, WCHAR *buf, int cch);

/* 以下は排他を取ったうえで g_cfg を書き換え、保存する */
void  config_insert_rule(int at, const Rule *r);
void  config_replace_rule(int at, const Rule *r);
void  config_delete_rule(int at);
void  config_swap_rules(int a, int b);
void  config_set_enabled(int at, BOOL on);
void  config_set_apply_existing(BOOL on);

/* ------------------------------------------------------------------ */
/*  engine.c                                                           */
/* ------------------------------------------------------------------ */

/* 照合の材料。必要になった項目だけ取りに行く */
typedef struct {
    HWND  hwnd;
    BYTE  have[F_COUNT];
    WCHAR val[F_COUNT][COND_MAX];   /* 小文字化済み */
    WCHAR raw[F_COUNT][COND_MAX];   /* 表示用 */
    WCHAR path[MAX_PATH];           /* 実行ファイルのフルパス(小文字) */
} WinInfo;

void         wininfo_init(WinInfo *wi, HWND hwnd);
const WCHAR *wininfo_raw(WinInfo *wi, int field);
BOOL         rule_match(const Rule *r, WinInfo *wi);

/* rule_apply の戻り値 */
enum { AP_OK, AP_GONE, AP_HUNG, AP_NODISPLAY, AP_FAILED };
int          rule_apply(HWND hwnd, const Rule *r);

void engine_start(void);
void engine_stop(void);
void engine_config_changed(void);
void engine_apply_all(void);        /* 開いているウィンドウすべてに今すぐ適用 */

/* モニタ */
typedef struct {
    HMONITOR h;
    int      num;           /* \\.\DISPLAYn の n。取れなければ 0 */
    BOOL     primary;
    RECT     rcMon, rcWork;
    UINT     dpi;
    WCHAR    name[64];      /* 表示用の名前 */
} MonInfo;

#define MON_MAX 16
int      monitors_get(MonInfo *out, int max);
BOOL     monitor_info(HMONITOR h, MonInfo *out);
BOOL     window_visible_rect(HWND h, RECT *out);

/* ------------------------------------------------------------------ */
/*  theme.c                                                            */
/* ------------------------------------------------------------------ */

void     theme_init(void);
BOOL     theme_refresh(void);
BOOL     theme_is_dark(void);
COLORREF theme_back(void);
COLORREF theme_footer(void);
COLORREF theme_ctrl_back(void);
COLORREF theme_text(void);
COLORREF theme_dim_text(void);
COLORREF theme_line(void);
HBRUSH   theme_back_brush(void);
HBRUSH   theme_footer_brush(void);
HBRUSH   theme_ctrl_brush(void);
void     theme_allow_dark(HWND hwnd);
void     theme_apply_dialog(HWND dlg);
LRESULT  theme_ctlcolor(UINT msg, HDC dc, HWND ctl, BOOL dimText);
BOOL     theme_custom_draw_button(NMCUSTOMDRAW *cd, LRESULT *result);

/* ------------------------------------------------------------------ */
/*  ui_*.c                                                             */
/* ------------------------------------------------------------------ */

void ui_open_main(void);
BOOL ui_dialog_message(MSG *msg);
void ui_theme_changed(void);
void ui_identify_displays(void);
BOOL ui_edit_rule(HWND owner, Rule *r, BOOL isNew);

/* 見出し・余白などダイアログ共通の見た目 */
typedef struct {
    HFONT heading;
    int   footerTop;        /* フッタ帯の上端(クライアント座標) */
} DlgLook;

void dlg_look_init(HWND dlg, DlgLook *lk, const int *headingIds, int nHeadings, int footerAnchorId);
void dlg_look_free(DlgLook *lk);
BOOL dlg_look_erase(HWND dlg, HDC dc, const DlgLook *lk);
int  ui_message(HWND owner, const WCHAR *main, const WCHAR *content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon);
void ui_cond_summary(const Rule *r, WCHAR *buf, int cch);
void ui_place_summary(const Rule *r, WCHAR *buf, int cch);

/* 照準でなぞっているウィンドウを囲む枠 */
void highlight_show(const RECT *r);
void highlight_hide(void);

#define WM_APP_RELOOK    (WM_APP + 20)  /* DPI が変わったあと見出しの文字を作り直す */
#define WM_APP_PICKED    (WM_APP + 21)  /* 照準でウィンドウを選んだ */

#endif
