/* ==================================================================
 * config.c - kotemado.ini の読み書き
 *
 *  レジストリは使わない。設定は exe と同じ場所の kotemado.ini
 *  (UTF-8)にだけ置く。手で書き換えやすいよう、ルールは [rule] の
 *  節を上から並べるだけの形にしてある(並び順 = 優先順)。
 *
 *      [rule]
 *      name=メモ帳
 *      exe=exact:notepad.exe       一致方法:文字列(最初の : で区切る)
 *      title=prefix:無題
 *      display=2                   main / current / 番号
 *      x=100  y=center  width=50%  空 = 変えない
 *
 *  GetPrivateProfileString は UTF-8 を扱えないので自前で読む。
 * ================================================================== */

#include "kotemado.h"

Config  g_cfg;
SRWLOCK g_cfgLock = SRWLOCK_INIT;

static UINT g_nextId = 1;
static BOOL g_logInIni;        /* ini に log=1 がある(-log の指定は書き戻さない) */

static const WCHAR *const k_modeNames[M_COUNT] = {
    L"any", L"exact", L"prefix", L"suffix", L"contains", L"wildcard"
};
static const WCHAR *const k_fieldKeys[F_COUNT] = { L"exe", L"class", L"title" };
static const WCHAR *const k_stateNames[] = { L"keep", L"normal", L"max", L"min" };
static const WCHAR *const k_topNames[]   = { L"keep", L"on", L"off" };
static const WCHAR *const k_whenNames[]  = { L"once", L"show" };

UINT rule_new_id(void) { return g_nextId++; }

void rule_defaults(Rule *r)
{
    ZeroMemory(r, sizeof(*r));
    r->id       = rule_new_id();
    r->enabled  = TRUE;
    r->display  = DISP_CURRENT;
    r->visframe = TRUE;
}

void rule_prepare(Rule *r)
{
    int i;
    for (i = 0; i < F_COUNT; i++) {
        lstrcpynW(r->cond[i].low, r->cond[i].text, COND_MAX);
        CharLowerBuffW(r->cond[i].low, lstrlenW(r->cond[i].low));
    }
}

/* ------------------------------------------------------------------ */
/*  値の解析と書式                                                      */
/* ------------------------------------------------------------------ */

static void trim(WCHAR *s)
{
    int n = lstrlenW(s), i = 0;
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' ||
                     s[n - 1] == 0x3000)) s[--n] = 0;
    while (s[i] == L' ' || s[i] == L'\t' || s[i] == 0x3000) i++;
    if (i) MoveMemory(s, s + i, (n - i + 1) * sizeof(WCHAR));
}

/* 全角数字・記号も受け付ける(日本語入力のまま打たれることが多い) */
static WCHAR narrow(WCHAR c)
{
    if (c >= 0xFF10 && c <= 0xFF19) return (WCHAR)(L'0' + (c - 0xFF10));
    if (c == 0xFF05) return L'%';
    if (c == 0xFF0D || c == 0x2212) return L'-';
    if (c == 0xFF0B) return L'+';
    return c;
}

/* "" / "123" / "-5" / "50%" / "center" / "中央" */
BOOL val_parse(const WCHAR *src, Val *v, BOOL allowCenter)
{
    WCHAR s[32];
    int   i, n, sign = 1, num = 0, digits = 0;

    v->kind = V_KEEP; v->v = 0;
    lstrcpynW(s, src ? src : L"", ARRAYSIZE(s));
    trim(s);
    if (!s[0]) return TRUE;

    if (!lstrcmpiW(s, L"center") || !lstrcmpW(s, L"中央")) {
        if (!allowCenter) return FALSE;
        v->kind = V_CENTER;
        return TRUE;
    }

    n = lstrlenW(s);
    for (i = 0; i < n; i++) s[i] = narrow(s[i]);

    i = 0;
    if (s[0] == L'-') { sign = -1; i++; }
    else if (s[0] == L'+') i++;
    for (; i < n && s[i] >= L'0' && s[i] <= L'9'; i++) {
        num = num * 10 + (s[i] - L'0');
        if (num > 100000) return FALSE;
        digits++;
    }
    if (!digits) return FALSE;
    if (i < n && s[i] == L'%') {
        if (i + 1 != n || sign < 0) return FALSE;
        v->kind = V_PCT; v->v = num;
        return TRUE;
    }
    if (i != n) return FALSE;
    v->kind = V_PX; v->v = sign * num;
    return TRUE;
}

