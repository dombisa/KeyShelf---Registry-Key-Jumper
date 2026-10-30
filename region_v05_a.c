/* ====================================================================
 *  v0.5: консоль, расширенная командная строка, «держать от изменений»,
 *        правка веток в текстовом редакторе
 * ==================================================================== */

/* ---- ключи командной строки ---- */
typedef enum {
  C_NONE = 0, C_HELP, C_VERSION, C_JUMP, C_JUMPM, C_ADD, C_GOTO, C_LIST, C_EXPORT, C_SAVETO, C_BAT, C_VBS,
  C_SNAP, C_CMP, C_SNAPS, C_WATCH, C_HOLD, C_OPEN, C_OPENWITH, C_OPENNP, C_IMPORT, C_CLEANTMP, C_LOG, C_ADDREG, C_PROBE
} CliCmd;

typedef struct {
  CliCmd cmd;
  BOOL bad;
  wchar_t badArg[200];
  wchar_t *pos[64];
  int npos;
  BOOL noGui, yes, excl, norec, apply, newWin;
  wchar_t out[MAX_PATH], cfg[MAX_PATH];
} Cli;
static Cli g_cli;

static void ParseCli(int argc, wchar_t **argv)
{
  static const struct { const wchar_t *name; CliCmd c; } cmds[] = {
    { L"?", C_HELP }, { L"help", C_HELP }, { L"h", C_HELP }, { L"version", C_VERSION },
    { L"jump", C_JUMP }, { L"jumpm", C_JUMPM }, { L"add", C_ADD }, { L"goto", C_GOTO }, { L"list", C_LIST },
    { L"export", C_EXPORT }, { L"saveto", C_SAVETO },
    { L"bat", C_BAT }, { L"export-bat", C_BAT }, { L"vbs", C_VBS }, { L"export-vbs", C_VBS },
    { L"snap", C_SNAP }, { L"cmp", C_CMP }, { L"snaps", C_SNAPS }, { L"watch", C_WATCH }, { L"hold", C_HOLD },
    { L"open", C_OPEN }, { L"open-with", C_OPENWITH }, { L"open-notepad", C_OPENNP }, { L"import", C_IMPORT },
    { L"clean-tmp", C_CLEANTMP }, { L"log", C_LOG }, { L"addreg", C_ADDREG }, { L"probe", C_PROBE }
  };
  int i;
  size_t k;
  memset(&g_cli, 0, sizeof g_cli);
  for (i = 1; i < argc; i++) {
    const wchar_t *a = argv[i], *nm;
    BOOL found = FALSE;
    if (a[0] != L'/' && a[0] != L'-') { if (g_cli.npos < 64) g_cli.pos[g_cli.npos++] = argv[i]; continue; }
    nm = a;
    while (*nm == L'/' || *nm == L'-') nm++;
    if (!_wcsicmp(nm, L"noui") || !_wcsicmp(nm, L"nogui")) { g_cli.noGui = TRUE; continue; }
    if (!_wcsicmp(nm, L"yes") || !_wcsicmp(nm, L"y")) { g_cli.yes = TRUE; continue; }
    if (!_wcsicmp(nm, L"excl")) { g_cli.excl = TRUE; continue; }
    if (!_wcsicmp(nm, L"norec")) { g_cli.norec = TRUE; continue; }
    if (!_wcsicmp(nm, L"apply")) { g_cli.apply = TRUE; continue; }
    if (!_wcsicmp(nm, L"new")) { g_cli.newWin = TRUE; continue; }
    if (!_wcsicmp(nm, L"out") && i + 1 < argc) { wcsncpy(g_cli.out, argv[++i], MAX_PATH - 1); continue; }
    if (!_wcsicmp(nm, L"cfg") && i + 1 < argc) { wcsncpy(g_cli.cfg, argv[++i], MAX_PATH - 1); continue; }
    for (k = 0; k < sizeof cmds / sizeof cmds[0]; k++) {
      if (!_wcsicmp(nm, cmds[k].name)) {
        found = TRUE;
        if (g_cli.cmd && g_cli.cmd != cmds[k].c) { g_cli.bad = TRUE; wcsncpy(g_cli.badArg, a, 199); }
        else g_cli.cmd = cmds[k].c;
        break;
      }
    }
    if (!found) { g_cli.bad = TRUE; wcsncpy(g_cli.badArg, a, 199); }
  }
}

/* ---- консольный вывод ---- */
static HANDLE g_hCon;
static BOOL g_conIsConsole, g_conReady;
static volatile LONG g_ctrlc;

static void ConInit(void)
{
  DWORD mode;
  HANDLE h;
  if (g_conReady) return;
  g_conReady = TRUE;
  h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
    g_hCon = h;
    g_conIsConsole = GetConsoleMode(h, &mode) ? TRUE : FALSE;
    return;
  }
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { g_hCon = h; g_conIsConsole = TRUE; }
  }
}

static void ConWrite(const wchar_t *s, size_t len)
{
  DWORD w;
  if (!g_hCon || !len) return;
  if (g_conIsConsole) { WriteConsoleW(g_hCon, s, (DWORD)len, &w, NULL); return; }
  {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, (int)len, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, s, (int)len, u, n, NULL, NULL);
    WriteFile(g_hCon, u, (DWORD)n, &w, NULL);
    free(u);
  }
}

/* форматированный вывод; \n превращается в \r\n */
static void ConPrint(const wchar_t *fmt, ...)
{
  wchar_t *buf = (wchar_t *)malloc(16384 * sizeof(wchar_t)), *out = (wchar_t *)malloc(32768 * sizeof(wchar_t));
  va_list ap;
  size_t i, n = 0;
  va_start(ap, fmt);
  _vsnwprintf(buf, 16383, fmt, ap);
  va_end(ap);
  buf[16383] = 0;
  for (i = 0; buf[i] && n < 32760; i++) {
    if (buf[i] == L'\n' && (i == 0 || buf[i - 1] != L'\r')) out[n++] = L'\r';
    out[n++] = buf[i];
  }
  ConWrite(out, n);
  free(buf); free(out);
}

static void ConProgressHook(const wchar_t *t)
{
  wchar_t line[100];
  if (!g_conIsConsole) return;
  _snwprintf(line, 99, L"\r%-78.78ls", t);
  line[99] = 0;
  ConWrite(line, wcslen(line));
}

static void ConProgressEnd(void)
{
  if (g_conIsConsole && g_conProgress) ConWrite(L"\r                                                                              \r", 80);
  g_conProgress = FALSE;
}