void val_format(const Val *v, WCHAR *buf, int cch)
{
    switch (v->kind) {
    case V_PX:     wsprintfW(buf, L"%d", v->v);  break;
    case V_PCT:    wsprintfW(buf, L"%d%%", v->v); break;
    case V_CENTER: lstrcpynW(buf, L"中央", cch); break;
    default:       buf[0] = 0; break;
    }
}

static int find_name(const WCHAR *s, const WCHAR *const *names, int n, int def)
{
    int i;
    for (i = 0; i < n; i++) if (!lstrcmpiW(s, names[i])) return i;
    return def;
}

/* ------------------------------------------------------------------ */
/*  読み込み                                                            */
/* ------------------------------------------------------------------ */

static WCHAR *read_text_file(const WCHAR *path)
{
    HANDLE f;
    DWORD  size, got;
    char  *buf;
    WCHAR *w;
    int    off = 0, len;
    UINT   cp = CP_UTF8;

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > 4 * 1024 * 1024) { CloseHandle(f); return NULL; }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(f); return NULL; }
    if (!ReadFile(f, buf, size, &got, NULL)) got = 0;
    CloseHandle(f);
    buf[got] = 0;

    if (got >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) off = 3;
    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf + off, got - off, NULL, 0);
    if (len <= 0 && got - off > 0) {      /* UTF-8 でなければシステムの既定で読む */
        cp  = CP_ACP;
        len = MultiByteToWideChar(cp, 0, buf + off, got - off, NULL, 0);
    }
    w = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
    if (w) {
        if (len > 0) MultiByteToWideChar(cp, 0, buf + off, got - off, w, len);
        w[len > 0 ? len : 0] = 0;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return w;
}

static Rule *push_rule(Config *c)
{
    if (c->count == c->cap) {
        int   ncap = c->cap ? c->cap * 2 : 16;
        Rule *n = c->rules
            ? (Rule *)HeapReAlloc(GetProcessHeap(), 0, c->rules, ncap * sizeof(Rule))
            : (Rule *)HeapAlloc(GetProcessHeap(), 0, ncap * sizeof(Rule));
        if (!n) return NULL;
        c->rules = n; c->cap = ncap;
    }
    rule_defaults(&c->rules[c->count]);
    return &c->rules[c->count++];
}

static void set_rule_key(Rule *r, const WCHAR *k, const WCHAR *v)
{
    int i;

    if (!lstrcmpiW(k, L"name"))    { lstrcpynW(r->name, v, NAME_MAX_); return; }
    if (!lstrcmpiW(k, L"enabled")) { r->enabled = (v[0] == L'1'); return; }

    for (i = 0; i < F_COUNT; i++) {
        if (!lstrcmpiW(k, k_fieldKeys[i])) {
            const WCHAR *colon = v;
            WCHAR mode[16];
            while (*colon && *colon != L':') colon++;
            if (*colon) {
                lstrcpynW(mode, v, (int)min(colon - v + 1, (ptrdiff_t)ARRAYSIZE(mode)));
                r->cond[i].mode = find_name(mode, k_modeNames, M_COUNT, M_EXACT);
                lstrcpynW(r->cond[i].text, colon + 1, COND_MAX);
            } else {            /* 一致方法が無ければ完全一致として扱う */
                r->cond[i].mode = v[0] ? M_EXACT : M_ANY;
                lstrcpynW(r->cond[i].text, v, COND_MAX);
            }
            return;
        }
    }

    if (!lstrcmpiW(k, L"display")) {
        if (!lstrcmpiW(v, L"main"))         r->display = DISP_MAIN;
        else if (!lstrcmpiW(v, L"current")) r->display = DISP_CURRENT;
        else {
            Val t;
            if (val_parse(v, &t, FALSE) && t.kind == V_PX && t.v >= 1) r->display = t.v;
        }
        return;
    }
    if (!lstrcmpiW(k, L"base"))    { r->work = !lstrcmpiW(v, L"work"); return; }
    if (!lstrcmpiW(k, L"frame"))   { r->visframe = lstrcmpiW(v, L"window") != 0; return; }
    if (!lstrcmpiW(k, L"x"))       { val_parse(v, &r->x, TRUE);  return; }
    if (!lstrcmpiW(k, L"y"))       { val_parse(v, &r->y, TRUE);  return; }
    if (!lstrcmpiW(k, L"width"))   { val_parse(v, &r->w, FALSE); return; }
    if (!lstrcmpiW(k, L"height"))  { val_parse(v, &r->h, FALSE); return; }
    if (!lstrcmpiW(k, L"state"))   { r->state   = find_name(v, k_stateNames, 4, ST_KEEP);  return; }
    if (!lstrcmpiW(k, L"topmost")) { r->topmost = find_name(v, k_topNames,   3, TOP_KEEP); return; }
    if (!lstrcmpiW(k, L"when"))    { r->when    = find_name(v, k_whenNames,  2, WHEN_ONCE); return; }
    if (!lstrcmpiW(k, L"delay")) {
        Val t;
        if (val_parse(v, &t, FALSE) && t.kind == V_PX && t.v >= 0) r->delay = min(t.v, 60000);
        return;
    }
}

void config_load(void)
{
    Config  c;
    WCHAR  *text, *p, *line;
    Rule   *cur = NULL;
    BOOL    inGeneral = FALSE;
    int     i;

    ZeroMemory(&c, sizeof(c));
    c.applyExisting = TRUE;

    text = read_text_file(g_iniPath);
    if (text) {
        for (p = text; *p; ) {
            WCHAR *eq;
            line = p;
            while (*p && *p != L'\n') p++;
            if (*p) *p++ = 0;
            trim(line);
            if (!line[0] || line[0] == L';' || line[0] == L'#') continue;

            if (line[0] == L'[') {
                inGeneral = !lstrcmpiW(line, L"[general]");
                cur = NULL;
                if (!lstrcmpiW(line, L"[rule]")) cur = push_rule(&c);
                continue;
            }
            eq = line;
            while (*eq && *eq != L'=') eq++;
            if (!*eq) continue;
            *eq = 0;
            trim(line);
            {
                WCHAR *val = eq + 1;
                /* 値は前の空白だけ落とす(キャプションの末尾の空白は意味を持ちうる) */
                while (*val == L' ' || *val == L'\t') val++;
                {
                    int n = lstrlenW(val);
                    if (n && val[n - 1] == L'\r') val[n - 1] = 0;
                }
                if (cur)
                    set_rule_key(cur, line, val);
                else if (inGeneral) {
                    if (!lstrcmpiW(line, L"apply_existing")) c.applyExisting = (val[0] == L'1');
                    else if (!lstrcmpiW(line, L"log"))       c.log = (val[0] == L'1');
                    else if (!lstrcmpiW(line, L"theme"))
                        c.theme = !lstrcmpiW(val, L"light") ? 1 : !lstrcmpiW(val, L"dark") ? 2 : 0;
                }
            }
        }
        HeapFree(GetProcessHeap(), 0, text);
    }
    for (i = 0; i < c.count; i++) rule_prepare(&c.rules[i]);

    g_logInIni = c.log;
    AcquireSRWLockExclusive(&g_cfgLock);
    {
        Rule *old = g_cfg.rules;
        BOOL  log = g_cfg.log;
        g_cfg = c;
        if (log) g_cfg.log = TRUE;      /* -log の指定は残す */
        if (old) HeapFree(GetProcessHeap(), 0, old);
    }
    ReleaseSRWLockExclusive(&g_cfgLock);
}