static BOOL ConAsk(const wchar_t *q)
{
  wchar_t line[64] = L"";
  DWORD got = 0, mode;
  HANDLE in;
  if (g_cli.yes) return TRUE;
  ConPrint(L"%ls [y/N] (д/н): ", q);
  in = GetStdHandle(STD_INPUT_HANDLE);
  if (!in || in == INVALID_HANDLE_VALUE || GetFileType(in) == FILE_TYPE_UNKNOWN)
    in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (!in || in == INVALID_HANDLE_VALUE) return FALSE;
  if (GetConsoleMode(in, &mode)) {
    ReadConsoleW(in, line, 63, &got, NULL);
    line[got] = 0;
  } else {
    char b[64];
    ReadFile(in, b, 63, &got, NULL);
    b[got] = 0;
    MultiByteToWideChar(CP_UTF8, 0, b, -1, line, 63);
  }
  {
    wchar_t *p = line;
    while (*p == L' ') p++;
    return *p == L'y' || *p == L'Y' || *p == 0x0434 || *p == 0x0414;
  }
}

static BOOL WINAPI CtrlHandler(DWORD t)
{
  if (t == CTRL_C_EVENT || t == CTRL_BREAK_EVENT || t == CTRL_CLOSE_EVENT) {
    InterlockedExchange(&g_ctrlc, 1);
    InterlockedExchange(&g_cancel, 1);
    return TRUE;
  }
  return FALSE;
}

static const wchar_t *HELP_TEXT =
  L"Key Shelf Reg Jumper " APP_VER_STR L" - командная строка\n"
  L"Использование: KeyShelfRegJumper.exe [ключи]   (без ключей открывается окно программы)\n"
  L"Ключи можно писать с / или с -. Ветки: HKLM\\..., HKCU\\..., HKEY_LOCAL_MACHINE\\... и т.д.\n\n"
  L"ОБЩИЕ\n"
  L"  /? | /help               эта справка          /version   версия\n"
  L"  /nogui | /noui           только консоль: окно не показывается вообще (даже для /addreg)\n"
  L"  /yes                     отвечать «да» на все вопросы\n"
  L"  /cfg файл                использовать другой файл конфигурации\n"
  L"  /log on|off              включить или выключить лог keyshelf.log\n\n"
  L"ПЕРЕХОД И ЗАКЛАДКИ\n"
  L"  /jump ветка              открыть regedit на ветке (режим NO: открытый regedit закрывается и запускается заново)\n"
  L"  /jumpm ветка             то же в новом окне regedit /m (режим NW: другие окна не трогаются)\n"
  L"  /goto \"имя|путь\" [/new]  перейти по закладке; /new - в новом окне\n"
  L"  /add ветка [\"имя\"] [\"папка/подпапка\"]   добавить ветку в закладки\n"
  L"  /list                    вывести закладки\n"
  L"  /addreg файл.reg ...     добавить ветки из .reg (без /nogui откроется окно выбора)\n\n"
  L"СОХРАНЕНИЕ И ЭКСПОРТ\n"
  L"  /export ветка... файл.reg            выгрузить ветки в один .reg-файл\n"
  L"  /saveto ветка... [\"заметка\"]         сохранить в архив Saved (дата, время и заметка в имени файла)\n"
  L"  /bat | /export-bat ветка... [NO|NW|BOTH] [папка]   скрипт .bat, открывающий regedit на ветке\n"
  L"  /vbs | /export-vbs ветка... [NO|NW|BOTH] [папка]   то же в виде .vbs (VBScript)\n"
  L"      NO   (Normal Open) - обычный способ: уже открытый regedit закрывается и запускается заново на ветке\n"
  L"      NW   (New Window)  - новое окно regedit /m: уже открытые окна не трогаются (по умолчанию)\n"
  L"      BOTH - создать оба варианта. Имена файлов: NO-HKLM_SOFTWARE_...bat и NW-HKLM_SOFTWARE_...bat\n\n"
  L"ПРАВКА В РЕДАКТОРЕ\n"
  L"  /open ветка...           выгрузить в Saved\\tmp и открыть в редакторе по умолчанию (для .reg это команда «Правка», обычно Блокнот)\n"
  L"  /open-notepad ветка...   то же в notepad.exe\n"
  L"  /open-with редактор.exe ветка...   то же в указанном редакторе\n"
  L"      Оригинал остаётся рядом (файл *.orig.reg). /apply - дождаться закрытия редактора и предложить внести правки.\n"
  L"  /import файл.reg         внести .reg в реестр (с подтверждением; /yes - без вопроса)\n"
  L"  /clean-tmp               очистить Saved\\tmp\n\n"
  L"СНИМКИ И СРАВНЕНИЕ\n"
  L"  /snap файл.ksn \"название\" ветка... [/excl]   снимок веток; /excl - без «шумных» веток\n"
  L"  /snaps                                        список снимков\n"
  L"  /cmp старый новый|current [отчёт.txt]         сравнить два снимка или снимок с текущим состоянием\n"
  L"      снимок можно указать путём, именем файла в Snapshots или частью названия\n\n"
  L"НАБЛЮДЕНИЕ И ЗАЩИТА\n"
  L"  /watch [секунды] ветка... [/out файл]   наблюдать и печатать изменения в консоль в реальном времени;\n"
  L"                                          без секунд - до Ctrl+C; /out - дополнительно писать события в файл\n"
  L"  /hold [ветка...] [/norec]               держать от изменений: при изменении или удалении вернуть исходное и записать в лог;\n"
  L"                                          без веток - список из программы; /norec - только сам ключ без вложенных; стоп - Ctrl+C\n"
  L"  /probe                                  прочитать текущую ветку из открытого regedit\n\n"
  L"КОДЫ ВЫХОДА: 0 - успех, 1 - ошибка выполнения, 2 - неверные параметры, 3 - не найдено\n";

static void ShowHelp(void)
{
  if (g_hCon) { ConPrint(L"%ls", HELP_TEXT); return; }
  if (!g_cli.noGui) MessageBoxW(NULL, HELP_TEXT, L"Key Shelf Reg Jumper", MB_ICONINFORMATION);
}

/* ---- ветка из открытого regedit ---- */
typedef struct { wchar_t out[2048]; BOOL ok; } KeyProbe;

static BOOL CALLBACK ProbeKid(HWND h, LPARAM lp)
{
  KeyProbe *kp = (KeyProbe *)lp;
  wchar_t cls[80], text[2300];
  DWORD_PTR res = 0;
  GetClassNameW(h, cls, 80);
  if (_wcsicmp(cls, L"msctls_statusbar32") != 0 && !wcsstr(cls, L"Edit") && !wcsstr(cls, L"edit") && !wcsstr(cls, L"Combo")) return TRUE;
  text[0] = 0;
  if (SendMessageTimeoutW(h, WM_GETTEXT, 2200, (LPARAM)text, SMTO_ABORTIFHUNG | SMTO_BLOCK, 700, &res)) {
    text[2299] = 0;
    Log(L"regedit: окно %ls, текст длиной %d", cls, (int)wcslen(text));
    if (text[0] && NormalizeKey(text, kp->out, 2048)) { kp->ok = TRUE; return FALSE; }
  } else Log(L"regedit: не удалось прочитать окно %ls (ошибка %lu)", cls, GetLastError());
  return TRUE;
}