/* ------------------------------------------------------------------ */
/*  保存                                                                */
/* ------------------------------------------------------------------ */

typedef struct { WCHAR *p; int len, cap; } SB;

static void sb_add(SB *b, const WCHAR *s)
{
    int n = lstrlenW(s);
    if (b->len + n + 1 > b->cap) {
        int    ncap = max(b->cap * 2, b->len + n + 1024);
        WCHAR *np = b->p
            ? (WCHAR *)HeapReAlloc(GetProcessHeap(), 0, b->p, ncap * sizeof(WCHAR))
            : (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ncap * sizeof(WCHAR));
        if (!np) return;
        b->p = np; b->cap = ncap;
    }
    CopyMemory(b->p + b->len, s, (n + 1) * sizeof(WCHAR));
    b->len += n;
}

static void sb_kv(SB *b, const WCHAR *k, const WCHAR *v)
{
    sb_add(b, k); sb_add(b, L"="); sb_add(b, v); sb_add(b, L"\r\n");
}

BOOL config_save(void)
{
    SB     b = { 0 };
    WCHAR  tmp[MAX_PATH + 8], num[32];
    char  *u8;
    int    i, f, n;
    HANDLE h;
    DWORD  wr;
    BOOL   ok = FALSE;

    sb_add(&b, L"; kotemado の設定。ルールは上から順に調べ、最初に条件が合ったものを使う。\r\n");
    sb_add(&b, L"; 一致方法: any(指定しない) exact prefix suffix contains wildcard\r\n");
    sb_add(&b, L"; display: main / current / 番号(\\\\.\\DISPLAYn の n)\r\n");
    sb_add(&b, L"; x, y, width, height: 空 = 変えない / 数値 = ピクセル / 50% / center(x, y のみ)\r\n\r\n");
    sb_add(&b, L"[general]\r\n");
    sb_kv(&b, L"apply_existing", g_cfg.applyExisting ? L"1" : L"0");
    if (g_cfg.theme) sb_kv(&b, L"theme", g_cfg.theme == 1 ? L"light" : L"dark");
    if (g_logInIni)  sb_kv(&b, L"log", L"1");

    for (i = 0; i < g_cfg.count; i++) {
        const Rule *r = &g_cfg.rules[i];
        sb_add(&b, L"\r\n[rule]\r\n");
        sb_kv(&b, L"name", r->name);
        sb_kv(&b, L"enabled", r->enabled ? L"1" : L"0");
        for (f = 0; f < F_COUNT; f++) {
            sb_add(&b, k_fieldKeys[f]); sb_add(&b, L"=");
            sb_add(&b, k_modeNames[r->cond[f].mode]); sb_add(&b, L":");
            sb_add(&b, r->cond[f].text); sb_add(&b, L"\r\n");
        }
        if (r->display == DISP_MAIN)         lstrcpyW(num, L"main");
        else if (r->display == DISP_CURRENT) lstrcpyW(num, L"current");
        else                                 wsprintfW(num, L"%d", r->display);
        sb_kv(&b, L"display", num);
        sb_kv(&b, L"base", r->work ? L"work" : L"screen");
        val_format(&r->x, num, ARRAYSIZE(num)); if (r->x.kind == V_CENTER) lstrcpyW(num, L"center");
        sb_kv(&b, L"x", num);
        val_format(&r->y, num, ARRAYSIZE(num)); if (r->y.kind == V_CENTER) lstrcpyW(num, L"center");
        sb_kv(&b, L"y", num);
        val_format(&r->w, num, ARRAYSIZE(num)); sb_kv(&b, L"width", num);
        val_format(&r->h, num, ARRAYSIZE(num)); sb_kv(&b, L"height", num);
        sb_kv(&b, L"frame", r->visframe ? L"visible" : L"window");
        sb_kv(&b, L"state", k_stateNames[r->state]);
        sb_kv(&b, L"topmost", k_topNames[r->topmost]);
        sb_kv(&b, L"when", k_whenNames[r->when]);
        wsprintfW(num, L"%d", r->delay);
        sb_kv(&b, L"delay", num);
    }
    if (!b.p) return FALSE;

    n  = WideCharToMultiByte(CP_UTF8, 0, b.p, b.len, NULL, 0, NULL, NULL);
    u8 = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
    if (u8) {
        WideCharToMultiByte(CP_UTF8, 0, b.p, b.len, u8, n, NULL, NULL);
        /* 書きかけで落ちても元の設定が残るよう、別名に書いてから置き換える */
        wsprintfW(tmp, L"%s.tmp", g_iniPath);
        h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ok = WriteFile(h, u8, n, &wr, NULL) && wr == (DWORD)n;
            CloseHandle(h);
            if (ok) ok = MoveFileExW(tmp, g_iniPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!ok) DeleteFileW(tmp);
        }
        HeapFree(GetProcessHeap(), 0, u8);
    }
    HeapFree(GetProcessHeap(), 0, b.p);
    if (!ok) log_printf(L"設定を保存できませんでした: %s (%lu)", g_iniPath, GetLastError());
    return ok;
}

/* ------------------------------------------------------------------ */
/*  書き換え(UI スレッドから)                                          */
/* ------------------------------------------------------------------ */

static void changed(void)
{
    config_save();
    engine_config_changed();
}

void config_insert_rule(int at, const Rule *r)
{
    Rule *slot;
    AcquireSRWLockExclusive(&g_cfgLock);
    slot = push_rule(&g_cfg);
    if (slot) {
        if (at < 0 || at > g_cfg.count - 1) at = g_cfg.count - 1;
        MoveMemory(&g_cfg.rules[at + 1], &g_cfg.rules[at], (g_cfg.count - 1 - at) * sizeof(Rule));
        g_cfg.rules[at] = *r;
        rule_prepare(&g_cfg.rules[at]);
    }
    ReleaseSRWLockExclusive(&g_cfgLock);
    changed();
}

void config_replace_rule(int at, const Rule *r)
{
    if (at < 0 || at >= g_cfg.count) return;
    AcquireSRWLockExclusive(&g_cfgLock);
    g_cfg.rules[at] = *r;
    rule_prepare(&g_cfg.rules[at]);
    ReleaseSRWLockExclusive(&g_cfgLock);
    changed();
}

void config_delete_rule(int at)
{
    if (at < 0 || at >= g_cfg.count) return;
    AcquireSRWLockExclusive(&g_cfgLock);
    MoveMemory(&g_cfg.rules[at], &g_cfg.rules[at + 1], (g_cfg.count - 1 - at) * sizeof(Rule));
    g_cfg.count--;
    ReleaseSRWLockExclusive(&g_cfgLock);
    changed();
}

void config_swap_rules(int a, int b)
{
    Rule t;
    if (a < 0 || b < 0 || a >= g_cfg.count || b >= g_cfg.count || a == b) return;
    AcquireSRWLockExclusive(&g_cfgLock);
    t = g_cfg.rules[a]; g_cfg.rules[a] = g_cfg.rules[b]; g_cfg.rules[b] = t;
    ReleaseSRWLockExclusive(&g_cfgLock);
    changed();
}

void config_set_enabled(int at, BOOL on)
{
    if (at < 0 || at >= g_cfg.count || g_cfg.rules[at].enabled == on) return;
    AcquireSRWLockExclusive(&g_cfgLock);
    g_cfg.rules[at].enabled = on;
    ReleaseSRWLockExclusive(&g_cfgLock);
    changed();
}

void config_set_apply_existing(BOOL on)
{
    g_cfg.applyExisting = on;
    config_save();
}