static BOOL CALLBACK FindRegeditTop(HWND h, LPARAM lp)
{
  wchar_t cls[64];
  GetClassNameW(h, cls, 64);
  if (!wcscmp(cls, L"RegEdit_RegEdit") && IsWindowVisible(h)) { *(HWND *)lp = h; return FALSE; }
  return TRUE;
}

/* 0 - успех, 1 - regedit не запущен, 2 - не удалось прочитать */
static int GetRegeditKey(wchar_t *out)
{
  HWND re = NULL;
  KeyProbe kp;
  EnumWindows(FindRegeditTop, (LPARAM)&re);
  if (!re) return 1;
  memset(&kp, 0, sizeof kp);
  EnumChildWindows(re, ProbeKid, (LPARAM)&kp);
  if (kp.ok) { wcscpy(out, kp.out); return 0; }
  return 2;
}

/* ---- диалог «что сделать с веткой» ---- */
enum { IDP_BOOK = 600, IDP_SAVE, IDP_SNAP, IDP_BAT, IDP_WATCH, IDP_HOLD, IDP_EDIT };

typedef struct { HWND hwnd; wchar_t path[2048]; const wchar_t *src; BOOL done; int choice; } PickDlg;

static LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  PickDlg *d = (PickDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
      d = (PickDlg *)cs->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
      d->hwnd = h;
      Mk(h, L"STATIC", d->src, 0, 10, 8, 440, 16, 90, 0);
      Mk(h, L"EDIT", d->path, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 10, 28, 440, 46, 91, WS_EX_CLIENTEDGE);
      Mk(h, L"STATIC", L"Что сделать с этой веткой:", 0, 10, 82, 440, 16, 92, 0);
      Mk(h, L"BUTTON", L"В закладки", WS_TABSTOP | BS_DEFPUSHBUTTON, 10, 102, 218, 28, IDP_BOOK, 0);
      Mk(h, L"BUTTON", L"Сохранить в .reg…", WS_TABSTOP, 232, 102, 218, 28, IDP_SAVE, 0);
      Mk(h, L"BUTTON", L"В снимок для сравнения…", WS_TABSTOP, 10, 136, 218, 28, IDP_SNAP, 0);
      Mk(h, L"BUTTON", L"Экспорт скрипта (.bat / .vbs)…", WS_TABSTOP, 232, 136, 218, 28, IDP_BAT, 0);
      Mk(h, L"BUTTON", L"В наблюдение онлайн…", WS_TABSTOP, 10, 170, 218, 28, IDP_WATCH, 0);
      Mk(h, L"BUTTON", L"Держать от изменений", WS_TABSTOP, 232, 170, 218, 28, IDP_HOLD, 0);
      Mk(h, L"BUTTON", L"Править в Блокноте…", WS_TABSTOP, 10, 204, 218, 28, IDP_EDIT, 0);
      Mk(h, L"BUTTON", L"Отмена", WS_TABSTOP, 232, 204, 218, 28, IDCANCEL, 0);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (!d) break;
      if ((id >= IDP_BOOK && id <= IDP_EDIT) || id == IDCANCEL) { d->choice = id == IDCANCEL ? 0 : id; d->done = TRUE; return 0; }
      break;
    }
    case WM_CLOSE: if (d) d->done = TRUE; return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static int RunPickDlg(const wchar_t *path, const wchar_t *src)
{
  PickDlg d;
  RECT r = { 0, 0, 460, 242 }, orc;
  MSG msg;
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  int w, hgt, x, y;
  memset(&d, 0, sizeof d);
  wcsncpy(d.path, path, 2047);
  d.src = src;
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  w = r.right - r.left; hgt = r.bottom - r.top;
  GetWindowRect(g_hMain, &orc);
  x = orc.left + ((orc.right - orc.left) - w) / 2; y = orc.top + ((orc.bottom - orc.top) - hgt) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  EnableWindow(g_hMain, FALSE);
  d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLS_PICK, L"Ветка из regedit", style, x, y, w, hgt, g_hMain, NULL, g_hInst, &d);
  if (!d.hwnd) { EnableWindow(g_hMain, TRUE); return 0; }
  ShowWindow(d.hwnd, SW_SHOW);
  SetForegroundWindow(d.hwnd);
  while (!d.done) {
    int rc = GetMessageW(&msg, NULL, 0, 0);
    if (rc <= 0) { if (rc == 0) PostQuitMessage((int)msg.wParam); break; }
    if (!IsDialogMessageW(d.hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  EnableWindow(g_hMain, TRUE);
  DestroyWindow(d.hwnd);
  SetForegroundWindow(g_hMain);
  return d.choice;
}

static void AddBookmarkPath(const wchar_t *path)
{
  HTREEITEM parent, after, it;
  wchar_t name[300], cm[200], ts[32];
  wchar_t *sl = wcsrchr(path, L'\\');
  CommitEdits();
  GetInsertTarget(&parent, &after);
  wcsncpy(name, sl ? sl + 1 : path, 255);
  name[255] = 0;
  NowStr(ts, 32);
  _snwprintf(cm, 199, L"Добавлено из regedit: %ls", ts);
  cm[199] = 0;
  it = InsertNode(parent, after, name, FALSE, path, cm);
  if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
  TreeView_SelectItem(g_hTree, it);
  MarkDirty();
  SetFocus(g_hComment);
}

static void ActWatchStart(void);
static void ActEditInEditor(int kind);
static void ProtAddPath(const wchar_t *path, BOOL rec);

static void ActTakeBranch(void)
{
  wchar_t path[2048], clip[2048];
  int r = GetRegeditKey(path), choice;
  const wchar_t *src = L"Источник: открытый regedit";
  if (r != 0) {
    wchar_t msg[2600];
    if (GetClipboardKey(clip, 2048)) {
      _snwprintf(msg, 2599, L"%ls\n\nВ буфере обмена есть ветка:\n%ls\n\nИспользовать её?",
                 r == 1 ? L"Окно regedit не найдено." : L"Не удалось прочитать ветку из окна regedit (возможно, он запущен от администратора, а программа без этих прав).", clip);
      if (MessageBoxW(g_hMain, msg, APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
      wcscpy(path, clip);
      src = L"Источник: буфер обмена";
    } else {
      MessageBoxW(g_hMain,
        r == 1 ? L"regedit не запущен. Откройте regedit, перейдите на нужную ветку и снова выберите эту команду."
               : L"Не удалось прочитать ветку из окна regedit: возможно, он запущен от администратора, а программа без этих прав.\n\n"
                 L"Способ, который работает всегда: в regedit нажмите правой кнопкой на нужную ветку → «Копировать имя раздела», "
                 L"затем снова выберите эту команду. Либо запустите Key Shelf Reg Jumper от имени администратора.",
        APP_NAME, MB_ICONINFORMATION);
      return;
    }
  }
  Log(L"Ветка из regedit: %ls (%ls)", path, src);
  choice = RunPickDlg(path, src);
  switch (choice) {
    case IDP_BOOK: SwitchTab(0); AddBookmarkPath(path); break;
    case IDP_SAVE: wcscpy(g_extraTarget, path); ActSave(); g_extraTarget[0] = 0; break;
    case IDP_SNAP: wcscpy(g_extraTarget, path); ActSnapNew(); g_extraTarget[0] = 0; SwitchTab(2); break;
    case IDP_BAT: wcscpy(g_extraTarget, path); ActExpBat(); g_extraTarget[0] = 0; break;
    case IDP_WATCH: wcscpy(g_extraTarget, path); ActWatchStart(); g_extraTarget[0] = 0; SwitchTab(3); break;
    case IDP_HOLD: ProtAddPath(path, TRUE); SwitchTab(4); break;
    case IDP_EDIT: wcscpy(g_extraTarget, path); ActEditInEditor(0); g_extraTarget[0] = 0; break;
  }
}

/* ====================================================================
 *  общий движок наблюдения (kind: наблюдение) и защиты (kind: protect)
 * ==================================================================== */
typedef struct { wchar_t time[24]; DiffItem d; } WItem;
static WItem *g_wev;
static int g_nwev, g_capwev;
static wchar_t g_watchDir[MAX_PATH], g_protLog[MAX_PATH + 16];

typedef struct {
  wchar_t root[2048];
  BOOL rec, disabled;
  HKEY hkey;
  HANDLE ev;
  wchar_t base[MAX_PATH + 16], fresh[MAX_PATH + 16], basereg[MAX_PATH + 16];
  BOOL pending;
  DWORD pendTick, lastScan, lastDur, retryTick, failUntil;
} Watcher;

typedef struct {
  Watcher *w;
  int n;
  Scope scope;
  HANDLE thread;
  volatile LONG stop;
  BOOL running, protect, conPrint;
  wchar_t log[MAX_PATH + 16], start[32];
  DWORD total;
} WatchSet;
static WatchSet g_obs, g_prot;
static WatchSet *g_baseWs;

#define WNOTIFY (REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY)

static void WLogAppend(const wchar_t *file, const wchar_t *line)
{
  HANDLE h;
  DWORD w;
  int n;
  char *u;
  if (!file || !file[0]) return;
  h = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  if (GetFileSize(h, NULL) == 0) { static const BYTE bom[3] = { 0xEF, 0xBB, 0xBF }; WriteFile(h, bom, 3, &w, NULL); }
  n = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
  u = (char *)malloc(n + 2);
  WideCharToMultiByte(CP_UTF8, 0, line, -1, u, n, NULL, NULL);
  u[n - 1] = '\r'; u[n] = '\n';
  WriteFile(h, u, n + 1, &w, NULL);
  free(u);
  CloseHandle(h);
}

/* многострочное представление события для консоли */
static void FmtEventBlock(WBuf *b, const wchar_t *ts, const DiffItem *d)
{
  const wchar_t *nm = d->name ? (d->name[0] ? d->name : L"(по умолчанию)") : NULL;
  wb_addc(b, L'['); wb_adds(b, ts); wb_adds(b, L"] "); wb_adds(b, KindLabel(d->kind)); wb_adds(b, L"\n");
  wb_adds(b, L"  Ветка:    "); wb_adds(b, d->key); wb_adds(b, L"\n");
  if (nm) { wb_adds(b, L"  Значение: "); wb_adds(b, nm); wb_adds(b, L"\n"); }
  if (d->o) { wb_adds(b, L"  Было:     "); wb_adds(b, d->o); wb_adds(b, L"\n"); }
  if (d->n) { wb_adds(b, L"  Стало:    "); wb_adds(b, d->n); wb_adds(b, L"\n"); }
}

static void WatchPushEvents(WatchSet *ws, DiffItem *items, int n)
{
  int i;
  wchar_t ts[32];
  NowStr(ts, 32);
  EnterCriticalSection(&g_wcs);
  for (i = 0; i < n; i++) {
    DiffItem *d = &items[i];
    WBuf b = { 0 }, cb = { 0 };
    const wchar_t *nm = d->name ? (d->name[0] ? d->name : L"(по умолчанию)") : NULL;
    wb_addc(&b, L'['); wb_adds(&b, ts); wb_adds(&b, L"] "); wb_adds(&b, KindLabel(d->kind)); wb_adds(&b, L": "); wb_adds(&b, d->key);
    if (nm) { wb_adds(&b, L"  \\  "); wb_adds(&b, nm); }
    if (d->o) { wb_adds(&b, L"  |  было: "); wb_adds(&b, d->o); }
    if (d->n) { wb_adds(&b, L"  |  стало: "); wb_adds(&b, d->n); }
    WLogAppend(ws->log, b.p);
    free(b.p);
    if (ws->conPrint) { FmtEventBlock(&cb, ts, d); ConPrint(L"%ls\n", cb.p); }
    free(cb.p);
    if (g_nwev < 200000) {
      WItem *w;
      if (g_nwev == g_capwev) { g_capwev = g_capwev ? g_capwev * 2 : 1024; g_wev = (WItem *)realloc(g_wev, g_capwev * sizeof(WItem)); }
      w = &g_wev[g_nwev++];
      wcscpy(w->time, ts);
      w->d = *d;
    } else { free(d->key); free(d->name); free(d->o); free(d->n); }
  }
  ws->total += n;
  LeaveCriticalSection(&g_wcs);
  if (g_hMain) PostMessageW(g_hMain, WM_APP_WATCH, 0, 0);
}

static void WatchArm(Watcher *w)
{
  HKEY hive;
  const wchar_t *sub;
  if (w->hkey) return;
  hive = HiveFromName(w->root);
  if (!hive) return;
  sub = wcschr(w->root, L'\\');
  sub = sub ? sub + 1 : L"";
  if (RegOpenKeyExW(hive, sub, 0, KEY_NOTIFY | KEY_READ | KEY_WOW64_64KEY, &w->hkey) != ERROR_SUCCESS) { w->hkey = NULL; return; }
  ResetEvent(w->ev);
  if (RegNotifyChangeKeyValue(w->hkey, w->rec ? TRUE : FALSE, WNOTIFY, w->ev, TRUE) != ERROR_SUCCESS) { RegCloseKey(w->hkey); w->hkey = NULL; }
}

/* ---- защита: возврат исходного состояния ---- */
static void ProtEmit(const wchar_t *line)
{
  WLogAppend(g_protLog, line);
  Log(L"Защита: %ls", line);
  if (g_prot.conPrint) ConPrint(L"%ls\n\n", line);
  if (g_hMain) PostMessageW(g_hMain, WM_APP_PROT, 0, (LPARAM)Dup(line));
}

static LONG DeleteRegValueAt(const wchar_t *key, const wchar_t *name)
{
  HKEY hive = HiveFromName(key), h;
  const wchar_t *sub;
  LONG r;
  if (!hive) return ERROR_PATH_NOT_FOUND;
  sub = wcschr(key, L'\\');
  sub = sub ? sub + 1 : L"";
  r = RegOpenKeyExW(hive, sub, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h);
  if (r == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
  if (r != ERROR_SUCCESS) return r;
  r = RegDeleteValueW(h, name);
  RegCloseKey(h);
  return r == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : r;
}

static LONG DeleteRegKeyTreeAt(const wchar_t *key)
{
  wchar_t parent[2048], *sl;
  HKEY hive, h;
  const wchar_t *sub;
  LONG r;
  wcsncpy(parent, key, 2047); parent[2047] = 0;
  sl = wcsrchr(parent, L'\\');
  if (!sl) return ERROR_INVALID_PARAMETER;
  *sl = 0;
  hive = HiveFromName(parent);
  if (!hive) return ERROR_PATH_NOT_FOUND;
  sub = wcschr(parent, L'\\');
  sub = sub ? sub + 1 : L"";
  r = RegOpenKeyExW(hive, sub, 0, DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &h);
  if (r == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
  if (r != ERROR_SUCCESS) return r;
  r = RegDeleteTreeW(h, sl + 1);
  RegCloseKey(h);
  return r == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : r;
}

static void ProtectApply(Watcher *w, DiffItem *items, int n)
{
  int i;
  BOOL needImport = FALSE, failed = FALSE;
  wchar_t ts[32], msg[600];
  NowStr(ts, 32);
  for (i = 0; i < n; i++) {
    DiffItem *d = &items[i];
    WBuf b = { 0 };
    const wchar_t *nm = d->name ? (d->name[0] ? d->name : L"(по умолчанию)") : L"";
    wchar_t *key = d->key;
    wb_addc(&b, L'['); wb_adds(&b, ts); wb_adds(&b, L"] ");
    switch (d->kind) {
      case DK_VALCHG:
        wb_adds(&b, L"Изменилось значение «"); wb_adds(&b, nm); wb_adds(&b, L"» в "); wb_adds(&b, key);
        wb_adds(&b, L": было "); wb_adds(&b, d->o ? d->o : L""); wb_adds(&b, L", стало "); wb_adds(&b, d->n ? d->n : L""); wb_adds(&b, L". Возвращаю.");
        needImport = TRUE;
        break;
      case DK_VALDEL:
        wb_adds(&b, L"Удалено значение «"); wb_adds(&b, nm); wb_adds(&b, L"» в "); wb_adds(&b, key);
        wb_adds(&b, L" (было "); wb_adds(&b, d->o ? d->o : L""); wb_adds(&b, L"). Возвращаю.");
        needImport = TRUE;
        break;
      case DK_VALADD:
        wb_adds(&b, L"Добавлено значение «"); wb_adds(&b, nm); wb_adds(&b, L"» в "); wb_adds(&b, key);
        wb_adds(&b, L" ("); wb_adds(&b, d->n ? d->n : L""); wb_adds(&b, L"). Удаляю.");
        break;
      case DK_KEYADD:
        wb_adds(&b, L"Добавлена ветка "); wb_adds(&b, key); wb_adds(&b, L". Удаляю.");
        break;
      case DK_KEYDEL:
        wb_adds(&b, L"Удалена ветка "); wb_adds(&b, key); wb_adds(&b, L". Возвращаю.");
        needImport = TRUE;
        break;
    }
    ProtEmit(b.p);
    free(b.p);
    if (d->kind == DK_VALADD) {
      LONG r = DeleteRegValueAt(d->key, d->name ? d->name : L"");
      if (r != ERROR_SUCCESS) { failed = TRUE; _snwprintf(msg, 599, L"    Не удалось удалить значение (код %ld). Нужны права администратора?", r); ProtEmit(msg); }
    } else if (d->kind == DK_KEYADD) {
      LONG r = DeleteRegKeyTreeAt(d->key);
      if (r != ERROR_SUCCESS) { failed = TRUE; _snwprintf(msg, 599, L"    Не удалось удалить ветку (код %ld). Нужны права администратора?", r); ProtEmit(msg); }
    }
  }
  if (needImport) {
    wchar_t em[500];
    int rc = RunRegImport(w->basereg, em, 500);
    if (rc == 0) ProtEmit(L"    Исходные значения возвращены.");
    else { failed = TRUE; _snwprintf(msg, 599, L"    Не удалось вернуть исходное (reg import, код %d): %ls Нужны права администратора?", rc, em); ProtEmit(msg); }
  }
  if (failed) { w->failUntil = GetTickCount() + 30000; ProtEmit(L"    Повторю попытку через 30 секунд."); }
  for (i = 0; i < n; i++) { free(items[i].key); free(items[i].name); free(items[i].o); free(items[i].n); }
}

static void WatchProcess(WatchSet *ws, Watcher *w)
{
  ScanJob *sj = (ScanJob *)calloc(1, sizeof *sj);
  Reader ra, rb;
  int i;
  DWORD t0 = GetTickCount();
  BOOL ok;
  ScAddRoot(&sj->scope, w->root);
  for (i = 0; i < ws->scope.nexcl; i++) ScAddExcl(&sj->scope, ws->scope.excl[i]);
  wcscpy(sj->file, w->fresh);
  wcscpy(sj->name, L"watch");
  sj->quiet = TRUE;
  sj->shallow = !w->rec;
  sj->cancel = &ws->stop;
  if (DoScanToFile(sj)) {
    DiffItem *items = NULL;
    int n = 0;
    memset(&ra, 0, sizeof ra); memset(&rb, 0, sizeof rb);
    ok = RdOpen(&ra, w->base);
    if (ok) ok = RdOpen(&rb, w->fresh);
    if (ok) {
      EnterCriticalSection(&g_mergeCs);
      g_nsink = 0;
      g_sinkActive = TRUE;
      MergeDiff(&ra, &rb, &sj->scope, &sj->scope);
      g_sinkActive = FALSE;
      items = g_sink; n = g_nsink;
      g_sink = NULL; g_nsink = g_capsink = 0;
      LeaveCriticalSection(&g_mergeCs);
      RdClose(&ra); RdClose(&rb);
      if (n > 0) { if (ws->protect) ProtectApply(w, items, n); else WatchPushEvents(ws, items, n); }
      free(items);
      if (ws->protect) DeleteFileW(w->fresh);
      else { DeleteFileW(w->base); MoveFileExW(w->fresh, w->base, MOVEFILE_REPLACE_EXISTING); }
    } else {
      RdClose(&ra); RdClose(&rb);
      DeleteFileW(w->fresh);
    }
  }
  ScFree(&sj->scope);
  free(sj);
  w->lastDur = GetTickCount() - t0;
}

static DWORD WINAPI WatchThread(LPVOID p)
{
  WatchSet *ws = (WatchSet *)p;
  while (!ws->stop) {
    int i;
    for (i = 0; i < ws->n && !ws->stop; i++) {
      Watcher *w = &ws->w[i];
      DWORD now = GetTickCount(), minGap;
      if (w->disabled) continue;
      if (!w->hkey) {
        if (now - w->retryTick >= 1000) {
          w->retryTick = now;
          WatchArm(w);
          if (w->hkey && !w->pending) { w->pending = TRUE; w->pendTick = now; }
        }
      } else if (WaitForSingleObject(w->ev, 0) == WAIT_OBJECT_0) {
        ResetEvent(w->ev);
        if (RegNotifyChangeKeyValue(w->hkey, w->rec ? TRUE : FALSE, WNOTIFY, w->ev, TRUE) != ERROR_SUCCESS) { RegCloseKey(w->hkey); w->hkey = NULL; }
        if (!w->pending) { w->pending = TRUE; w->pendTick = now; }
      }
      minGap = w->lastDur * 2 > 1000 ? w->lastDur * 2 : 1000;
      if (w->pending && now - w->pendTick >= 600 && now - w->lastScan >= minGap && (LONG)(now - w->failUntil) >= 0) {
        w->pending = FALSE;
        WatchProcess(ws, w);
        w->lastScan = GetTickCount();
      }
    }
    Sleep(150);
  }
  return 0;
}

static void WatchAddRoot(WatchSet *ws, const wchar_t *root, BOOL rec)
{
  Watcher *w;
  if (ws->n >= 300) return;
  ws->w = (Watcher *)realloc(ws->w, (ws->n + 1) * sizeof(Watcher));
  w = &ws->w[ws->n];
  memset(w, 0, sizeof *w);
  wcsncpy(w->root, root, 2047);
  w->rec = rec;
  w->ev = CreateEventW(NULL, TRUE, FALSE, NULL);
  ws->n++;
}

static void WatchFreeAll(WatchSet *ws, BOOL delFiles)
{
  int i;
  for (i = 0; i < ws->n; i++) {
    if (ws->w[i].hkey) RegCloseKey(ws->w[i].hkey);
    if (ws->w[i].ev) CloseHandle(ws->w[i].ev);
    if (delFiles) { DeleteFileW(ws->w[i].base); DeleteFileW(ws->w[i].fresh); DeleteFileW(ws->w[i].basereg); }
  }
  free(ws->w);
  ws->w = NULL; ws->n = 0;
}

typedef struct { int canceled, failed; } WBaseRes;

static DWORD WINAPI WatchBaseWorker(LPVOID p)
{
  WBaseRes *r = (WBaseRes *)p;
  WatchSet *ws = g_baseWs;
  int i, k;
  for (i = 0; i < ws->n; i++) {
    ScanJob *sj;
    Watcher *w = &ws->w[i];
    if (g_cancel) { r->canceled = 1; break; }
    SetBusy(L"Исходное состояние %d из %d: %ls", i + 1, ws->n, w->root);
    if (ws->protect) {
      wchar_t em[500], line[900];
      int rc = RunRegExport(w->root, w->basereg, em, 500);
      if (rc != 0) {
        w->disabled = TRUE;
        _snwprintf(line, 899, L"Не удалось взять исходное состояние ветки %ls (%ls): защита для неё не включена.", w->root, em);
        ProtEmit(line);
        continue;
      }
    }
    sj = (ScanJob *)calloc(1, sizeof *sj);
    ScAddRoot(&sj->scope, w->root);
    for (k = 0; k < ws->scope.nexcl; k++) ScAddExcl(&sj->scope, ws->scope.excl[k]);
    wcscpy(sj->file, w->base);
    wcscpy(sj->name, L"watch");
    sj->quiet = TRUE;
    sj->shallow = !w->rec;
    if (!DoScanToFile(sj)) { if (g_cancel) r->canceled = 1; else r->failed++; ScFree(&sj->scope); free(sj); if (r->canceled) break; continue; }
    ScFree(&sj->scope); free(sj);
  }
  SetBusy(L"Готово");
  return 0;
}

/* общий запуск: открыть ключи, взять исходное состояние, стартовать поток */
static BOOL WatchLaunch(WatchSet *ws, BOOL ui)
{
  int i, active = 0;
  wchar_t ts[32], tsf[32], hdr[400];
  WBaseRes res;
  EnsureDir(g_watchDir);
  MakeTimestamp(tsf, 32);
  for (i = 0; i < ws->n; i++) {
    Watcher *w = &ws->w[i];
    _snwprintf(w->base, MAX_PATH + 15, L"%ls\\%lc%ls_%d_base.tmp", g_watchDir, ws->protect ? L'p' : L'w', tsf, i);
    _snwprintf(w->fresh, MAX_PATH + 15, L"%ls\\%lc%ls_%d_new.tmp", g_watchDir, ws->protect ? L'p' : L'w', tsf, i);
    _snwprintf(w->basereg, MAX_PATH + 15, L"%ls\\%lc%ls_%d_base.reg", g_watchDir, ws->protect ? L'p' : L'w', tsf, i);
    WatchArm(w);
    if (!w->hkey) Log(L"Слежение: не удалось открыть для слежения %ls", w->root);
  }
  g_baseWs = ws;
  memset(&res, 0, sizeof res);
  if (ui) RunBusy(WatchBaseWorker, &res, ws->protect ? L"Защита: исходное состояние" : L"Наблюдение: исходное состояние");
  else { InterlockedExchange(&g_cancel, 0); WatchBaseWorker(&res); }
  if (res.canceled) { WatchFreeAll(ws, TRUE); ScFree(&ws->scope); return FALSE; }
  for (i = 0; i < ws->n; i++) if (!ws->w[i].disabled) active++;
  if (active == 0) { WatchFreeAll(ws, TRUE); ScFree(&ws->scope); return FALSE; }
  NowStr(ts, 32);
  wcscpy(ws->start, ts);
  ws->total = 0;
  if (!ws->protect && !ws->log[0]) _snwprintf(ws->log, MAX_PATH + 15, L"%ls\\watch_%ls.log", g_watchDir, tsf);
  _snwprintf(hdr, 399, L"[%ls] %ls. Областей: %d", ts, ws->protect ? L"Защита от изменений включена" : L"Наблюдение начато", active);
  WLogAppend(ws->log, hdr);
  for (i = 0; i < ws->n && i < 50; i++) {
    if (ws->w[i].disabled) continue;
    _snwprintf(hdr, 399, L"    %ls%ls", ws->w[i].root, ws->protect && !ws->w[i].rec ? L"  (только сам ключ)" : L"");
    WLogAppend(ws->log, hdr);
  }
  ws->stop = 0;
  ws->running = TRUE;
  ws->thread = CreateThread(NULL, 0, WatchThread, ws, 0, NULL);
  Log(L"%ls: областей %d, лог %ls", ws->protect ? L"Защита включена" : L"Наблюдение начато", active, ws->log);
  return TRUE;
}

static void WatchStopSet(WatchSet *ws)
{
  wchar_t ts[32], line[300];
  if (!ws->running) return;
  InterlockedExchange(&ws->stop, 1);
  if (ws->thread) { WaitForSingleObject(ws->thread, 20000); CloseHandle(ws->thread); ws->thread = NULL; }
  NowStr(ts, 32);
  _snwprintf(line, 299, L"[%ls] %ls. Событий: %lu", ts, ws->protect ? L"Защита остановлена" : L"Наблюдение остановлено", ws->total);
  WLogAppend(ws->log, line);
  WatchFreeAll(ws, TRUE);
  ScFree(&ws->scope);
  ws->running = FALSE;
  Log(L"%ls, событий %lu", ws->protect ? L"Защита остановлена" : L"Наблюдение остановлено", ws->total);
  if (g_hMain) PostMessageW(g_hMain, WM_APP_WATCH, 0, 0);
}

static void WatchStop(void) { WatchStopSet(&g_obs); }
static void WatchStopAll(void) { WatchStopSet(&g_obs); WatchStopSet(&g_prot); }

/* наблюдение за областью; целые разделы дробятся на крупные подветки */
static BOOL ObsBegin(const Scope *user, BOOL ui, const wchar_t *logOverride, BOOL conPrint)
{
  int i, k;
  WatchSet *ws = &g_obs;
  memset(ws, 0, sizeof *ws);
  ws->conPrint = conPrint;
  if (logOverride && logOverride[0]) wcsncpy(ws->log, logOverride, MAX_PATH + 15);
  for (i = 0; i < user->nexcl; i++) ScAddExcl(&ws->scope, user->excl[i]);
  for (i = 0; i < user->nroots; i++) {
    const wchar_t *root = user->roots[i];
    if (!wcschr(root, L'\\')) {
      HKEY hive = HiveFromName(root), h;
      DWORD nsub = 0, maxsub = 0, j;
      if (hive && RegOpenKeyExW(hive, L"", 0, KEY_READ | KEY_WOW64_64KEY, &h) == ERROR_SUCCESS) {
        if (RegQueryInfoKeyW(h, NULL, NULL, NULL, &nsub, &maxsub, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
          wchar_t *nm = (wchar_t *)malloc((maxsub + 2) * sizeof(wchar_t));
          for (j = 0; j < nsub; j++) {
            DWORD nl = maxsub + 1;
            wchar_t sub[2100];
            BOOL excl = FALSE;
            if (RegEnumKeyExW(h, j, nm, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) continue;
            _snwprintf(sub, 2099, L"%ls\\%ls", root, nm);
            sub[2099] = 0;
            for (k = 0; k < ws->scope.nexcl; k++) if (IsUnderPath(sub, ws->scope.excl[k])) excl = TRUE;
            if (_wcsicmp(sub, L"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet") == 0) excl = TRUE;
            if (!excl) WatchAddRoot(ws, sub, TRUE);
          }
          free(nm);
        }
        RegCloseKey(h);
      }
    } else WatchAddRoot(ws, root, TRUE);
  }
  if (ws->n == 0) { ScFree(&ws->scope); return FALSE; }
  return WatchLaunch(ws, ui);
}

/* ---- список защиты (хранится в конфигурации) ---- */
typedef struct { wchar_t path[2048]; BOOL rec; } ProtItem;
static ProtItem *g_pitems;
static int g_npitems, g_cappitems;
static BOOL g_protAuto;

static void ProtLoad(void)
{
  int i, n = (int)CfgGetInt(L"Protect", L"Count", 0);
  g_protAuto = CfgGetInt(L"Protect", L"AutoStart", 0) != 0;
  free(g_pitems);
  g_pitems = NULL; g_npitems = g_cappitems = 0;
  for (i = 0; i < n && i < 500; i++) {
    wchar_t key[32], val[2100], norm[2048];
    _snwprintf(key, 31, L"Item%d", i + 1);
    CfgGetStr(L"Protect", key, L"", val, 2100);
    if (!val[0]) continue;
    if (g_npitems == g_cappitems) { g_cappitems = g_cappitems ? g_cappitems * 2 : 16; g_pitems = (ProtItem *)realloc(g_pitems, g_cappitems * sizeof(ProtItem)); }
    if (val[1] == L'|' && NormalizeKey(val + 2, norm, 2048)) {
      wcscpy(g_pitems[g_npitems].path, norm);
      g_pitems[g_npitems].rec = val[0] != L'0';
      g_npitems++;
    }
  }
}

static void ProtSaveCfg(void)
{
  int i;
  wchar_t key[32], val[2100], num[16];
  CfgDeleteSection(L"Protect");
  _snwprintf(num, 15, L"%d", g_npitems);
  CfgSet(L"Protect", L"Count", num);
  CfgSet(L"Protect", L"AutoStart", g_protAuto ? L"1" : L"0");
  for (i = 0; i < g_npitems; i++) {
    _snwprintf(key, 31, L"Item%d", i + 1);
    _snwprintf(val, 2099, L"%c|%ls", g_pitems[i].rec ? L'1' : L'0', g_pitems[i].path);
    CfgSet(L"Protect", key, val);
  }
}

static void ProtRefreshUI(void)
{
  int i;
  if (!g_hProtLV) return;
  ListView_DeleteAllItems(g_hProtLV);
  for (i = 0; i < g_npitems; i++) {
    LVITEMW it;
    memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT; it.iItem = i; it.pszText = g_pitems[i].path;
    ListView_InsertItem(g_hProtLV, &it);
    ListView_SetItemText(g_hProtLV, i, 1, g_pitems[i].rec ? (LPWSTR)L"вся ветка" : (LPWSTR)L"только ключ");
    ListView_SetItemText(g_hProtLV, i, 2, g_prot.running ? (LPWSTR)L"защищается" : (LPWSTR)L"");
  }
  if (g_tab == 4) {
    wchar_t cnt[120];
    _snwprintf(cnt, 119, L"В списке: %d. Защита: %ls. Автозапуск: %ls", g_npitems, g_prot.running ? L"ВКЛ" : L"выкл", g_protAuto ? L"да" : L"нет");
    SetWindowTextW(g_hCount, cnt);
    SetWindowTextW(g_hBtnX[0], g_prot.running ? L"Остановить защиту" : L"Запустить защиту");
    SetWindowTextW(g_hBtnX[2], g_protAuto ? L"Автозапуск: вкл" : L"Автозапуск: выкл");
    EnableWindow(g_hBtnX[0], TRUE);
    EnableWindow(g_hBtnX[1], g_protLog[0] != 0);
    EnableWindow(g_hBtnX[2], TRUE);
    EnableWindow(g_hBtnJump, TRUE);
    EnableWindow(g_hBtnJumpNew, g_npitems > 0);
  }
}

static void ProtAddPath(const wchar_t *path, BOOL rec)
{
  wchar_t norm[2048];
  int i;
  if (!NormalizeKey(path, norm, 2048) || !wcschr(norm, L'\\')) {
    MessageBoxW(g_hMain, L"Защита целого раздела (HKEY_...) не поддерживается: укажите конкретную ветку.", APP_NAME, MB_ICONINFORMATION);
    return;
  }
  for (i = 0; i < g_npitems; i++) if (_wcsicmp(g_pitems[i].path, norm) == 0) { g_pitems[i].rec = rec; ProtSaveCfg(); ProtRefreshUI(); return; }
  if (g_npitems == g_cappitems) { g_cappitems = g_cappitems ? g_cappitems * 2 : 16; g_pitems = (ProtItem *)realloc(g_pitems, g_cappitems * sizeof(ProtItem)); }
  wcscpy(g_pitems[g_npitems].path, norm);
  g_pitems[g_npitems].rec = rec;
  g_npitems++;
  ProtSaveCfg();
  ProtRefreshUI();
  if (g_prot.running) MessageBoxW(g_hMain, L"Ветка добавлена в список. Чтобы она стала защищаться, остановите и снова запустите защиту (исходным состоянием будет считаться текущее).", APP_NAME, MB_ICONINFORMATION);
}

/* запуск защиты по списку; ui - окно прогресса */
static BOOL ProtBegin(const ProtItem *items, int n, BOOL ui, BOOL conPrint)
{
  int i;
  WatchSet *ws = &g_prot;
  memset(ws, 0, sizeof *ws);
  ws->protect = TRUE;
  ws->conPrint = conPrint;
  wcsncpy(ws->log, g_protLog, MAX_PATH + 15);
  for (i = 0; i < n; i++) {
    if (!wcschr(items[i].path, L'\\')) { Log(L"Защита: пропущен целый раздел %ls", items[i].path); continue; }
    WatchAddRoot(ws, items[i].path, items[i].rec);
  }
  if (ws->n == 0) { ScFree(&ws->scope); return FALSE; }
  return WatchLaunch(ws, ui);
}

/* ---- правка веток в текстовом редакторе ---- */
static void ClearTmpDir(void)
{
  WIN32_FIND_DATAW fd;
  HANDLE h;
  wchar_t pat[MAX_PATH + 8], full[MAX_PATH + 8];
  _snwprintf(pat, MAX_PATH + 7, L"%ls\\*", g_tmpDir);
  h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
      _snwprintf(full, MAX_PATH + 7, L"%ls\\%ls", g_tmpDir, fd.cFileName);
      SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(full); else DeleteFileW(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(g_tmpDir);
}

/* выгрузка веток в Saved\tmp; рядом кладётся неизменяемая копия оригинала *.orig.reg */
static BOOL EditExport(TargetList *tl, BOOL ui, wchar_t *editFile, wchar_t *origFile, wchar_t *err)
{
  SaveJob *job = (SaveJob *)calloc(1, sizeof *job);
  BOOL ok = FALSE;
  err[0] = 0;
  EnsureDir(g_tmpDir);
  job->tl = tl;
  wcsncpy(job->dir, g_tmpDir, MAX_PATH - 1);
  MakeTimestamp(job->ts, 32);
  job->useTag = TRUE;
  job->combine = tl->n > 1;
  if (ui) RunBusy(SaveWorker, job, L"Выгрузка для правки");
  else { InterlockedExchange(&g_cancel, 0); SaveWorker(job); }
  if (job->ok > 0 && job->firstFile[0]) {
    size_t l;
    wcscpy(editFile, job->firstFile);
    wcscpy(origFile, job->firstFile);
    l = wcslen(origFile);
    if (l > 4) wcscpy(origFile + l - 4, L".orig.reg");
    if (CopyFileW(editFile, origFile, FALSE)) { SetFileAttributesW(origFile, FILE_ATTRIBUTE_READONLY); ok = TRUE; }
    else wcscpy(err, L"не удалось сохранить копию оригинала");
  } else wcsncpy(err, job->firstErr[0] ? job->firstErr : L"не удалось выгрузить ветку", 299);
  free(job);
  return ok;
}

/* kind: 0 - редактор по умолчанию («Правка» для .reg), 1 - указанный exe, 2 - notepad.exe */
static BOOL LaunchEditor(int kind, const wchar_t *exe, const wchar_t *file, HANDLE *hp)
{
  SHELLEXECUTEINFOW si;
  wchar_t args[MAX_PATH + 8];
  BOOL ok;
  *hp = NULL;
  memset(&si, 0, sizeof si);
  si.cbSize = sizeof si;
  si.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  si.nShow = SW_SHOWNORMAL;
  if (kind == 0) {
    si.lpVerb = L"edit";
    si.lpFile = file;
    if (ShellExecuteExW(&si)) { *hp = si.hProcess; return TRUE; }
    Log(L"Правка: у .reg нет команды «Правка», открываю в notepad.exe");
    kind = 2;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    si.nShow = SW_SHOWNORMAL;
  }
  _snwprintf(args, MAX_PATH + 7, L"\"%ls\"", file);
  si.lpFile = kind == 1 ? exe : L"notepad.exe";
  si.lpParameters = args;
  ok = ShellExecuteExW(&si);
  if (ok) *hp = si.hProcess;
  return ok;
}

static void ActEditInEditor(int kind)
{
  TargetList tl;
  wchar_t editF[MAX_PATH + 16], origF[MAX_PATH + 16], err[300], exe[MAX_PATH] = L"", msg[1500];
  HANDLE hp = NULL;
  if (g_tab != 0 && !g_extraTarget[0]) { MessageBoxW(g_hMain, L"Откройте вкладку «Закладки» и выберите или отметьте ветки.", APP_NAME, MB_ICONINFORMATION); return; }
  memset(&tl, 0, sizeof tl);
  CollectTargets(&tl);
  if (tl.n == 0) { MessageBoxW(g_hMain, L"Нечего править: выберите ветку (или папку с ветками) либо отметьте ветки флажками.", APP_NAME, MB_ICONINFORMATION); free(tl.t); return; }
  if (kind == 1) {
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = g_hMain;
    o.lpstrFilter = L"Программы (*.exe)\0*.exe\0Все файлы\0*.*\0";
    o.lpstrFile = exe; o.nMaxFile = MAX_PATH; o.lpstrTitle = L"Выберите текстовый редактор";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&o)) { free(tl.t); return; }
  }
  if (!EditExport(&tl, TRUE, editF, origF, err)) {
    _snwprintf(msg, 1499, L"Не удалось выгрузить ветку для правки: %ls", err);
    MessageBoxW(g_hMain, msg, APP_NAME, MB_ICONERROR);
    free(tl.t);
    return;
  }
  free(tl.t);
  if (!LaunchEditor(kind, exe, editF, &hp)) { MessageBoxW(g_hMain, L"Не удалось запустить редактор.", APP_NAME, MB_ICONERROR); return; }
  if (hp) CloseHandle(hp);
  SavedRefresh(editF);
  SwitchTab(1);
  _snwprintf(msg, 1499, L"Файл открыт в редакторе:\n%ls\n\nОригинал сохранён (его можно вернуть в реестр при ошибке):\n%ls\n\n"
             L"Когда закончите правки и сохраните файл: вкладка «Сохранённые» → папка tmp → выберите файл → «Восстановить в реестр». "
             L"Временная папка очищается при выходе из программы%ls.", editF, origF, g_clearTmp ? L"" : L" (сейчас очистка выключена в настройках)");
  MessageBoxW(g_hMain, msg, APP_NAME, MB_ICONINFORMATION);
}
