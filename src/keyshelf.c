/*
 * KeyShelf - закладки веток реестра: папки, комментарии, переход в regedit
 * (в т.ч. в отдельном окне regedit /m), добавление веток из .reg-файлов
 * через контекстное меню. Windows 7 - 10, x86 / x64. Чистый Win32 API.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

#define APP_NAME   L"KeyShelf"
#define APP_VER    L"0.1"
#define CLS_MAIN   L"KeyShelfMainWnd"
#define CLS_DLG    L"KeyShelfRegDlg"
#define MUTEX_NAME L"KeyShelf_SingleInstance_Mutex"
#define SHELL_KEY  L"Software\\Classes\\SystemFileAssociations\\.reg\\shell\\KeyShelf"

/* ---- ID ---- */
enum {
  IDC_TREE = 100, IDC_NAME, IDC_PATH, IDC_COMMENT, IDC_JUMP, IDC_JUMPNEW, IDC_NEWWIN,
  IDM_NEWKEY = 200, IDM_NEWFOLDER, IDM_NEWSUBFOLDER, IDM_NEWROOTFOLDER, IDM_RENAME, IDM_DELETE, IDM_ADDREG, IDM_IMPORT, IDM_EXPORT, IDM_EXIT,
  IDM_OPT_NEWWIN = 210, IDM_OPT_ONTOP, IDM_OPT_SHELL, IDM_ABOUT = 220,
  IDM_JUMP = 230, IDM_JUMPNEW, IDM_COPYPATH,
  IDD_ALL = 300, IDD_NONE, IDD_TOP, IDD_SUB, IDD_COMBO, IDD_LIST
};
enum {
  IDC_TAB = 110, IDC_SAVED, IDC_COUNT, IDC_X1, IDC_X2, IDC_X3,
  IDM_SAVE = 240, IDM_EXPBAT, IDM_OPENREG_N, IDM_OPENREG_W, IDM_CHECKALL, IDM_UNCHECKALL,
  IDM_REFRESH, IDM_OPENARCH, IDM_NOTEPAD, IDM_SHOWFILE, IDM_RESTORE,
  IDE_NOTE = 400, IDE_DATE, IDE_TAG, IDE_SEP, IDE_ONE, IDE_ARCH, IDE_OTHER, IDE_COMBO, IDE_SUB, IDE_DIR,
  IDE_BROWSE, IDE_PREVIEW, IDE_NO, IDE_NW, IDE_BOTH
};
#define WM_APP_CHECK        (WM_APP + 4)
#define WM_APP_SAVEDREFRESH (WM_APP + 5)
#define CLS_EXP  L"KeyShelfExpDlg"
#define CLS_BUSY L"KeyShelfBusy"
#define WM_APP_QUEUE    (WM_APP + 2)
#define WM_APP_JUMPFAIL (WM_APP + 3)
#define TIMER_SAVE 1

/* ---- данные ---- */
typedef struct { int folder; wchar_t *path; wchar_t *comment; } NodeData;

static HINSTANCE g_hInst;
static HWND g_hMain, g_hTree, g_hName, g_hPath, g_hComment, g_hBtnJump, g_hBtnJumpNew, g_hChkNew;
static HWND g_hLbl[3];
static HFONT g_font;
static HTREEITEM g_cur;
static BOOL g_loading, g_dirty, g_busyDlg;
static BOOL g_newWin = TRUE, g_onTop = FALSE;
static int g_imgFolder, g_imgKey;
static wchar_t g_exe[MAX_PATH], g_dataDir[MAX_PATH], g_dataFile[MAX_PATH], g_iniFile[MAX_PATH], g_logFile[MAX_PATH];
static HANDLE g_jumpMutex;
static HTREEITEM g_drag;
static BOOL g_dragging;
static wchar_t *g_queue[64];
static int g_qn;
static HWND g_hTab, g_hSaved, g_hCount, g_hBtnX[3];
static int g_tab, g_imgReg;
static wchar_t g_savedDir[MAX_PATH], g_batDir[MAX_PATH], g_pendingSelect[MAX_PATH + 8];
static HTREEITEM g_curS, g_dragS;
static BOOL g_dragSaved;

/* ---- лог ---- */
static void Log(const wchar_t *fmt, ...)
{
  wchar_t buf[2048], line[2300];
  char u8[8000];
  va_list ap;
  SYSTEMTIME st;
  HANDLE h;
  DWORD w;
  if (!g_logFile[0]) return;
  va_start(ap, fmt);
  _vsnwprintf(buf, 2047, fmt, ap);
  va_end(ap);
  buf[2047] = 0;
  GetLocalTime(&st);
  _snwprintf(line, 2299, L"%04d-%02d-%02d %02d:%02d:%02d  %ls\r\n", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, buf);
  line[2299] = 0;
  WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof u8, NULL, NULL);
  h = CreateFileW(g_logFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) { WriteFile(h, u8, (DWORD)strlen(u8), &w, NULL); CloseHandle(h); }
}

/* ---- строки ---- */
static wchar_t *Dup(const wchar_t *s) { return _wcsdup(s ? s : L""); }
static void SetStr(wchar_t **d, const wchar_t *s) { wchar_t *n = Dup(s); free(*d); *d = n; }

static void Trim(wchar_t *s)
{
  size_t n = wcslen(s), i = 0;
  while (n > 0 && iswspace(s[n - 1])) s[--n] = 0;
  while (s[i] && iswspace(s[i])) i++;
  if (i) memmove(s, s + i, (wcslen(s + i) + 1) * sizeof(wchar_t));
}

typedef struct { wchar_t *p; size_t n, cap; } WBuf;
static void wb_addc(WBuf *b, wchar_t c)
{
  if (b->n + 2 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 4096;
    b->p = (wchar_t *)realloc(b->p, b->cap * sizeof(wchar_t));
  }
  b->p[b->n++] = c;
  b->p[b->n] = 0;
}
static void wb_adds(WBuf *b, const wchar_t *s) { while (*s) wb_addc(b, *s++); }
static void wb_esc(WBuf *b, const wchar_t *s)
{
  for (; s && *s; s++) {
    switch (*s) {
      case L'\\': wb_adds(b, L"\\\\"); break;
      case L'|':  wb_adds(b, L"\\p"); break;
      case L'\n': wb_adds(b, L"\\n"); break;
      case L'\r': break;
      default:    wb_addc(b, *s);
    }
  }
}
static wchar_t *Unesc(const wchar_t *s, size_t len)
{
  wchar_t *o = (wchar_t *)malloc((len * 2 + 2) * sizeof(wchar_t));
  size_t i, n = 0;
  for (i = 0; i < len; i++) {
    if (s[i] == L'\\' && i + 1 < len) {
      i++;
      if (s[i] == L'p') o[n++] = L'|';
      else if (s[i] == L'n') { o[n++] = L'\r'; o[n++] = L'\n'; }
      else o[n++] = s[i];
    } else o[n++] = s[i];
  }
  o[n] = 0;
  return o;
}

/* ---- файлы ---- */
static BYTE *ReadAllLimit(const wchar_t *file, DWORD *len, DWORD limit)
{
  HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  DWORD sz, rd = 0;
  BYTE *b;
  if (h == INVALID_HANDLE_VALUE) return NULL;
  sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE) { CloseHandle(h); return NULL; }
  if (sz > limit) sz = limit;
  b = (BYTE *)malloc(sz + 4);
  if (!ReadFile(h, b, sz, &rd, NULL)) { free(b); CloseHandle(h); return NULL; }
  CloseHandle(h);
  b[rd] = b[rd + 1] = b[rd + 2] = b[rd + 3] = 0;
  *len = rd;
  return b;
}

static BYTE *ReadAll(const wchar_t *file, DWORD *len) { return ReadAllLimit(file, len, 200u * 1024u * 1024u); }

static wchar_t *Utf8ToWide(const BYTE *b, DWORD len)
{
  int n;
  wchar_t *w;
  if (len >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { b += 3; len -= 3; }
  n = MultiByteToWideChar(CP_UTF8, 0, (const char *)b, (int)len, NULL, 0);
  w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  if (n) MultiByteToWideChar(CP_UTF8, 0, (const char *)b, (int)len, w, n);
  w[n] = 0;
  return w;
}

static BOOL WriteUtf8Atomic(const wchar_t *file, const wchar_t *text)
{
  wchar_t tmp[MAX_PATH + 8];
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  char *u = (char *)malloc(n + 1);
  HANDLE h;
  DWORD w = 0;
  BOOL ok;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, u, n, NULL, NULL);
  _snwprintf(tmp, MAX_PATH + 7, L"%ls.tmp", file);
  h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) { free(u); return FALSE; }
  ok = WriteFile(h, u, (DWORD)(n - 1), &w, NULL);
  CloseHandle(h);
  free(u);
  if (!ok) return FALSE;
  return MoveFileExW(tmp, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

/* ---- нормализация пути ветки ---- */
static const struct { const wchar_t *abbr, *full; } HIVES[] = {
  { L"HKEY_LOCAL_MACHINE", L"HKEY_LOCAL_MACHINE" }, { L"HKEY_CURRENT_USER", L"HKEY_CURRENT_USER" },
  { L"HKEY_CLASSES_ROOT", L"HKEY_CLASSES_ROOT" },   { L"HKEY_USERS", L"HKEY_USERS" },
  { L"HKEY_CURRENT_CONFIG", L"HKEY_CURRENT_CONFIG" },
  { L"HKLM", L"HKEY_LOCAL_MACHINE" }, { L"HKCU", L"HKEY_CURRENT_USER" }, { L"HKCR", L"HKEY_CLASSES_ROOT" },
  { L"HKU", L"HKEY_USERS" },          { L"HKCC", L"HKEY_CURRENT_CONFIG" }
};

static BOOL MatchHiveAt(const wchar_t *s, const wchar_t **full, int *len)
{
  size_t i;
  for (i = 0; i < sizeof HIVES / sizeof HIVES[0]; i++) {
    size_t l = wcslen(HIVES[i].abbr);
    if (_wcsnicmp(s, HIVES[i].abbr, l) == 0) {
      wchar_t c = s[l];
      if (c == 0 || c == L'\\' || c == L']' || c == L'"' || c == L' ' || c == L'\r' || c == L'\n') {
        *full = HIVES[i].full; *len = (int)l; return TRUE;
      }
    }
  }
  return FALSE;
}

/* Находит в тексте ветку реестра и возвращает её в виде HKEY_...\... */
static BOOL NormalizeKey(const wchar_t *in, wchar_t *out, size_t cap)
{
  size_t i, L;
  if (!in || !out || cap < 32) return FALSE;
  L = wcslen(in);
  for (i = 0; i < L; i++) {
    const wchar_t *full; int hl;
    if (i > 0 && (iswalnum(in[i - 1]) || in[i - 1] == L'_')) continue;
    if (MatchHiveAt(in + i, &full, &hl)) {
      wchar_t tmp[4096];
      size_t o = 0;
      const wchar_t *r = in + i + hl, *q;
      for (q = full; *q && o < 4000; q++) tmp[o++] = *q;
      if (*r != L'\\') r = L"";
      while (*r && *r != L']' && *r != L'"' && *r != L'\r' && *r != L'\n' && o < 4000) {
        if (*r == L'\\' && o > 0 && tmp[o - 1] == L'\\') { r++; continue; }
        tmp[o++] = *r++;
      }
      while (o > 0 && (tmp[o - 1] == L' ' || tmp[o - 1] == L'\\' || tmp[o - 1] == L'\t')) o--;
      tmp[o] = 0;
      wcsncpy(out, tmp, cap - 1);
      out[cap - 1] = 0;
      return TRUE;
    }
  }
  return FALSE;
}

static BOOL IsAncestorOf(const wchar_t *a, const wchar_t *b)
{
  size_t la = wcslen(a);
  return _wcsnicmp(a, b, la) == 0 && b[la] == L'\\';
}

/* ---- дерево ---- */
static NodeData *GetNode(HTREEITEM it)
{
  TVITEMW tv;
  if (!it) return NULL;
  memset(&tv, 0, sizeof tv);
  tv.mask = TVIF_PARAM;
  tv.hItem = it;
  if (!TreeView_GetItem(g_hTree, &tv)) return NULL;
  return (NodeData *)tv.lParam;
}

static void GetItemTextW2(HTREEITEM it, wchar_t *buf, int n)
{
  TVITEMW tv;
  memset(&tv, 0, sizeof tv);
  buf[0] = 0;
  tv.mask = TVIF_TEXT;
  tv.hItem = it;
  tv.pszText = buf;
  tv.cchTextMax = n;
  TreeView_GetItem(g_hTree, &tv);
}

static void SetItemTextW2(HTREEITEM it, const wchar_t *text)
{
  TVITEMW tv;
  memset(&tv, 0, sizeof tv);
  tv.mask = TVIF_TEXT;
  tv.hItem = it;
  tv.pszText = (LPWSTR)text;
  TreeView_SetItem(g_hTree, &tv);
}

static HTREEITEM FirstChild(HTREEITEM parent)
{
  if (parent == NULL || parent == TVI_ROOT) return TreeView_GetRoot(g_hTree);
  return TreeView_GetChild(g_hTree, parent);
}

static HTREEITEM InsertNode(HTREEITEM parent, HTREEITEM after, const wchar_t *name, BOOL folder,
                            const wchar_t *path, const wchar_t *comment)
{
  TVINSERTSTRUCTW is;
  NodeData *nd = (NodeData *)calloc(1, sizeof *nd);
  nd->folder = folder;
  nd->path = Dup(path);
  nd->comment = Dup(comment);
  memset(&is, 0, sizeof is);
  is.hParent = parent ? parent : TVI_ROOT;
  is.hInsertAfter = after ? after : TVI_LAST;
  is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
  is.item.pszText = (LPWSTR)name;
  is.item.lParam = (LPARAM)nd;
  is.item.iImage = is.item.iSelectedImage = folder ? g_imgFolder : g_imgKey;
  return TreeView_InsertItem(g_hTree, &is);
}

static void ExpandAll(HTREEITEM parent)
{
  HTREEITEM it;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (nd && nd->folder) { ExpandAll(it); TreeView_Expand(g_hTree, it, TVE_EXPAND); }
  }
}

static HTREEITEM CopySub(HTREEITEM src, HTREEITEM parent, HTREEITEM after)
{
  NodeData *nd = GetNode(src);
  wchar_t name[300];
  HTREEITEM n, c;
  GetItemTextW2(src, name, 300);
  n = InsertNode(parent, after, name, nd->folder, nd->path, nd->comment);
  for (c = TreeView_GetChild(g_hTree, src); c; c = TreeView_GetNextSibling(g_hTree, c)) CopySub(c, n, TVI_LAST);
  if (TreeView_GetItemState(g_hTree, src, TVIS_EXPANDED) & TVIS_EXPANDED) TreeView_Expand(g_hTree, n, TVE_EXPAND);
  return n;
}

static BOOL IsDescendant(HTREEITEM item, HTREEITEM anc)
{
  while (item) {
    if (item == anc) return TRUE;
    item = TreeView_GetParent(g_hTree, item);
  }
  return FALSE;
}

/* ---- сохранение / загрузка списка ---- */
static void MarkDirty(void)
{
  g_dirty = TRUE;
  SetTimer(g_hMain, TIMER_SAVE, 1500, NULL);
}

static void CommitEdits(void)
{
  NodeData *nd;
  wchar_t *name, *cm, old[300];
  if (!g_cur || g_tab != 0) return;
  nd = GetNode(g_cur);
  if (!nd) return;
  name = (wchar_t *)malloc((GetWindowTextLengthW(g_hName) + 2) * sizeof(wchar_t));
  GetWindowTextW(g_hName, name, GetWindowTextLengthW(g_hName) + 1);
  Trim(name);
  if (name[0]) {
    GetItemTextW2(g_cur, old, 300);
    if (wcscmp(old, name) != 0) SetItemTextW2(g_cur, name);
  }
  free(name);
  cm = (wchar_t *)malloc((GetWindowTextLengthW(g_hComment) + 2) * sizeof(wchar_t));
  GetWindowTextW(g_hComment, cm, GetWindowTextLengthW(g_hComment) + 1);
  SetStr(&nd->comment, cm);
  free(cm);
  if (!nd->folder) {
    wchar_t *p = (wchar_t *)malloc((GetWindowTextLengthW(g_hPath) + 2) * sizeof(wchar_t));
    wchar_t norm[2048];
    GetWindowTextW(g_hPath, p, GetWindowTextLengthW(g_hPath) + 1);
    if (NormalizeKey(p, norm, 2048)) SetStr(&nd->path, norm);
    else { Trim(p); SetStr(&nd->path, p); }
    free(p);
  }
}

static void SaveRec(WBuf *b, HTREEITEM parent, int depth)
{
  HTREEITEM it;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    wchar_t name[300], num[16];
    if (!nd) continue;
    GetItemTextW2(it, name, 300);
    wb_addc(b, nd->folder ? L'F' : L'K');
    wb_addc(b, L'|');
    _snwprintf(num, 15, L"%d", depth);
    wb_adds(b, num);
    wb_addc(b, L'|');
    wb_esc(b, name);
    wb_addc(b, L'|');
    wb_esc(b, nd->path);
    wb_addc(b, L'|');
    wb_esc(b, nd->comment);
    wb_adds(b, L"\r\n");
    if (nd->folder) SaveRec(b, it, depth + 1);
  }
}

static BOOL SaveTo(const wchar_t *file)
{
  WBuf b = { 0 };
  BOOL ok;
  wb_adds(&b, L"KEYSHELF|1\r\n");
  SaveRec(&b, TVI_ROOT, 0);
  ok = WriteUtf8Atomic(file, b.p);
  free(b.p);
  return ok;
}

static void SaveAll(void)
{
  CommitEdits();
  if (SaveTo(g_dataFile)) g_dirty = FALSE;
  else Log(L"ОШИБКА: не удалось записать %ls (err %lu)", g_dataFile, GetLastError());
}

static BOOL LoadFrom(const wchar_t *file, HTREEITEM base)
{
  DWORD len;
  BYTE *raw = ReadAll(file, &len);
  wchar_t *txt, *p;
  HTREEITEM stack[70];
  int maxd = 0;
  if (!raw) return FALSE;
  txt = Utf8ToWide(raw, len);
  free(raw);
  stack[0] = base ? base : TVI_ROOT;
  p = txt;
  while (*p) {
    wchar_t *eol = p, *f[5];
    int nf = 0, d, i;
    wchar_t *start = p;
    size_t flen[5];
    while (*eol && *eol != L'\n') eol++;
    {
      wchar_t *q = start, *fs = start;
      for (i = 0; i < 5; i++) { f[i] = NULL; flen[i] = 0; }
      for (; q <= eol; q++) {
        if (q == eol || (*q == L'|' && nf < 4)) {
          size_t l = (size_t)(q - fs);
          if (l && fs[l - 1] == L'\r') l--;
          f[nf] = fs; flen[nf] = l; nf++;
          fs = q + 1;
          if (q == eol) break;
        }
      }
    }
    if (nf >= 3 && flen[0] == 1 && (f[0][0] == L'F' || f[0][0] == L'K')) {
      wchar_t dn[16], *name, *path, *cm;
      HTREEITEM it;
      BOOL folder = (f[0][0] == L'F');
      size_t dl = flen[1] < 15 ? flen[1] : 15;
      wcsncpy(dn, f[1], dl); dn[dl] = 0;
      d = _wtoi(dn);
      if (d < 0) d = 0;
      if (d > maxd) d = maxd;
      if (d > 62) d = 62;
      name = Unesc(f[2], flen[2]);
      path = nf > 3 ? Unesc(f[3], flen[3]) : Dup(L"");
      cm = nf > 4 ? Unesc(f[4], flen[4]) : Dup(L"");
      it = InsertNode(stack[d], TVI_LAST, name, folder, path, cm);
      free(name); free(path); free(cm);
      if (folder) { stack[d + 1] = it; maxd = d + 1; } else maxd = d;
    }
    p = *eol ? eol + 1 : eol;
  }
  free(txt);
  return TRUE;
}

/* ---- интерфейс: показ выбранного ---- */
static void ShowNode(HTREEITEM it)
{
  NodeData *nd = GetNode(it);
  wchar_t name[300];
  if (g_tab != 0) return;
  g_loading = TRUE;
  if (!nd) {
    SetWindowTextW(g_hName, L""); SetWindowTextW(g_hPath, L""); SetWindowTextW(g_hComment, L"");
    EnableWindow(g_hName, FALSE); EnableWindow(g_hPath, FALSE); EnableWindow(g_hComment, FALSE);
    EnableWindow(g_hBtnJump, FALSE); EnableWindow(g_hBtnJumpNew, FALSE);
  } else {
    GetItemTextW2(it, name, 300);
    SetWindowTextW(g_hName, name);
    SetWindowTextW(g_hPath, nd->folder ? L"" : nd->path);
    SetWindowTextW(g_hComment, nd->comment);
    EnableWindow(g_hName, TRUE);
    EnableWindow(g_hPath, !nd->folder);
    EnableWindow(g_hComment, TRUE);
    EnableWindow(g_hBtnJump, !nd->folder);
    EnableWindow(g_hBtnJumpNew, !nd->folder);
  }
  EnableWindow(g_hBtnX[0], nd != NULL);
  EnableWindow(g_hBtnX[1], nd != NULL);
  EnableWindow(g_hBtnX[2], TRUE);
  g_loading = FALSE;
}

/* ---- regedit ---- */
static BOOL CALLBACK EnumCnt(HWND h, LPARAM lp)
{
  wchar_t cls[64];
  GetClassNameW(h, cls, 64);
  if (wcscmp(cls, L"RegEdit_RegEdit") == 0) (*(int *)lp)++;
  return TRUE;
}
static BOOL CALLBACK EnumClose(HWND h, LPARAM lp)
{
  wchar_t cls[64];
  (void)lp;
  GetClassNameW(h, cls, 64);
  if (wcscmp(cls, L"RegEdit_RegEdit") == 0) PostMessageW(h, WM_CLOSE, 0, 0);
  return TRUE;
}
static int CountRegedit(void) { int n = 0; EnumWindows(EnumCnt, (LPARAM)&n); return n; }

static BOOL WriteLastKey(const wchar_t *path)
{
  HKEY k;
  wchar_t root[128] = L"", old[2048] = L"", full[4200];
  DWORD sz = sizeof old - 4, type = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit", 0, NULL, 0,
                      KEY_READ | KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS)
    return FALSE;
  GetPrivateProfileStringW(L"Regedit", L"RootName", L"", root, 128, g_iniFile);
  if (!root[0]) {
    if (RegQueryValueExW(k, L"LastKey", NULL, &type, (BYTE *)old, &sz) == ERROR_SUCCESS && type == REG_SZ) {
      wchar_t *bs = wcschr(old, L'\\');
      if (bs) { *bs = 0; if (_wcsnicmp(old, L"HKEY_", 5) != 0 && wcslen(old) < 100) wcscpy(root, old); }
    }
  }
  if (!root[0]) wcscpy(root, L"Computer");
  _snwprintf(full, 4199, L"%ls\\%ls", root, path);
  full[4199] = 0;
  RegSetValueExW(k, L"LastKey", 0, REG_SZ, (const BYTE *)full, (DWORD)((wcslen(full) + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  Log(L"LastKey = %ls", full);
  return TRUE;
}

typedef struct { wchar_t path[2048]; BOOL newWin; } JumpReq;

static DWORD WINAPI JumpThread(LPVOID param)
{
  JumpReq *r = (JumpReq *)param;
  wchar_t regedit[MAX_PATH];
  int n0, i;
  BOOL newWin = r->newWin;
  HINSTANCE h;
  WaitForSingleObject(g_jumpMutex, INFINITE);
  GetWindowsDirectoryW(regedit, MAX_PATH);
  wcscat(regedit, L"\\regedit.exe");
  n0 = CountRegedit();
  Log(L"Переход: %ls (режим %ls, открытых regedit: %d)", r->path, newWin ? L"/m" : L"обычный", n0);
  if (!newWin && n0 > 0) {
    EnumWindows(EnumClose, 0);
    for (i = 0; i < 25 && CountRegedit() > 0; i++) Sleep(100);
    n0 = CountRegedit();
    if (n0 > 0) { newWin = TRUE; Log(L"Старый regedit не закрылся (скорее всего запущен от админа) - открываю с /m"); }
  }
  if (!WriteLastKey(r->path)) {
    Log(L"ОШИБКА: не удалось записать LastKey");
    PostMessageW(g_hMain, WM_APP_JUMPFAIL, 1, 0);
  } else {
    h = ShellExecuteW(NULL, L"open", regedit, newWin ? L"/m" : NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) {
      Log(L"ОШИБКА ShellExecute: код %d", (int)(INT_PTR)h);
      PostMessageW(g_hMain, WM_APP_JUMPFAIL, 2, (LPARAM)(INT_PTR)h);
    } else {
      for (i = 0; i < 150; i++) { Sleep(100); if (CountRegedit() > n0) break; }
      Log(L"regedit: окон было %d, стало %d", n0, CountRegedit());
      Sleep(1000); /* дать regedit прочитать LastKey, прежде чем следующий переход его перезапишет */
    }
  }
  ReleaseMutex(g_jumpMutex);
  free(r);
  return 0;
}

static void DoJump(const wchar_t *rawpath, BOOL newWin)
{
  JumpReq *r = (JumpReq *)calloc(1, sizeof *r);
  HANDLE t;
  if (!NormalizeKey(rawpath, r->path, 2048)) {
    free(r);
    MessageBoxW(g_hMain, L"Не удалось распознать ветку реестра.\n\nПуть должен начинаться с HKEY_LOCAL_MACHINE, "
                L"HKEY_CURRENT_USER, HKEY_CLASSES_ROOT, HKEY_USERS, HKEY_CURRENT_CONFIG (или HKLM, HKCU, HKCR, HKU, HKCC).",
                APP_NAME, MB_ICONWARNING);
    return;
  }
  r->newWin = newWin;
  t = CreateThread(NULL, 0, JumpThread, r, 0, NULL);
  if (t) CloseHandle(t); else free(r);
}

static void ActJump(BOOL forceNew)
{
  NodeData *nd;
  CommitEdits();
  nd = GetNode(g_cur);
  if (!nd || nd->folder) return;
  DoJump(nd->path, forceNew ? TRUE : g_newWin);
}

static void AddRegFile(const wchar_t *file);

/* ---- действия над деревом ---- */
static void GetInsertTarget(HTREEITEM *parent, HTREEITEM *after)
{
  NodeData *nd = GetNode(g_cur);
  *after = TVI_LAST;
  if (!nd) { *parent = TVI_ROOT; return; }
  if (nd->folder) { *parent = g_cur; return; }
  *parent = TreeView_GetParent(g_hTree, g_cur);
  if (!*parent) *parent = TVI_ROOT;
  *after = g_cur;
}

static BOOL GetClipboardKey(wchar_t *out, size_t cap)
{
  BOOL ok = FALSE;
  HANDLE h;
  if (!OpenClipboard(g_hMain)) return FALSE;
  h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    wchar_t *p = (wchar_t *)GlobalLock(h);
    if (p) { ok = NormalizeKey(p, out, cap); GlobalUnlock(h); }
  }
  CloseClipboard();
  return ok;
}

static void ActNewKey(void)
{
  HTREEITEM parent, after, it;
  wchar_t path[2048] = L"", name[300] = L"Новая ветка";
  CommitEdits();
  GetInsertTarget(&parent, &after);
  if (GetClipboardKey(path, 2048)) {
    wchar_t *sl = wcsrchr(path, L'\\');
    wcsncpy(name, sl ? sl + 1 : path, 255);
    name[255] = 0;
  }
  it = InsertNode(parent, after, name, FALSE, path, L"");
  if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
  TreeView_SelectItem(g_hTree, it);
  MarkDirty();
  if (path[0]) { SetFocus(g_hComment); } else { SetFocus(g_hPath); }
}

/* mode: 0 - на том же уровне, что выбранный элемент; 1 - внутри выбранной папки; 2 - в корне списка */
static void ActNewFolder(int mode)
{
  HTREEITEM parent = TVI_ROOT, after = TVI_LAST, it, p;
  NodeData *nd;
  CommitEdits();
  nd = GetNode(g_cur);
  if (mode == 2 || !nd) { parent = TVI_ROOT; after = TVI_LAST; }
  else if (mode == 1 && nd->folder) { parent = g_cur; after = TVI_LAST; }
  else {
    p = TreeView_GetParent(g_hTree, g_cur);
    parent = p ? p : TVI_ROOT;
    after = g_cur;
  }
  it = InsertNode(parent, after, L"Новая папка", TRUE, L"", L"");
  if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
  TreeView_SelectItem(g_hTree, it);
  MarkDirty();
  SetFocus(g_hTree);
  TreeView_EditLabel(g_hTree, it);
}

static void ActRename(void)
{
  if (!g_cur) return;
  CommitEdits();
  SetFocus(g_hTree);
  TreeView_EditLabel(g_hTree, g_cur);
}

static void ActDelete(void)
{
  HTREEITEM it = g_cur;
  NodeData *nd = GetNode(it);
  wchar_t name[300], msg[700];
  if (!nd) return;
  GetItemTextW2(it, name, 300);
  if (nd->folder && TreeView_GetChild(g_hTree, it))
    _snwprintf(msg, 699, L"Удалить папку «%ls» вместе со всем содержимым?", name);
  else
    _snwprintf(msg, 699, L"Удалить «%ls»?", name);
  msg[699] = 0;
  if (MessageBoxW(g_hMain, msg, APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  g_cur = NULL;
  TreeView_DeleteItem(g_hTree, it);
  if (!TreeView_GetSelection(g_hTree)) ShowNode(NULL);
  MarkDirty();
}

static void ActCopyPath(void)
{
  NodeData *nd;
  HGLOBAL g;
  wchar_t *p;
  size_t n;
  CommitEdits();
  nd = GetNode(g_cur);
  if (!nd || nd->folder || !nd->path[0]) return;
  n = (wcslen(nd->path) + 1) * sizeof(wchar_t);
  g = GlobalAlloc(GMEM_MOVEABLE, n);
  if (!g) return;
  p = (wchar_t *)GlobalLock(g);
  memcpy(p, nd->path, n);
  GlobalUnlock(g);
  if (OpenClipboard(g_hMain)) { EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, g); CloseClipboard(); }
  else GlobalFree(g);
}

static void ActAddReg(void)
{
  OPENFILENAMEW o;
  wchar_t f[MAX_PATH] = L"";
  memset(&o, 0, sizeof o);
  o.lStructSize = sizeof o; o.hwndOwner = g_hMain;
  o.lpstrFilter = L"Файлы реестра (*.reg)\0*.reg\0Все файлы\0*.*\0";
  o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrDefExt = L"reg";
  o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&o)) return;
  AddRegFile(f);
}

static void ActExport(void)
{
  OPENFILENAMEW o;
  wchar_t f[MAX_PATH] = L"KeyShelf.ksl";
  memset(&o, 0, sizeof o);
  o.lStructSize = sizeof o; o.hwndOwner = g_hMain;
  o.lpstrFilter = L"Списки KeyShelf (*.ksl)\0*.ksl\0Все файлы\0*.*\0";
  o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrDefExt = L"ksl";
  o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&o)) return;
  CommitEdits();
  if (!SaveTo(f)) MessageBoxW(g_hMain, L"Не удалось записать файл.", APP_NAME, MB_ICONERROR);
}

static void ActImport(void)
{
  OPENFILENAMEW o;
  wchar_t f[MAX_PATH] = L"", title[300];
  HTREEITEM folder;
  memset(&o, 0, sizeof o);
  o.lStructSize = sizeof o; o.hwndOwner = g_hMain;
  o.lpstrFilter = L"Списки KeyShelf (*.ksl)\0*.ksl\0Все файлы\0*.*\0";
  o.lpstrFile = f; o.nMaxFile = MAX_PATH;
  o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&o)) return;
  CommitEdits();
  _snwprintf(title, 299, L"Импорт: %ls", PathFindFileNameW(f));
  title[299] = 0;
  folder = InsertNode(TVI_ROOT, TVI_LAST, title, TRUE, L"", L"Импортировано из файла");
  if (!LoadFrom(f, folder)) {
    TreeView_DeleteItem(g_hTree, folder);
    MessageBoxW(g_hMain, L"Не удалось прочитать файл.", APP_NAME, MB_ICONERROR);
    return;
  }
  ExpandAll(folder);
  TreeView_Expand(g_hTree, folder, TVE_EXPAND);
  TreeView_SelectItem(g_hTree, folder);
  MarkDirty();
}

/* ---- пункт в меню .reg ---- */
static BOOL ShellMenuRegistered(void)
{
  HKEY k;
  LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, SHELL_KEY, 0, KEY_READ, &k);
  if (r == ERROR_SUCCESS) RegCloseKey(k);
  return r == ERROR_SUCCESS;
}

static BOOL SetShellMenu(BOOL on)
{
  HKEY k;
  wchar_t cmd[MAX_PATH + 40], icon[MAX_PATH + 8];
  if (!on) {
    LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, SHELL_KEY);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
  }
  if (RegCreateKeyExW(HKEY_CURRENT_USER, SHELL_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return FALSE;
  {
    const wchar_t *label = L"Добавить в KeyShelf";
    RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)label, (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
    _snwprintf(icon, MAX_PATH + 7, L"%ls,0", g_exe);
    icon[MAX_PATH + 7] = 0;
  }
  RegCloseKey(k);
  {
    wchar_t sub[256];
    _snwprintf(sub, 255, L"%ls\\command", SHELL_KEY);
    sub[255] = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return FALSE;
    _snwprintf(cmd, MAX_PATH + 39, L"\"%ls\" /addreg \"%%1\"", g_exe);
    cmd[MAX_PATH + 39] = 0;
    RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
  }
  return TRUE;
}

/* ---- разбор .reg ---- */
typedef struct { wchar_t **keys; int n, cap; } KeyList;

static void kl_add(KeyList *l, const wchar_t *s)
{
  int i;
  for (i = 0; i < l->n; i++) if (_wcsicmp(l->keys[i], s) == 0) return;
  if (l->n >= 50000) return;
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 256;
    l->keys = (wchar_t **)realloc(l->keys, l->cap * sizeof(wchar_t *));
  }
  l->keys[l->n++] = Dup(s);
}
static void kl_free(KeyList *l)
{
  int i;
  for (i = 0; i < l->n; i++) free(l->keys[i]);
  free(l->keys);
  memset(l, 0, sizeof *l);
}

static BOOL ParseRegFileLimit(const wchar_t *file, KeyList *out, DWORD limit)
{
  DWORD len;
  BYTE *raw = ReadAllLimit(file, &len, limit);
  wchar_t *txt, *p;
  BOOL freeTxt = TRUE;
  if (!raw) return FALSE;
  if (len >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
    size_t cnt = (len - 2) / 2;
    txt = (wchar_t *)malloc((cnt + 1) * sizeof(wchar_t));
    memcpy(txt, raw + 2, cnt * sizeof(wchar_t));
    txt[cnt] = 0;
  } else {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)raw, (int)len, NULL, 0);
    UINT cp = n ? CP_UTF8 : CP_ACP;
    const BYTE *src = raw;
    DWORD sl = len;
    if (len >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { src += 3; sl -= 3; cp = CP_UTF8; }
    n = MultiByteToWideChar(cp, 0, (const char *)src, (int)sl, NULL, 0);
    txt = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (n) MultiByteToWideChar(cp, 0, (const char *)src, (int)sl, txt, n);
    txt[n] = 0;
  }
  free(raw);
  p = txt;
  while (*p) {
    wchar_t *eol = p;
    while (*eol && *eol != L'\n') eol++;
    while (p < eol && (*p == L' ' || *p == L'\t')) p++;
    if (*p == L'[' && p[1] != L'-') {
      wchar_t line[4096], norm[2048];
      size_t l = (size_t)(eol - p);
      if (l > 4095) l = 4095;
      wcsncpy(line, p, l); line[l] = 0;
      if (NormalizeKey(line, norm, 2048)) kl_add(out, norm);
    }
    p = *eol ? eol + 1 : eol;
  }
  if (freeTxt) free(txt);
  return TRUE;
}

static BOOL ParseRegFile(const wchar_t *file, KeyList *out) { return ParseRegFileLimit(file, out, 200u * 1024u * 1024u); }

/* ---- диалог добавления из .reg ---- */
typedef struct {
  HWND hwnd, lv, chkTop, chkSub, combo;
  KeyList *keys;
  const wchar_t *fileName;
  BOOL done, ok, makeSub;
  HTREEITEM dest;
  BOOL *sel;
  HTREEITEM defaultDest;
} RegDlg;

static void FillFolderCombo(HWND combo, HTREEITEM parent, int depth, HTREEITEM def)
{
  HTREEITEM it;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (nd && nd->folder) {
      wchar_t name[300], lbl[400];
      int idx, i, pos = 0;
      GetItemTextW2(it, name, 300);
      for (i = 0; i < depth * 3 && pos < 60; i++) lbl[pos++] = L' ';
      lbl[pos] = 0;
      wcsncat(lbl, name, 300);
      idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lbl);
      SendMessageW(combo, CB_SETITEMDATA, idx, (LPARAM)it);
      if (it == def) SendMessageW(combo, CB_SETCURSEL, idx, 0);
      FillFolderCombo(combo, it, depth + 1, def);
    }
  }
}

static void DlgSetFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); }

static void DlgApplyTop(RegDlg *d, BOOL onlyTop)
{
  int i, j;
  SendMessageW(d->lv, WM_SETREDRAW, FALSE, 0);
  for (i = 0; i < d->keys->n; i++) {
    BOOL check = TRUE;
    if (onlyTop) {
      for (j = 0; j < d->keys->n; j++)
        if (j != i && IsAncestorOf(d->keys->keys[j], d->keys->keys[i])) { check = FALSE; break; }
    }
    ListView_SetCheckState(d->lv, i, check);
  }
  SendMessageW(d->lv, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(d->lv, NULL, TRUE);
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  RegDlg *d = (RegDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
      HWND c;
      LVCOLUMNW col;
      LVITEMW li;
      wchar_t info[600];
      int i;
      d = (RegDlg *)cs->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
      d->hwnd = h;
      _snwprintf(info, 599, L"Файл: %ls  —  найдено веток: %d", d->fileName, d->keys->n);
      info[599] = 0;
      c = CreateWindowExW(0, L"STATIC", info, WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 8, 8, 584, 18, h, (HMENU)-1, g_hInst, NULL);
      DlgSetFont(c);
      d->lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                              LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 8, 30, 584, 260, h, (HMENU)IDD_LIST, g_hInst, NULL);
      DlgSetFont(d->lv);
      ListView_SetExtendedListViewStyle(d->lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
      memset(&col, 0, sizeof col);
      col.mask = LVCF_WIDTH; col.cx = 560;
      ListView_InsertColumn(d->lv, 0, &col);
      SendMessageW(d->lv, WM_SETREDRAW, FALSE, 0);
      for (i = 0; i < d->keys->n; i++) {
        memset(&li, 0, sizeof li);
        li.mask = LVIF_TEXT; li.iItem = i; li.pszText = d->keys->keys[i];
        ListView_InsertItem(d->lv, &li);
        ListView_SetCheckState(d->lv, i, TRUE);
      }
      SendMessageW(d->lv, WM_SETREDRAW, TRUE, 0);
      d->chkTop = CreateWindowExW(0, L"BUTTON", L"Только верхние ветки (без вложенных подветок)", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                  BS_AUTOCHECKBOX, 8, 296, 584, 20, h, (HMENU)IDD_TOP, g_hInst, NULL);
      DlgSetFont(d->chkTop);
      d->chkSub = CreateWindowExW(0, L"BUTTON", L"Создать подпапку с именем файла", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                  BS_AUTOCHECKBOX, 8, 320, 584, 20, h, (HMENU)IDD_SUB, g_hInst, NULL);
      DlgSetFont(d->chkSub);
      SendMessageW(d->chkSub, BM_SETCHECK, BST_CHECKED, 0);
      c = CreateWindowExW(0, L"STATIC", L"Папка назначения:", WS_CHILD | WS_VISIBLE, 8, 348, 584, 16, h, (HMENU)-1, g_hInst, NULL);
      DlgSetFont(c);
      d->combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                 8, 366, 584, 220, h, (HMENU)IDD_COMBO, g_hInst, NULL);
      DlgSetFont(d->combo);
      SendMessageW(d->combo, CB_ADDSTRING, 0, (LPARAM)L"(корень списка)");
      SendMessageW(d->combo, CB_SETITEMDATA, 0, (LPARAM)TVI_ROOT);
      SendMessageW(d->combo, CB_SETCURSEL, 0, 0);
      FillFolderCombo(d->combo, TVI_ROOT, 0, d->defaultDest);
      c = CreateWindowExW(0, L"BUTTON", L"Выбрать все", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, 432, 100, 26, h, (HMENU)IDD_ALL, g_hInst, NULL);
      DlgSetFont(c);
      c = CreateWindowExW(0, L"BUTTON", L"Снять все", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 116, 432, 100, 26, h, (HMENU)IDD_NONE, g_hInst, NULL);
      DlgSetFont(c);
      c = CreateWindowExW(0, L"BUTTON", L"Добавить", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 412, 432, 86, 26, h, (HMENU)IDOK, g_hInst, NULL);
      DlgSetFont(c);
      c = CreateWindowExW(0, L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 506, 432, 86, 26, h, (HMENU)IDCANCEL, g_hInst, NULL);
      DlgSetFont(c);
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDD_TOP: DlgApplyTop(d, SendMessageW(d->chkTop, BM_GETCHECK, 0, 0) == BST_CHECKED); return 0;
        case IDD_ALL: SendMessageW(d->chkTop, BM_SETCHECK, BST_UNCHECKED, 0); DlgApplyTop(d, FALSE); return 0;
        case IDD_NONE: {
          int i;
          for (i = 0; i < d->keys->n; i++) ListView_SetCheckState(d->lv, i, FALSE);
          return 0;
        }
        case IDOK: {
          int i, cs = (int)SendMessageW(d->combo, CB_GETCURSEL, 0, 0);
          d->sel = (BOOL *)calloc(d->keys->n + 1, sizeof(BOOL));
          for (i = 0; i < d->keys->n; i++) d->sel[i] = ListView_GetCheckState(d->lv, i) ? TRUE : FALSE;
          d->dest = cs >= 0 ? (HTREEITEM)SendMessageW(d->combo, CB_GETITEMDATA, cs, 0) : TVI_ROOT;
          d->makeSub = SendMessageW(d->chkSub, BM_GETCHECK, 0, 0) == BST_CHECKED;
          d->ok = TRUE; d->done = TRUE;
          return 0;
        }
        case IDCANCEL: d->done = TRUE; return 0;
      }
      break;
    case WM_CLOSE: if (d) d->done = TRUE; return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static BOOL RunRegDialog(RegDlg *d)
{
  RECT r = { 0, 0, 600, 470 }, orc;
  MSG msg;
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  int w, hgt, x, y;
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  w = r.right - r.left; hgt = r.bottom - r.top;
  GetWindowRect(g_hMain, &orc);
  x = orc.left + ((orc.right - orc.left) - w) / 2;
  y = orc.top + ((orc.bottom - orc.top) - hgt) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  EnableWindow(g_hMain, FALSE);
  d->hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLS_DLG, L"Добавить ветки из .reg-файла", style, x, y, w, hgt, g_hMain, NULL, g_hInst, d);
  if (!d->hwnd) { EnableWindow(g_hMain, TRUE); return FALSE; }
  ShowWindow(d->hwnd, SW_SHOW);
  SetForegroundWindow(d->hwnd);
  while (!d->done) {
    int rc = GetMessageW(&msg, NULL, 0, 0);
    if (rc <= 0) { if (rc == 0) PostQuitMessage((int)msg.wParam); break; }
    if (!IsDialogMessageW(d->hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  EnableWindow(g_hMain, TRUE);
  DestroyWindow(d->hwnd);
  SetForegroundWindow(g_hMain);
  return d->ok;
}

static void AddRegFile(const wchar_t *file)
{
  KeyList kl;
  RegDlg d;
  wchar_t title[300], fname[300], cm[400];
  memset(&kl, 0, sizeof kl);
  Log(L"Добавление из .reg: %ls", file);
  if (!ParseRegFile(file, &kl) || kl.n == 0) {
    MessageBoxW(g_hMain, L"В этом файле не найдено веток реестра (или файл не удалось прочитать).", APP_NAME, MB_ICONINFORMATION);
    kl_free(&kl);
    return;
  }
  CommitEdits();
  memset(&d, 0, sizeof d);
  d.keys = &kl;
  wcsncpy(fname, PathFindFileNameW(file), 299); fname[299] = 0;
  d.fileName = fname;
  {
    NodeData *nd = GetNode(g_cur);
    d.defaultDest = TVI_ROOT;
    if (nd) d.defaultDest = nd->folder ? g_cur : (TreeView_GetParent(g_hTree, g_cur) ? TreeView_GetParent(g_hTree, g_cur) : TVI_ROOT);
  }
  g_busyDlg = TRUE;
  RunRegDialog(&d);
  g_busyDlg = FALSE;
  if (d.ok) {
    HTREEITEM parent = d.dest ? d.dest : TVI_ROOT, first = NULL;
    int i, cnt = 0;
    if (d.makeSub) {
      wcsncpy(title, fname, 299); title[299] = 0;
      PathRemoveExtensionW(title);
      parent = InsertNode(parent, TVI_LAST, title, TRUE, L"", L"Создано из .reg-файла");
    }
    _snwprintf(cm, 399, L"Из файла: %ls", fname);
    cm[399] = 0;
    for (i = 0; i < kl.n; i++) {
      if (d.sel && d.sel[i]) {
        wchar_t *sl = wcsrchr(kl.keys[i], L'\\');
        wchar_t name[300];
        HTREEITEM it;
        wcsncpy(name, sl ? sl + 1 : kl.keys[i], 255);
        name[255] = 0;
        it = InsertNode(parent, TVI_LAST, name, FALSE, kl.keys[i], cm);
        if (!first) first = it;
        cnt++;
      }
    }
    if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
    if (first) TreeView_SelectItem(g_hTree, first);
    if (cnt) MarkDirty();
    Log(L"Добавлено веток: %d", cnt);
  }
  free(d.sel);
  kl_free(&kl);
}

static void ProcessQueue(void)
{
  if (g_busyDlg) return;
  while (g_qn > 0) {
    wchar_t *f = g_queue[0];
    memmove(g_queue, g_queue + 1, (g_qn - 1) * sizeof(wchar_t *));
    g_qn--;
    AddRegFile(f);
    free(f);
  }
}

static void EnqueueFile(const wchar_t *f)
{
  if (g_qn < 64) g_queue[g_qn++] = Dup(f);
  PostMessageW(g_hMain, WM_APP_QUEUE, 0, 0);
}

/* ====================================================================
 *  v0.2: имена файлов, bat-скрипты, сохранение веток (.reg), архив,
 *        групповые операции, открытие regedit без перехода
 * ==================================================================== */
static CRITICAL_SECTION g_cs;
static wchar_t g_busyText[500];
static volatile LONG g_cancel;
static HANDLE g_curProc;

static HWND Mk(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, DWORD ex)
{
  HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
  SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
  return c;
}

static NodeData *GetNodeIn(HWND tv, HTREEITEM it)
{
  TVITEMW t;
  if (!it) return NULL;
  memset(&t, 0, sizeof t);
  t.mask = TVIF_PARAM;
  t.hItem = it;
  if (!SendMessageW(tv, TVM_GETITEMW, 0, (LPARAM)&t)) return NULL;
  return (NodeData *)t.lParam;
}

static void GetItemTextIn(HWND tv, HTREEITEM it, wchar_t *buf, int n)
{
  TVITEMW t;
  memset(&t, 0, sizeof t);
  buf[0] = 0;
  t.mask = TVIF_TEXT; t.hItem = it; t.pszText = buf; t.cchTextMax = n;
  SendMessageW(tv, TVM_GETITEMW, 0, (LPARAM)&t);
}

static HTREEITEM FirstChildIn(HWND tv, HTREEITEM parent)
{
  if (!parent || parent == TVI_ROOT) return (HTREEITEM)SendMessageW(tv, TVM_GETNEXTITEM, TVGN_ROOT, 0);
  return (HTREEITEM)SendMessageW(tv, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)parent);
}
static HTREEITEM NextSibIn(HWND tv, HTREEITEM it) { return (HTREEITEM)SendMessageW(tv, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)it); }

/* ---- имена ---- */
static void SanitizeName(wchar_t *s)
{
  size_t i, n;
  for (i = 0; s[i]; i++)
    if (s[i] < 32 || wcschr(L"<>:\"/\\|?*", s[i])) s[i] = L'_';
  n = wcslen(s);
  while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'.')) s[--n] = 0;
}

static unsigned HashW(const wchar_t *s)
{
  unsigned h = 2166136261u;
  for (; *s; s++) { h ^= (unsigned)*s; h *= 16777619u; }
  return h;
}

/* HKEY_LOCAL_MACHINE\SOFTWARE\X  ->  HKLM_SOFTWARE_X */
static void KeyTag(const wchar_t *path, wchar_t *out, size_t cap)
{
  wchar_t tmp[2200], *p;
  const wchar_t *rest = path;
  size_t i;
  tmp[0] = 0;
  for (i = 5; i < 10; i++) {
    size_t l = wcslen(HIVES[i].full);
    if (_wcsnicmp(path, HIVES[i].full, l) == 0 && (path[l] == 0 || path[l] == L'\\')) {
      wcscpy(tmp, HIVES[i].abbr);
      rest = path + l;
      break;
    }
  }
  if (!tmp[0]) wcscpy(tmp, L"REG");
  if (*rest == L'\\') { wcscat(tmp, L"_"); rest++; }
  wcsncat(tmp, rest, 1900);
  for (p = tmp; *p; p++) if (*p == L'\\') *p = L'_';
  SanitizeName(tmp);
  wcsncpy(out, tmp, cap - 1);
  out[cap - 1] = 0;
}

static void MakeTimestamp(wchar_t *out, size_t cap)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf(out, cap - 1, L"%04d-%02d-%02d_%02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  out[cap - 1] = 0;
}

/* имя без расширения: [дата] - [заметка] - [тег] */
static void MakeRegName(wchar_t *name, size_t cap, int avail, const wchar_t *ts, const wchar_t *note, const wchar_t *tag)
{
  wchar_t pre[400], n2[200], t2[400];
  int room;
  pre[0] = 0;
  if (ts && ts[0]) wcsncat(pre, ts, 40);
  if (note && note[0]) {
    wcsncpy(n2, note, 80); n2[80] = 0;
    SanitizeName(n2);
    Trim(n2);
    if (n2[0]) { if (pre[0]) wcscat(pre, L" - "); wcscat(pre, n2); }
  }
  wcsncpy(t2, tag ? tag : L"", 399); t2[399] = 0;
  if (t2[0] && pre[0]) wcscat(pre, L" - ");
  if (!t2[0] && !pre[0]) wcscpy(t2, L"export");
  room = avail - (int)wcslen(pre);
  if (room < 12) room = 12;
  if ((int)wcslen(t2) > room) {
    unsigned hh = HashW(tag ? tag : L"");
    t2[room - 9] = 0;
    _snwprintf(t2 + wcslen(t2), 12, L"~%08x", hh);
  }
  _snwprintf(name, cap - 1, L"%ls%ls", pre, t2);
  name[cap - 1] = 0;
}

static void UniquePath(wchar_t *path, size_t cap)
{
  wchar_t base[MAX_PATH + 16], ext[16], *dot;
  int i;
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
  wcsncpy(base, path, MAX_PATH + 15); base[MAX_PATH + 15] = 0;
  dot = wcsrchr(base, L'.');
  if (dot) { wcsncpy(ext, dot, 15); ext[15] = 0; *dot = 0; } else ext[0] = 0;
  for (i = 2; i < 1000; i++) {
    _snwprintf(path, cap - 1, L"%ls (%d)%ls", base, i, ext);
    path[cap - 1] = 0;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
  }
}

static void MakeRegPath(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *ts, const wchar_t *note, const wchar_t *tag)
{
  wchar_t name[600];
  int avail = 250 - (int)wcslen(dir) - 1 - 4 - 6;
  if (avail < 40) avail = 40;
  MakeRegName(name, 600, avail, ts, note, tag);
  _snwprintf(out, cap - 1, L"%ls\\%ls.reg", dir, name);
  out[cap - 1] = 0;
  UniquePath(out, cap);
}

static void MakeBatName(wchar_t *name, size_t cap, int avail, BOOL newWin, const wchar_t *keyPath)
{
  wchar_t tag[400];
  KeyTag(keyPath, tag, 400);
  if (avail < 20) avail = 20;
  if ((int)wcslen(tag) > avail) {
    unsigned hh = HashW(keyPath);
    tag[avail - 9] = 0;
    _snwprintf(tag + wcslen(tag), 12, L"~%08x", hh);
  }
  _snwprintf(name, cap - 1, L"%ls-%ls.bat", newWin ? L"NW" : L"NO", tag);
  name[cap - 1] = 0;
}

/* ---- bat ---- */
static void GetRootName(wchar_t *root, size_t cap)
{
  HKEY k;
  wchar_t old[2048] = L"";
  DWORD sz = sizeof old - 4, type = 0;
  root[0] = 0;
  GetPrivateProfileStringW(L"Regedit", L"RootName", L"", root, (DWORD)cap, g_iniFile);
  if (!root[0] && RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit", 0, KEY_READ, &k) == ERROR_SUCCESS) {
    if (RegQueryValueExW(k, L"LastKey", NULL, &type, (BYTE *)old, &sz) == ERROR_SUCCESS && type == REG_SZ) {
      wchar_t *bs = wcschr(old, L'\\');
      if (bs) { *bs = 0; if (_wcsnicmp(old, L"HKEY_", 5) != 0 && wcslen(old) < cap) wcscpy(root, old); }
    }
    RegCloseKey(k);
  }
  if (!root[0]) wcscpy(root, L"Computer");
}

static wchar_t *BuildBat(const wchar_t *keyPath, BOOL newWin)
{
  WBuf b = { 0 };
  wchar_t root[128], full[4300], esc[4400], *o = esc;
  const wchar_t *s;
  BOOL nonascii = FALSE;
  GetRootName(root, 128);
  _snwprintf(full, 4299, L"%ls\\%ls", root, keyPath);
  full[4299] = 0;
  for (s = full; *s; s++) {
    if (*s > 127) nonascii = TRUE;
    if (*s == L'%') *o++ = L'%';
    *o++ = *s;
  }
  *o = 0;
  wb_adds(&b, L"@echo off\r\n");
  if (nonascii) wb_adds(&b, L"chcp 65001 >nul\r\n");
  wb_adds(&b, L"rem KeyShelf: open regedit at the registry key stored below\r\n");
  if (!newWin) {
    wb_adds(&b, L"rem Normal mode: close a running regedit first, otherwise it ignores the key\r\n");
    wb_adds(&b, L"taskkill /im regedit.exe >nul 2>&1\r\nping -n 2 127.0.0.1 >nul\r\n");
  }
  wb_adds(&b, L"reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit\" /v LastKey /t REG_SZ /d \"");
  wb_adds(&b, esc);
  wb_adds(&b, L"\" /f >nul\r\n");
  wb_adds(&b, newWin ? L"start \"\" \"%windir%\\regedit.exe\" /m\r\n" : L"start \"\" \"%windir%\\regedit.exe\"\r\n");
  return b.p;
}

static BOOL WriteUtf8File(const wchar_t *file, const wchar_t *text)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  char *u = (char *)malloc(n + 1);
  HANDLE h;
  DWORD w = 0;
  BOOL ok;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, u, n, NULL, NULL);
  h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) { free(u); return FALSE; }
  ok = WriteFile(h, u, (DWORD)(n - 1), &w, NULL);
  CloseHandle(h);
  free(u);
  return ok;
}

static BOOL WriteUtf16File(const wchar_t *file, const wchar_t *text)
{
  HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  WORD bom = 0xFEFF;
  DWORD w = 0;
  BOOL ok;
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  ok = WriteFile(h, &bom, 2, &w, NULL) && WriteFile(h, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &w, NULL);
  CloseHandle(h);
  return ok;
}

/* ---- список целей (отмеченные ветки или выбранная) ---- */
typedef struct { wchar_t path[2048]; } Target;
typedef struct { Target *t; int n, cap, skipped, fromChecks; } TargetList;

static void tl_add(TargetList *l, const wchar_t *raw)
{
  wchar_t norm[2048];
  int i;
  if (!NormalizeKey(raw, norm, 2048)) { l->skipped++; return; }
  for (i = 0; i < l->n; i++) if (_wcsicmp(l->t[i].path, norm) == 0) return;
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 16;
    l->t = (Target *)realloc(l->t, l->cap * sizeof(Target));
  }
  wcscpy(l->t[l->n++].path, norm);
}

static void CollectChecked(HTREEITEM parent, TargetList *l)
{
  HTREEITEM it;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (!nd) continue;
    if (nd->folder) CollectChecked(it, l);
    else if (TreeView_GetCheckState(g_hTree, it) == 1) tl_add(l, nd->path);
  }
}

static void CollectUnder(HTREEITEM parent, TargetList *l)
{
  HTREEITEM it;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (!nd) continue;
    if (nd->folder) CollectUnder(it, l); else tl_add(l, nd->path);
  }
}

static int CountChecked(HTREEITEM parent)
{
  HTREEITEM it;
  int n = 0;
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (!nd) continue;
    if (nd->folder) n += CountChecked(it);
    else if (TreeView_GetCheckState(g_hTree, it) == 1) n++;
  }
  return n;
}

static void CollectTargets(TargetList *l)
{
  NodeData *nd;
  CommitEdits();
  CollectChecked(TVI_ROOT, l);
  if (l->n > 0 || CountChecked(TVI_ROOT) > 0) { l->fromChecks = 1; return; }
  nd = GetNode(g_cur);
  if (!nd) return;
  if (nd->folder) CollectUnder(g_cur, l); else tl_add(l, nd->path);
}

static void SetCheckRec(HTREEITEM it, BOOL state)
{
  HTREEITEM c;
  TreeView_SetCheckState(g_hTree, it, state);
  for (c = TreeView_GetChild(g_hTree, it); c; c = TreeView_GetNextSibling(g_hTree, c)) SetCheckRec(c, state);
}

static void SetCheckAll(BOOL state)
{
  HTREEITEM it;
  for (it = TreeView_GetRoot(g_hTree); it; it = TreeView_GetNextSibling(g_hTree, it)) SetCheckRec(it, state);
}

/* ---- индикатор занятости и фоновый запуск ---- */
static void SetBusy(const wchar_t *fmt, ...)
{
  va_list ap;
  EnterCriticalSection(&g_cs);
  va_start(ap, fmt);
  _vsnwprintf(g_busyText, 499, fmt, ap);
  va_end(ap);
  g_busyText[499] = 0;
  LeaveCriticalSection(&g_cs);
}

static LRESULT CALLBACK BusyProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  switch (m) {
    case WM_COMMAND:
      if (LOWORD(wp) == IDCANCEL) {
        InterlockedExchange(&g_cancel, 1);
        EnterCriticalSection(&g_cs);
        if (g_curProc) TerminateProcess(g_curProc, 1);
        LeaveCriticalSection(&g_cs);
      }
      return 0;
    case WM_CLOSE: return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static void RunBusy(LPTHREAD_START_ROUTINE fn, void *param, const wchar_t *title)
{
  RECT orc;
  int w = 440, hh = 132, x, y;
  HWND hb, st, pb;
  HANDLE th;
  wchar_t last[500] = L"";
  MSG msg;
  InterlockedExchange(&g_cancel, 0);
  g_busyText[0] = 0;
  g_curProc = NULL;
  GetWindowRect(g_hMain, &orc);
  x = orc.left + ((orc.right - orc.left) - w) / 2;
  y = orc.top + ((orc.bottom - orc.top) - hh) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  EnableWindow(g_hMain, FALSE);
  hb = CreateWindowExW(WS_EX_DLGMODALFRAME, CLS_BUSY, title, WS_POPUP | WS_CAPTION, x, y, w, hh, g_hMain, NULL, g_hInst, NULL);
  st = Mk(hb, L"STATIC", L"Подготовка...", SS_ENDELLIPSIS, 12, 12, 404, 18, 100, 0);
  pb = Mk(hb, PROGRESS_CLASSW, L"", PBS_MARQUEE, 12, 40, 404, 16, 101, 0);
  SendMessageW(pb, PBM_SETMARQUEE, TRUE, 40);
  Mk(hb, L"BUTTON", L"Отмена", BS_PUSHBUTTON | WS_TABSTOP, 170, 70, 90, 26, IDCANCEL, 0);
  ShowWindow(hb, SW_SHOW);
  th = CreateThread(NULL, 0, fn, param, 0, NULL);
  for (;;) {
    DWORD r = MsgWaitForMultipleObjects(1, &th, FALSE, 100, QS_ALLINPUT);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
      if (!IsDialogMessageW(hb, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnterCriticalSection(&g_cs);
    if (wcscmp(last, g_busyText) != 0) { wcscpy(last, g_busyText); SetWindowTextW(st, last); }
    LeaveCriticalSection(&g_cs);
    if (r == WAIT_OBJECT_0) break;
  }
  CloseHandle(th);
  EnableWindow(g_hMain, TRUE);
  DestroyWindow(hb);
  SetForegroundWindow(g_hMain);
}

/* ---- reg export ---- */
static BOOL Is64OS(void)
{
#ifdef _WIN64
  return TRUE;
#else
  BOOL w = FALSE;
  typedef BOOL (WINAPI *FN)(HANDLE, PBOOL);
  FN f = (FN)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
  if (f) f(GetCurrentProcess(), &w);
  return w;
#endif
}

/* возвращает код выхода reg.exe (0 - успех) или -1, если запустить не удалось */
static int RunRegExport(const wchar_t *key, const wchar_t *outFile, wchar_t *msg, size_t mcap)
{
  wchar_t sysdir[MAX_PATH], cmd[5000], wb[2100];
  SECURITY_ATTRIBUTES sa;
  HANDLE rd = NULL, wr = NULL, nul;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD code = 1, got;
  char buf[512], ob[2048];
  size_t tot = 0, i;
  int n;
  msg[0] = 0;
  GetSystemDirectoryW(sysdir, MAX_PATH);
  _snwprintf(cmd, 4999, L"\"%ls\\reg.exe\" export \"%ls\" \"%ls\" /y%ls", sysdir, key, outFile, Is64OS() ? L" /reg:64" : L"");
  cmd[4999] = 0;
  memset(&sa, 0, sizeof sa);
  sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
  if (!CreatePipe(&rd, &wr, &sa, 0)) { _snwprintf(msg, mcap - 1, L"не удалось создать канал"); return -1; }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
  if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    _snwprintf(msg, mcap - 1, L"не удалось запустить reg.exe (ошибка %lu)", GetLastError());
    CloseHandle(rd); CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return -1;
  }
  CloseHandle(wr);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  EnterCriticalSection(&g_cs); g_curProc = pi.hProcess; LeaveCriticalSection(&g_cs);
  while (ReadFile(rd, buf, sizeof buf, &got, NULL) && got) {
    if (tot + got < sizeof ob - 1) { memcpy(ob + tot, buf, got); tot += got; }
  }
  ob[tot] = 0;
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &code);
  EnterCriticalSection(&g_cs); g_curProc = NULL; LeaveCriticalSection(&g_cs);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
  n = MultiByteToWideChar(CP_OEMCP, 0, ob, (int)tot, wb, 2099);
  wb[n > 0 ? n : 0] = 0;
  for (i = 0; wb[i]; i++) if (wb[i] == L'\r' || wb[i] == L'\n') wb[i] = L' ';
  Trim(wb);
  wcsncpy(msg, wb, mcap - 1); msg[mcap - 1] = 0;
  if (code != 0) Log(L"reg export код %lu: %ls | %ls", code, key, wb);
  return (int)code;
}

/* добавляет в буфер тело экспортированного .reg без заголовка */
static void AppendRegBody(WBuf *b, const wchar_t *file)
{
  DWORD len;
  BYTE *raw = ReadAllLimit(file, &len, 400u * 1024u * 1024u);
  wchar_t *w, *p;
  if (!raw) return;
  if (len >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) w = (wchar_t *)(raw + 2);
  else { free(raw); return; }
  p = wcschr(w, L'\n');
  p = p ? p + 1 : w + wcslen(w);
  while (*p == L'\r' || *p == L'\n') p++;
  wb_adds(b, p);
  wb_adds(b, L"\r\n");
  free(raw);
}

typedef struct {
  TargetList *tl;
  wchar_t dir[MAX_PATH], ts[32], note[200];
  BOOL useTag, combine;
  int ok, fail, canceled;
  wchar_t firstErr[500], firstFile[MAX_PATH + 16];
} SaveJob;

static DWORD WINAPI SaveWorker(LPVOID p)
{
  SaveJob *j = (SaveJob *)p;
  int i;
  wchar_t file[MAX_PATH + 16], msg[400], tag[400], tmp[MAX_PATH + 40], tdir[MAX_PATH];
  WBuf all = { 0 };
  GetTempPathW(MAX_PATH, tdir);
  for (i = 0; i < j->tl->n; i++) {
    int rc;
    const wchar_t *kp = j->tl->t[i].path;
    if (g_cancel) { j->canceled = 1; break; }
    SetBusy(L"Экспорт %d из %d: %ls", i + 1, j->tl->n, kp);
    if (j->combine) {
      _snwprintf(tmp, MAX_PATH + 39, L"%lskeyshelf_%lu_%d.reg", tdir, GetTickCount(), i);
      rc = RunRegExport(kp, tmp, msg, 400);
      if (rc == 0) { AppendRegBody(&all, tmp); j->ok++; }
      else { j->fail++; if (!j->firstErr[0]) _snwprintf(j->firstErr, 499, L"%ls\n%ls", kp, msg); }
      DeleteFileW(tmp);
    } else {
      if (j->useTag) KeyTag(kp, tag, 400); else tag[0] = 0;
      MakeRegPath(file, MAX_PATH + 16, j->dir, j->ts, j->note, tag);
      rc = RunRegExport(kp, file, msg, 400);
      if (rc == 0) { j->ok++; if (!j->firstFile[0]) wcscpy(j->firstFile, file); }
      else { j->fail++; DeleteFileW(file); if (!j->firstErr[0]) _snwprintf(j->firstErr, 499, L"%ls\n%ls", kp, msg); }
    }
  }
  if (j->combine && j->ok > 0 && !j->canceled) {
    wchar_t fixed[100], *text;
    size_t hl = wcslen(L"Windows Registry Editor Version 5.00\r\n\r\n");
    _snwprintf(fixed, 99, L"группа (%d)", j->ok);
    MakeRegPath(file, MAX_PATH + 16, j->dir, j->ts, j->note, j->useTag ? fixed : L"");
    text = (wchar_t *)malloc((hl + (all.p ? wcslen(all.p) : 0) + 4) * sizeof(wchar_t));
    wcscpy(text, L"Windows Registry Editor Version 5.00\r\n\r\n");
    if (all.p) wcscat(text, all.p);
    if (WriteUtf16File(file, text)) wcscpy(j->firstFile, file);
    else { j->fail += j->ok; j->ok = 0; _snwprintf(j->firstErr, 499, L"Не удалось записать файл\n%ls", file); }
    free(text);
  }
  free(all.p);
  SetBusy(L"Готово");
  return 0;
}

/* ---- обзор папок ---- */
static BOOL BrowseFolder(HWND owner, const wchar_t *title, wchar_t *out)
{
  BROWSEINFOW bi;
  PIDLIST_ABSOLUTE pidl;
  memset(&bi, 0, sizeof bi);
  bi.hwndOwner = owner; bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return FALSE;
  if (!SHGetPathFromIDListW(pidl, out)) { CoTaskMemFree(pidl); return FALSE; }
  CoTaskMemFree(pidl);
  return TRUE;
}

/* ---- диалог сохранения / экспорта ---- */
typedef struct {
  int kind, n;               /* kind: 0 - .reg, 1 - .bat */
  const wchar_t *summary, *firstKey;
  HWND hwnd, eNote, cDate, cTag, rSep, rOne, rArch, rOther, combo, eSub, eDir, bBrowse, ePrev, rNO, rNW, rBoth, lA1, lA2;
  BOOL done, ok;
  wchar_t *dirs[400];
  int ndirs, defSel;
  wchar_t selDir[MAX_PATH];  /* папка архива, выбранная по умолчанию */
  wchar_t note[200];
  BOOL useDate, useTag, combine, toArchive;
  wchar_t destDir[MAX_PATH];
  int batMode;               /* 0 NO, 1 NW, 2 оба */
} ExpDlg;

static void AddDirsCombo(ExpDlg *d, const wchar_t *dir, int depth)
{
  WIN32_FIND_DATAW fd;
  HANDLE h;
  wchar_t pat[MAX_PATH + 4], full[MAX_PATH + 4], lbl[400];
  if (depth > 10) return;
  _snwprintf(pat, MAX_PATH + 3, L"%ls\\*", dir);
  h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    int idx, i, pos = 0;
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    if (d->ndirs >= 399) break;
    _snwprintf(full, MAX_PATH + 3, L"%ls\\%ls", dir, fd.cFileName);
    for (i = 0; i < (depth + 1) * 3 && pos < 40; i++) lbl[pos++] = L' ';
    lbl[pos] = 0;
    wcsncat(lbl, fd.cFileName, 300);
    idx = (int)SendMessageW(d->combo, CB_ADDSTRING, 0, (LPARAM)lbl);
    d->dirs[d->ndirs] = Dup(full);
    SendMessageW(d->combo, CB_SETITEMDATA, idx, (LPARAM)d->ndirs);
    if (d->selDir[0] && _wcsicmp(d->selDir, full) == 0) d->defSel = idx;
    d->ndirs++;
    AddDirsCombo(d, full, depth + 1);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static BOOL ExpChecked(HWND h) { return h && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

static void ExpUpdate(ExpDlg *d)
{
  wchar_t note[200] = L"", tag[400], ts[32], name[700], disp[1500];
  if (!d->ePrev) return;
  if (d->kind == 0) {
    BOOL arch = ExpChecked(d->rArch), comb = d->n > 1 && ExpChecked(d->rOne);
    GetWindowTextW(d->eNote, note, 200);
    if (ExpChecked(d->cDate)) MakeTimestamp(ts, 32); else ts[0] = 0;
    if (ExpChecked(d->cTag)) {
      if (comb) _snwprintf(tag, 399, L"группа (%d)", d->n); else KeyTag(d->firstKey, tag, 400);
    } else tag[0] = 0;
    MakeRegName(name, 700, 150, ts, note, tag);
    _snwprintf(disp, 1499, L"%ls.reg", name);
    EnableWindow(d->combo, arch); EnableWindow(d->eSub, arch);
    EnableWindow(d->eDir, !arch); EnableWindow(d->bBrowse, !arch);
  } else {
    wchar_t n1[400], n2[400];
    MakeBatName(n1, 400, 150, FALSE, d->firstKey);
    MakeBatName(n2, 400, 150, TRUE, d->firstKey);
    if (ExpChecked(d->rNO)) wcscpy(disp, n1);
    else if (ExpChecked(d->rNW)) wcscpy(disp, n2);
    else _snwprintf(disp, 1499, L"%ls\r\n%ls", n1, n2);
  }
  SetWindowTextW(d->ePrev, disp);
}

static LRESULT CALLBACK ExpProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  ExpDlg *d = (ExpDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
      RECT rc;
      int y = 8, W = 564, bh;
      d = (ExpDlg *)cs->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
      d->hwnd = h;
      GetClientRect(h, &rc);
      bh = rc.bottom;
      Mk(h, L"STATIC", d->summary, SS_ENDELLIPSIS, 8, y, W, 34, 90, 0); y += 40;
      if (d->kind == 0) {
        Mk(h, L"STATIC", L"Заметка (будет добавлена в имя файла):", 0, 8, y, W, 16, 91, 0); y += 18;
        d->eNote = Mk(h, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 8, y, W, 22, IDE_NOTE, WS_EX_CLIENTEDGE); y += 28;
        SendMessageW(d->eNote, EM_LIMITTEXT, 80, 0);
        d->cDate = Mk(h, L"BUTTON", L"Добавить дату и время в имя файла", WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, W, 20, IDE_DATE, 0); y += 22;
        d->cTag = Mk(h, L"BUTTON", L"Добавить путь ветки в имя файла", WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, W, 20, IDE_TAG, 0); y += 24;
        SendMessageW(d->cDate, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(d->cTag, BM_SETCHECK, BST_CHECKED, 0);
        if (d->n > 1) {
          d->rSep = Mk(h, L"BUTTON", L"Отдельный .reg для каждой ветки", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_SEP, 0); y += 22;
          d->rOne = Mk(h, L"BUTTON", L"Один общий .reg со всеми ветками", BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_ONE, 0); y += 26;
          SendMessageW(d->rSep, BM_SETCHECK, BST_CHECKED, 0);
        }
        Mk(h, L"STATIC", L"Имя файла (пример):", 0, 8, y, W, 16, 92, 0); y += 18;
        d->ePrev = Mk(h, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, 8, y, W, 22, IDE_PREVIEW, WS_EX_CLIENTEDGE); y += 32;
        d->rArch = Mk(h, L"BUTTON", L"Сохранить в архив KeyShelf (вкладка «Сохранённые»)", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_ARCH, 0); y += 22;
        SendMessageW(d->rArch, BM_SETCHECK, BST_CHECKED, 0);
        d->lA1 = Mk(h, L"STATIC", L"Папка в архиве:", 0, 28, y, W - 20, 16, 93, 0); y += 18;
        d->combo = Mk(h, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 28, y, W - 20, 200, IDE_COMBO, 0); y += 28;
        d->lA2 = Mk(h, L"STATIC", L"Новая подпапка (создать, необязательно):", 0, 28, y, W - 20, 16, 94, 0); y += 18;
        d->eSub = Mk(h, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 28, y, W - 20, 22, IDE_SUB, WS_EX_CLIENTEDGE); y += 32;
        d->rOther = Mk(h, L"BUTTON", L"Сохранить в другую папку (обычный экспорт .reg):", WS_TABSTOP | BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_OTHER, 0); y += 22;
        d->eDir = Mk(h, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 28, y, W - 20 - 90, 22, IDE_DIR, WS_EX_CLIENTEDGE);
        d->bBrowse = Mk(h, L"BUTTON", L"Обзор...", WS_TABSTOP, 28 + W - 20 - 84, y - 1, 84, 24, IDE_BROWSE, 0);
        {
          wchar_t sd[MAX_PATH];
          GetPrivateProfileStringW(L"Paths", L"SaveDir", L"", sd, MAX_PATH, g_iniFile);
          SetWindowTextW(d->eDir, sd);
        }
        SendMessageW(d->combo, CB_ADDSTRING, 0, (LPARAM)L"(корень архива)");
        d->dirs[0] = Dup(g_savedDir);
        SendMessageW(d->combo, CB_SETITEMDATA, 0, 0);
        d->ndirs = 1;
        d->defSel = 0;
        AddDirsCombo(d, g_savedDir, 0);
        SendMessageW(d->combo, CB_SETCURSEL, d->defSel, 0);
      } else {
        d->rNO = Mk(h, L"BUTTON", L"NO — открыть regedit обычным способом", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_NO, 0); y += 22;
        d->rNW = Mk(h, L"BUTTON", L"NW — открыть regedit в новом окне (/m)", BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_NW, 0); y += 22;
        d->rBoth = Mk(h, L"BUTTON", L"Оба варианта (два файла на каждую ветку)", BS_AUTORADIOBUTTON, 8, y, W, 20, IDE_BOTH, 0); y += 28;
        SendMessageW(d->rNW, BM_SETCHECK, BST_CHECKED, 0);
        Mk(h, L"STATIC", L"Имя файла (пример):", 0, 8, y, W, 16, 92, 0); y += 18;
        d->ePrev = Mk(h, L"EDIT", L"", ES_MULTILINE | ES_READONLY, 8, y, W, 40, IDE_PREVIEW, WS_EX_CLIENTEDGE); y += 50;
        Mk(h, L"STATIC", L"Папка для скриптов:", 0, 8, y, W, 16, 93, 0); y += 18;
        d->eDir = Mk(h, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 8, y, W - 90, 22, IDE_DIR, WS_EX_CLIENTEDGE);
        d->bBrowse = Mk(h, L"BUTTON", L"Обзор...", WS_TABSTOP, 8 + W - 84, y - 1, 84, 24, IDE_BROWSE, 0);
        SetWindowTextW(d->eDir, g_batDir);
      }
      Mk(h, L"BUTTON", d->kind == 0 ? L"Сохранить" : L"Создать", WS_TABSTOP | BS_DEFPUSHBUTTON, 8 + W - 188, bh - 38, 90, 28, IDOK, 0);
      Mk(h, L"BUTTON", L"Отмена", WS_TABSTOP, 8 + W - 90, bh - 38, 90, 28, IDCANCEL, 0);
      ExpUpdate(d);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if (!d) break;
      if (id == IDE_NOTE && code == EN_CHANGE) { ExpUpdate(d); return 0; }
      if ((id == IDE_DATE || id == IDE_TAG || id == IDE_SEP || id == IDE_ONE || id == IDE_ARCH || id == IDE_OTHER ||
           id == IDE_NO || id == IDE_NW || id == IDE_BOTH) && code == BN_CLICKED) { ExpUpdate(d); return 0; }
      if (id == IDE_BROWSE) {
        wchar_t p[MAX_PATH];
        if (BrowseFolder(h, L"Выберите папку", p)) { SetWindowTextW(d->eDir, p); if (d->kind == 0) { SendMessageW(d->rOther, BM_SETCHECK, BST_CHECKED, 0); SendMessageW(d->rArch, BM_SETCHECK, BST_UNCHECKED, 0); ExpUpdate(d); } }
        return 0;
      }
      if (id == IDOK) {
        if (d->kind == 0) {
          GetWindowTextW(d->eNote, d->note, 200);
          d->useDate = ExpChecked(d->cDate);
          d->useTag = ExpChecked(d->cTag);
          d->combine = d->n > 1 && ExpChecked(d->rOne);
          d->toArchive = ExpChecked(d->rArch);
          if (d->toArchive) {
            int sel = (int)SendMessageW(d->combo, CB_GETCURSEL, 0, 0);
            int di = sel >= 0 ? (int)SendMessageW(d->combo, CB_GETITEMDATA, sel, 0) : 0;
            wchar_t sub[120];
            wcsncpy(d->destDir, (di >= 0 && di < d->ndirs) ? d->dirs[di] : g_savedDir, MAX_PATH - 1);
            d->destDir[MAX_PATH - 1] = 0;
            GetWindowTextW(d->eSub, sub, 120);
            SanitizeName(sub);
            Trim(sub);
            if (sub[0]) { wcsncat(d->destDir, L"\\", MAX_PATH - wcslen(d->destDir) - 1); wcsncat(d->destDir, sub, MAX_PATH - wcslen(d->destDir) - 1); }
          } else {
            GetWindowTextW(d->eDir, d->destDir, MAX_PATH);
            Trim(d->destDir);
            if (!d->destDir[0]) { MessageBoxW(h, L"Укажите папку.", APP_NAME, MB_ICONINFORMATION); return 0; }
          }
        } else {
          d->batMode = ExpChecked(d->rNO) ? 0 : ExpChecked(d->rNW) ? 1 : 2;
          GetWindowTextW(d->eDir, d->destDir, MAX_PATH);
          Trim(d->destDir);
          if (!d->destDir[0]) { MessageBoxW(h, L"Укажите папку.", APP_NAME, MB_ICONINFORMATION); return 0; }
        }
        d->ok = TRUE; d->done = TRUE;
        return 0;
      }
      if (id == IDCANCEL) { d->done = TRUE; return 0; }
      break;
    }
    case WM_CLOSE: if (d) d->done = TRUE; return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static BOOL RunExpDlg(ExpDlg *d)
{
  RECT r, orc;
  MSG msg;
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  int w, hgt, x, y, ch, i;
  ch = d->kind == 0 ? (d->n > 1 ? 506 : 456) : 330;
  r.left = 0; r.top = 0; r.right = 580; r.bottom = ch;
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  w = r.right - r.left; hgt = r.bottom - r.top;
  GetWindowRect(g_hMain, &orc);
  x = orc.left + ((orc.right - orc.left) - w) / 2;
  y = orc.top + ((orc.bottom - orc.top) - hgt) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  EnableWindow(g_hMain, FALSE);
  d->hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLS_EXP, d->kind == 0 ? L"Сохранить ветки в .reg" : L"Экспорт в .bat", style, x, y, w, hgt, g_hMain, NULL, g_hInst, d);
  if (!d->hwnd) { EnableWindow(g_hMain, TRUE); return FALSE; }
  ShowWindow(d->hwnd, SW_SHOW);
  SetForegroundWindow(d->hwnd);
  while (!d->done) {
    int rc = GetMessageW(&msg, NULL, 0, 0);
    if (rc <= 0) { if (rc == 0) PostQuitMessage((int)msg.wParam); break; }
    if (!IsDialogMessageW(d->hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  EnableWindow(g_hMain, TRUE);
  DestroyWindow(d->hwnd);
  SetForegroundWindow(g_hMain);
  for (i = 0; i < d->ndirs; i++) free(d->dirs[i]);
  return d->ok;
}

/* ---- архив (вкладка «Сохранённые») ---- */
static HTREEITEM InsertSaved(HTREEITEM parent, const wchar_t *name, BOOL folder, const wchar_t *full)
{
  TVINSERTSTRUCTW is;
  NodeData *nd = (NodeData *)calloc(1, sizeof *nd);
  wchar_t label[300];
  size_t l;
  nd->folder = folder; nd->path = Dup(full); nd->comment = Dup(L"");
  wcsncpy(label, name, 299); label[299] = 0;
  l = wcslen(label);
  if (!folder && l > 4 && _wcsicmp(label + l - 4, L".reg") == 0) label[l - 4] = 0;
  memset(&is, 0, sizeof is);
  is.hParent = parent ? parent : TVI_ROOT;
  is.hInsertAfter = TVI_LAST;
  is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
  is.item.pszText = label;
  is.item.lParam = (LPARAM)nd;
  is.item.iImage = is.item.iSelectedImage = folder ? g_imgFolder : g_imgReg;
  return (HTREEITEM)SendMessageW(g_hSaved, TVM_INSERTITEMW, 0, (LPARAM)&is);
}

static void SavedLoadDir(HTREEITEM parent, const wchar_t *dir, int depth)
{
  int pass;
  WIN32_FIND_DATAW fd;
  HANDLE h;
  wchar_t pat[MAX_PATH + 4], full[MAX_PATH + 4];
  if (depth > 12) return;
  for (pass = 0; pass < 2; pass++) {
    _snwprintf(pat, MAX_PATH + 3, L"%ls\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
      BOOL isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
      _snwprintf(full, MAX_PATH + 3, L"%ls\\%ls", dir, fd.cFileName);
      if (pass == 0 && isdir && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        HTREEITEM it = InsertSaved(parent, fd.cFileName, TRUE, full);
        SavedLoadDir(it, full, depth + 1);
      } else if (pass == 1 && !isdir) {
        size_t l = wcslen(fd.cFileName);
        if (l > 4 && _wcsicmp(fd.cFileName + l - 4, L".reg") == 0) InsertSaved(parent, fd.cFileName, FALSE, full);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
}

static void ExpandAllIn(HWND tv, HTREEITEM parent)
{
  HTREEITEM it;
  for (it = FirstChildIn(tv, parent); it; it = NextSibIn(tv, it)) {
    NodeData *nd = GetNodeIn(tv, it);
    if (nd && nd->folder) { ExpandAllIn(tv, it); SendMessageW(tv, TVM_EXPAND, TVE_EXPAND, (LPARAM)it); }
  }
}

static HTREEITEM SavedFind(HTREEITEM parent, const wchar_t *path)
{
  HTREEITEM it, r;
  for (it = FirstChildIn(g_hSaved, parent); it; it = NextSibIn(g_hSaved, it)) {
    NodeData *nd = GetNodeIn(g_hSaved, it);
    if (!nd) continue;
    if (_wcsicmp(nd->path, path) == 0) return it;
    if (nd->folder && (r = SavedFind(it, path)) != NULL) return r;
  }
  return NULL;
}

static int SavedCountFiles(HTREEITEM parent)
{
  HTREEITEM it;
  int n = 0;
  for (it = FirstChildIn(g_hSaved, parent); it; it = NextSibIn(g_hSaved, it)) {
    NodeData *nd = GetNodeIn(g_hSaved, it);
    if (!nd) continue;
    if (nd->folder) n += SavedCountFiles(it); else n++;
  }
  return n;
}

static void UpdateCount(void)
{
  wchar_t b[200];
  if (g_tab == 0) _snwprintf(b, 199, L"Отмечено веток: %d  (групповые действия)", CountChecked(TVI_ROOT));
  else _snwprintf(b, 199, L"Файлов в архиве: %d", SavedCountFiles(TVI_ROOT));
  b[199] = 0;
  SetWindowTextW(g_hCount, b);
}

static void ShowSaved(HTREEITEM it);

static void SavedRefresh(const wchar_t *selPath)
{
  wchar_t keep[MAX_PATH + 8] = L"";
  NodeData *cur = GetNodeIn(g_hSaved, g_curS);
  HTREEITEM sel = NULL;
  if (cur) wcsncpy(keep, cur->path, MAX_PATH);
  g_curS = NULL;
  SendMessageW(g_hSaved, WM_SETREDRAW, FALSE, 0);
  TreeView_DeleteAllItems(g_hSaved);
  CreateDirectoryW(g_savedDir, NULL);
  SavedLoadDir(TVI_ROOT, g_savedDir, 0);
  ExpandAllIn(g_hSaved, TVI_ROOT);
  SendMessageW(g_hSaved, WM_SETREDRAW, TRUE, 0);
  if (selPath && selPath[0]) sel = SavedFind(TVI_ROOT, selPath);
  if (!sel && keep[0]) sel = SavedFind(TVI_ROOT, keep);
  if (!sel) sel = FirstChildIn(g_hSaved, TVI_ROOT);
  if (sel) { SendMessageW(g_hSaved, TVM_SELECTITEM, TVGN_CARET, (LPARAM)sel); SendMessageW(g_hSaved, TVM_ENSUREVISIBLE, 0, (LPARAM)sel); }
  else if (g_tab == 1) ShowSaved(NULL);
  UpdateCount();
}

static BOOL SavedFirstKey(const wchar_t *file, wchar_t *out, size_t cap)
{
  KeyList kl;
  BOOL ok;
  memset(&kl, 0, sizeof kl);
  ParseRegFileLimit(file, &kl, 1u << 20);
  ok = kl.n > 0;
  if (ok) { wcsncpy(out, kl.keys[0], cap - 1); out[cap - 1] = 0; }
  kl_free(&kl);
  return ok;
}

static void ShowSaved(HTREEITEM it)
{
  NodeData *nd = GetNodeIn(g_hSaved, it);
  wchar_t name[300];
  g_loading = TRUE;
  if (!nd) {
    SetWindowTextW(g_hName, L""); SetWindowTextW(g_hPath, L"");
    SetWindowTextW(g_hComment, L"Архив пока пуст или ничего не выбрано.\r\n\r\nСохранить ветку: вкладка «Закладки» → кнопка «Сохранить ветку…» (Ctrl+S).");
    EnableWindow(g_hBtnJump, FALSE); EnableWindow(g_hBtnJumpNew, FALSE);
    EnableWindow(g_hBtnX[0], FALSE); EnableWindow(g_hBtnX[1], FALSE); EnableWindow(g_hBtnX[2], FALSE);
  } else if (nd->folder) {
    wchar_t info[MAX_PATH + 100];
    GetItemTextIn(g_hSaved, it, name, 300);
    SetWindowTextW(g_hName, name); SetWindowTextW(g_hPath, L"");
    _snwprintf(info, MAX_PATH + 99, L"Папка архива:\r\n%ls", nd->path);
    SetWindowTextW(g_hComment, info);
    EnableWindow(g_hBtnJump, FALSE); EnableWindow(g_hBtnJumpNew, FALSE);
    EnableWindow(g_hBtnX[0], FALSE); EnableWindow(g_hBtnX[1], TRUE); EnableWindow(g_hBtnX[2], FALSE);
  } else {
    KeyList kl;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    WBuf b = { 0 };
    wchar_t tmp[400];
    int i;
    memset(&kl, 0, sizeof kl);
    ParseRegFileLimit(nd->path, &kl, 1u << 20);
    GetItemTextIn(g_hSaved, it, name, 300);
    SetWindowTextW(g_hName, name);
    SetWindowTextW(g_hPath, kl.n ? kl.keys[0] : L"");
    wb_adds(&b, L"Файл:\r\n"); wb_adds(&b, nd->path); wb_adds(&b, L"\r\n\r\n");
    if (GetFileAttributesExW(nd->path, GetFileExInfoStandard, &fa)) {
      SYSTEMTIME st; FILETIME lf;
      unsigned long long sz = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
      FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lf);
      FileTimeToSystemTime(&lf, &st);
      _snwprintf(tmp, 399, L"Размер: %llu КБ\r\nИзменён: %02d.%02d.%04d %02d:%02d:%02d\r\n\r\n", (sz + 1023) / 1024, st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
      wb_adds(&b, tmp);
    }
    _snwprintf(tmp, 399, L"Веток в начале файла: %d%ls\r\n", kl.n, kl.n >= 50000 ? L"+" : L"");
    wb_adds(&b, tmp);
    for (i = 0; i < kl.n && i < 40; i++) { wb_adds(&b, kl.keys[i]); wb_adds(&b, L"\r\n"); }
    if (kl.n > 40) wb_adds(&b, L"...\r\n");
    SetWindowTextW(g_hComment, b.p ? b.p : L"");
    free(b.p);
    EnableWindow(g_hBtnJump, kl.n > 0); EnableWindow(g_hBtnJumpNew, kl.n > 0);
    EnableWindow(g_hBtnX[0], TRUE); EnableWindow(g_hBtnX[1], TRUE); EnableWindow(g_hBtnX[2], TRUE);
    kl_free(&kl);
  }
  g_loading = FALSE;
}

static void SwitchTab(int t)
{
  if (g_tab == 0 && t != 0) CommitEdits();
  g_tab = t;
  TabCtrl_SetCurSel(g_hTab, t);
  ShowWindow(g_hTree, t == 0 ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hSaved, t == 1 ? SW_SHOW : SW_HIDE);
  SetWindowTextW(g_hLbl[0], t == 0 ? L"Название:" : L"Файл:");
  SetWindowTextW(g_hLbl[1], t == 0 ? L"Путь в реестре:" : L"Ветка (первая в файле):");
  SetWindowTextW(g_hLbl[2], t == 0 ? L"Комментарий:" : L"Сведения:");
  SendMessageW(g_hName, EM_SETREADONLY, t == 1, 0);
  SendMessageW(g_hPath, EM_SETREADONLY, t == 1, 0);
  SendMessageW(g_hComment, EM_SETREADONLY, t == 1, 0);
  SetWindowTextW(g_hBtnX[0], t == 0 ? L"Сохранить ветку…" : L"Открыть в Блокноте");
  SetWindowTextW(g_hBtnX[1], t == 0 ? L"Экспорт в .bat…" : L"Показать в папке");
  SetWindowTextW(g_hBtnX[2], t == 0 ? L"Открыть regedit" : L"Восстановить в реестр…");
  if (t == 0) ShowNode(g_cur); else ShowSaved(g_curS);
  UpdateCount();
}

static void SavedParentDir(const wchar_t *path, wchar_t *out)
{
  wchar_t *sl;
  wcsncpy(out, path, MAX_PATH - 1); out[MAX_PATH - 1] = 0;
  sl = wcsrchr(out, L'\\');
  if (sl) *sl = 0;
}

static void SavedNewFolder(int mode)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  wchar_t base[MAX_PATH], np[MAX_PATH + 24];
  int i;
  HTREEITEM it;
  if (mode == 2 || !nd) wcscpy(base, g_savedDir);
  else if (mode == 1 && nd->folder) wcscpy(base, nd->path);
  else SavedParentDir(nd->path, base);
  _snwprintf(np, MAX_PATH + 23, L"%ls\\Новая папка", base);
  for (i = 2; GetFileAttributesW(np) != INVALID_FILE_ATTRIBUTES && i < 500; i++) _snwprintf(np, MAX_PATH + 23, L"%ls\\Новая папка (%d)", base, i);
  if (!CreateDirectoryW(np, NULL)) { MessageBoxW(g_hMain, L"Не удалось создать папку.", APP_NAME, MB_ICONERROR); return; }
  SavedRefresh(np);
  it = SavedFind(TVI_ROOT, np);
  if (it) { SetFocus(g_hSaved); TreeView_EditLabel(g_hSaved, it); }
}

static void SavedDelete(void)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  wchar_t from[MAX_PATH + 4];
  SHFILEOPSTRUCTW op;
  if (!nd) return;
  memset(from, 0, sizeof from);
  wcsncpy(from, nd->path, MAX_PATH);
  memset(&op, 0, sizeof op);
  op.hwnd = g_hMain; op.wFunc = FO_DELETE; op.pFrom = from; op.fFlags = FOF_ALLOWUNDO;
  if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) SavedRefresh(NULL);
}

static void SavedRename(NMTVDISPINFOW *di)
{
  NodeData *nd = GetNodeIn(g_hSaved, di->item.hItem);
  wchar_t nn[300], dir[MAX_PATH], np[MAX_PATH + 8];
  if (!nd || !di->item.pszText) return;
  wcsncpy(nn, di->item.pszText, 299); nn[299] = 0;
  Trim(nn);
  SanitizeName(nn);
  if (!nn[0]) return;
  SavedParentDir(nd->path, dir);
  _snwprintf(np, MAX_PATH + 7, L"%ls\\%ls%ls", dir, nn, nd->folder ? L"" : L".reg");
  if (_wcsicmp(np, nd->path) == 0) return;
  if (!MoveFileW(nd->path, np)) { MessageBoxW(g_hMain, L"Не удалось переименовать (возможно, такое имя уже есть).", APP_NAME, MB_ICONWARNING); return; }
  wcsncpy(g_pendingSelect, np, MAX_PATH); g_pendingSelect[MAX_PATH] = 0;
  PostMessageW(g_hMain, WM_APP_SAVEDREFRESH, 0, 0);
}

static void SavedJump(BOOL forceNew)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  wchar_t key[2048];
  if (!nd || nd->folder) return;
  if (!SavedFirstKey(nd->path, key, 2048)) { MessageBoxW(g_hMain, L"В файле не найдено ветки реестра.", APP_NAME, MB_ICONINFORMATION); return; }
  DoJump(key, forceNew ? TRUE : g_newWin);
}

static void SavedNotepad(void)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  wchar_t arg[MAX_PATH + 8];
  if (!nd || nd->folder) return;
  _snwprintf(arg, MAX_PATH + 7, L"\"%ls\"", nd->path);
  ShellExecuteW(g_hMain, L"open", L"notepad.exe", arg, NULL, SW_SHOWNORMAL);
}

static void SavedShow(void)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  wchar_t arg[MAX_PATH + 24];
  if (!nd) return;
  if (nd->folder) _snwprintf(arg, MAX_PATH + 23, L"\"%ls\"", nd->path);
  else _snwprintf(arg, MAX_PATH + 23, L"/select,\"%ls\"", nd->path);
  ShellExecuteW(g_hMain, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
}

static void SavedRestore(void)
{
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  if (!nd || nd->folder) return;
  if (MessageBoxW(g_hMain, L"Файл будет внесён в реестр: значения в указанных ветках изменятся или будут созданы.\n\nПродолжить? "
                  L"(Windows ещё раз запросит подтверждение.)", APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
  ShellExecuteW(g_hMain, L"open", nd->path, NULL, NULL, SW_SHOWNORMAL);
}

static void ShowSavedMenu(void)
{
  HMENU pm = CreatePopupMenu();
  POINT pt;
  NodeData *nd = GetNodeIn(g_hSaved, g_curS);
  GetCursorPos(&pt);
  if (nd && !nd->folder) {
    AppendMenuW(pm, MF_STRING, IDM_JUMP, L"Перейти к ветке");
    AppendMenuW(pm, MF_STRING, IDM_JUMPNEW, L"Перейти в новом окне (/m)");
    AppendMenuW(pm, MF_STRING, IDM_NOTEPAD, L"Открыть в Блокноте");
    AppendMenuW(pm, MF_STRING, IDM_RESTORE, L"Восстановить в реестр...");
    SetMenuDefaultItem(pm, IDM_JUMP, FALSE);
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
  }
  if (nd) AppendMenuW(pm, MF_STRING, IDM_SHOWFILE, L"Показать в Проводнике");
  AppendMenuW(pm, MF_STRING, IDM_NEWFOLDER, L"Новая папка (на этом уровне)");
  if (nd && nd->folder) AppendMenuW(pm, MF_STRING, IDM_NEWSUBFOLDER, L"Новая подпапка (внутри)");
  AppendMenuW(pm, MF_STRING, IDM_NEWROOTFOLDER, L"Новая папка в корне архива");
  if (nd) {
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(pm, MF_STRING, IDM_RENAME, L"Переименовать\tF2");
    AppendMenuW(pm, MF_STRING, IDM_DELETE, L"Удалить (в корзину)\tDel");
  }
  AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
  AppendMenuW(pm, MF_STRING, IDM_REFRESH, L"Обновить\tF5");
  TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hMain, NULL);
  DestroyMenu(pm);
}

/* перетаскивание в архиве: перенос файлов и папок между папками (файлы на диске) */
static BOOL SavedComputeDrop(wchar_t *destDir, HTREEITEM *hl)
{
  POINT pt;
  RECT rc;
  TVHITTESTINFO hi;
  NodeData *sn = GetNodeIn(g_hSaved, g_dragS), *tn;
  size_t sl;
  *hl = NULL;
  if (!sn) return FALSE;
  GetCursorPos(&pt);
  GetWindowRect(g_hSaved, &rc);
  if (!PtInRect(&rc, pt)) return FALSE;
  ScreenToClient(g_hSaved, &pt);
  memset(&hi, 0, sizeof hi);
  hi.pt = pt;
  SendMessageW(g_hSaved, TVM_HITTEST, 0, (LPARAM)&hi);
  if (!hi.hItem) wcscpy(destDir, g_savedDir);
  else {
    tn = GetNodeIn(g_hSaved, hi.hItem);
    if (!tn) return FALSE;
    if (tn->folder) { wcscpy(destDir, tn->path); *hl = hi.hItem; }
    else {
      SavedParentDir(tn->path, destDir);
      *hl = (HTREEITEM)SendMessageW(g_hSaved, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)hi.hItem);
    }
  }
  if (sn->folder) {
    sl = wcslen(sn->path);
    if (_wcsnicmp(destDir, sn->path, sl) == 0 && (destDir[sl] == 0 || destDir[sl] == L'\\')) return FALSE;
  }
  return TRUE;
}

static void SavedDoDrop(const wchar_t *destDir)
{
  NodeData *sn = GetNodeIn(g_hSaved, g_dragS);
  wchar_t cur[MAX_PATH], np[MAX_PATH + 8];
  if (!sn) return;
  SavedParentDir(sn->path, cur);
  if (_wcsicmp(cur, destDir) == 0) return;
  _snwprintf(np, MAX_PATH + 7, L"%ls\\%ls", destDir, PathFindFileNameW(sn->path));
  if (GetFileAttributesW(np) != INVALID_FILE_ATTRIBUTES) { MessageBoxW(g_hMain, L"В папке назначения уже есть элемент с таким именем.", APP_NAME, MB_ICONWARNING); return; }
  if (!MoveFileW(sn->path, np)) { MessageBoxW(g_hMain, L"Не удалось переместить.", APP_NAME, MB_ICONERROR); return; }
  SavedRefresh(np);
}

static LRESULT SavedNotify(NMHDR *nh, LPARAM lp)
{
  switch (nh->code) {
    case TVN_SELCHANGEDW: {
      NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
      g_curS = n->itemNew.hItem;
      if (g_tab == 1) ShowSaved(g_curS);
      return 0;
    }
    case TVN_DELETEITEMW: {
      NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
      NodeData *nd = (NodeData *)n->itemOld.lParam;
      if (n->itemOld.hItem == g_curS) g_curS = NULL;
      if (nd) { free(nd->path); free(nd->comment); free(nd); }
      return 0;
    }
    case TVN_BEGINLABELEDITW: {
      HWND e = TreeView_GetEditControl(g_hSaved);
      if (e) SendMessageW(e, EM_LIMITTEXT, 200, 0);
      return FALSE;
    }
    case TVN_ENDLABELEDITW: SavedRename((NMTVDISPINFOW *)lp); return FALSE;
    case TVN_KEYDOWN: {
      NMTVKEYDOWN *k = (NMTVKEYDOWN *)lp;
      if (k->wVKey == VK_ESCAPE && g_dragging) ReleaseCapture();
      else if (k->wVKey == VK_DELETE) SavedDelete();
      else if (k->wVKey == VK_RETURN) SavedJump(FALSE);
      return 0;
    }
    case NM_DBLCLK: {
      NodeData *nd = GetNodeIn(g_hSaved, g_curS);
      if (nd && !nd->folder) { SavedJump(FALSE); return TRUE; }
      return 0;
    }
    case NM_RCLICK: {
      POINT pt;
      TVHITTESTINFO hi;
      GetCursorPos(&pt);
      ScreenToClient(g_hSaved, &pt);
      memset(&hi, 0, sizeof hi);
      hi.pt = pt;
      SendMessageW(g_hSaved, TVM_HITTEST, 0, (LPARAM)&hi);
      if (hi.flags & TVHT_ONITEM) SendMessageW(g_hSaved, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hi.hItem);
      ShowSavedMenu();
      return 0;
    }
    case TVN_BEGINDRAGW: {
      NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
      g_dragS = n->itemNew.hItem;
      g_dragSaved = TRUE;
      g_dragging = TRUE;
      SetCapture(g_hMain);
      return 0;
    }
  }
  return 0;
}

/* ---- действия верхнего уровня ---- */
static void OpenRegedit(BOOL newWin)
{
  wchar_t regedit[MAX_PATH];
  HINSTANCE h;
  GetWindowsDirectoryW(regedit, MAX_PATH);
  wcscat(regedit, L"\\regedit.exe");
  h = ShellExecuteW(NULL, L"open", regedit, newWin ? L"/m" : NULL, NULL, SW_SHOWNORMAL);
  Log(L"Открыть regedit (%ls): код %d", newWin ? L"/m" : L"обычно", (int)(INT_PTR)h);
  if ((INT_PTR)h <= 32) MessageBoxW(g_hMain, L"Не удалось запустить regedit.exe (возможно, вы отменили запрос UAC).", APP_NAME, MB_ICONWARNING);
}

static void MakeSummary(TargetList *tl, wchar_t *out, size_t cap)
{
  if (tl->n == 1) _snwprintf(out, cap - 1, L"Ветка: %ls", tl->t[0].path);
  else _snwprintf(out, cap - 1, L"%ls: %d веток", tl->fromChecks ? L"Отмеченные" : L"Выбранная папка", tl->n);
  out[cap - 1] = 0;
}

static BOOL EnsureDir(const wchar_t *dir)
{
  int r;
  if (GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES) return TRUE;
  r = SHCreateDirectoryExW(NULL, dir, NULL);
  return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

static void ActSave(void)
{
  TargetList tl;
  ExpDlg *d;
  SaveJob job;
  wchar_t sum[2200], msg[1200];
  if (g_tab != 0) { MessageBoxW(g_hMain, L"Откройте вкладку «Закладки» и выберите или отметьте ветки.", APP_NAME, MB_ICONINFORMATION); return; }
  memset(&tl, 0, sizeof tl);
  CollectTargets(&tl);
  if (tl.n == 0) {
    MessageBoxW(g_hMain, L"Нечего сохранять: выберите ветку (или папку с ветками) либо отметьте ветки флажками.", APP_NAME, MB_ICONINFORMATION);
    free(tl.t);
    return;
  }
  d = (ExpDlg *)calloc(1, sizeof *d);
  d->kind = 0; d->n = tl.n; d->firstKey = tl.t[0].path;
  MakeSummary(&tl, sum, 2200);
  d->summary = sum;
  if (g_tab == 0) {
    /* папка архива по умолчанию - корень */
    d->selDir[0] = 0;
  }
  if (!RunExpDlg(d)) { free(d); free(tl.t); return; }
  memset(&job, 0, sizeof job);
  job.tl = &tl;
  job.useTag = d->useTag; job.combine = d->combine;
  wcsncpy(job.note, d->note, 199);
  if (d->useDate) MakeTimestamp(job.ts, 32);
  wcsncpy(job.dir, d->destDir, MAX_PATH - 1);
  if (!d->toArchive) WritePrivateProfileStringW(L"Paths", L"SaveDir", d->destDir, g_iniFile);
  if (!EnsureDir(job.dir)) {
    MessageBoxW(g_hMain, L"Не удалось создать папку назначения.", APP_NAME, MB_ICONERROR);
    free(d); free(tl.t);
    return;
  }
  RunBusy(SaveWorker, &job, L"Сохранение веток");
  if (job.canceled) _snwprintf(msg, 1199, L"Отменено. Успешно сохранено: %d.", job.ok);
  else if (job.fail == 0) _snwprintf(msg, 1199, L"Готово. Сохранено: %d.\n\nПапка:\n%ls", job.ok, job.dir);
  else _snwprintf(msg, 1199, L"Сохранено: %d, с ошибкой: %d.\n\nПервая ошибка:\n%ls\n\n(Чаще всего это значит, что ветки нет в реестре или нужны права администратора.)", job.ok, job.fail, job.firstErr);
  msg[1199] = 0;
  Log(L"Сохранение: ok=%d fail=%d dir=%ls", job.ok, job.fail, job.dir);
  if (d->toArchive || _wcsnicmp(job.dir, g_savedDir, wcslen(g_savedDir)) == 0) {
    SavedRefresh(job.firstFile[0] ? job.firstFile : NULL);
    if (job.ok > 0) SwitchTab(1);
  }
  MessageBoxW(g_hMain, msg, APP_NAME, job.fail ? MB_ICONWARNING : MB_ICONINFORMATION);
  free(d);
  free(tl.t);
}

static void ActExpBat(void)
{
  TargetList tl;
  ExpDlg *d;
  wchar_t sum[2200], msg[1200];
  int i, made = 0, failed = 0;
  if (g_tab != 0) { MessageBoxW(g_hMain, L"Откройте вкладку «Закладки» и выберите или отметьте ветки.", APP_NAME, MB_ICONINFORMATION); return; }
  memset(&tl, 0, sizeof tl);
  CollectTargets(&tl);
  if (tl.n == 0) {
    MessageBoxW(g_hMain, L"Нечего экспортировать: выберите ветку (или папку с ветками) либо отметьте ветки флажками.", APP_NAME, MB_ICONINFORMATION);
    free(tl.t);
    return;
  }
  d = (ExpDlg *)calloc(1, sizeof *d);
  d->kind = 1; d->n = tl.n; d->firstKey = tl.t[0].path;
  MakeSummary(&tl, sum, 2200);
  d->summary = sum;
  if (!RunExpDlg(d)) { free(d); free(tl.t); return; }
  wcsncpy(g_batDir, d->destDir, MAX_PATH - 1);
  WritePrivateProfileStringW(L"Paths", L"BatDir", g_batDir, g_iniFile);
  if (!EnsureDir(g_batDir)) {
    MessageBoxW(g_hMain, L"Не удалось создать папку.", APP_NAME, MB_ICONERROR);
    free(d); free(tl.t);
    return;
  }
  for (i = 0; i < tl.n; i++) {
    int mode;
    for (mode = 0; mode < 2; mode++) {
      wchar_t name[400], full[MAX_PATH + 16], *text;
      BOOL nw = (mode == 1);
      if (d->batMode == 0 && nw) continue;
      if (d->batMode == 1 && !nw) continue;
      MakeBatName(name, 400, 250 - (int)wcslen(g_batDir) - 1 - 4, nw, tl.t[i].path);
      _snwprintf(full, MAX_PATH + 15, L"%ls\\%ls", g_batDir, name);
      text = BuildBat(tl.t[i].path, nw);
      if (text && WriteUtf8File(full, text)) made++; else failed++;
      free(text);
    }
  }
  Log(L"Экспорт bat: создано %d, ошибок %d, папка %ls", made, failed, g_batDir);
  _snwprintf(msg, 1199, L"Создано скриптов: %d%ls.\n\nПапка:\n%ls\n\nОткрыть эту папку?", made, failed ? L" (ошибок: см. лог)" : L"", g_batDir);
  msg[1199] = 0;
  if (MessageBoxW(g_hMain, msg, APP_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
    ShellExecuteW(g_hMain, L"open", g_batDir, NULL, NULL, SW_SHOWNORMAL);
  free(d);
  free(tl.t);
}

/* ---- главное окно ---- */
static void ApplyOptions(void)
{
  HMENU m = GetMenu(g_hMain);
  CheckMenuItem(m, IDM_OPT_NEWWIN, MF_BYCOMMAND | (g_newWin ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(m, IDM_OPT_ONTOP, MF_BYCOMMAND | (g_onTop ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(m, IDM_OPT_SHELL, MF_BYCOMMAND | (ShellMenuRegistered() ? MF_CHECKED : MF_UNCHECKED));
  SendMessageW(g_hChkNew, BM_SETCHECK, g_newWin ? BST_CHECKED : BST_UNCHECKED, 0);
  SetWindowPos(g_hMain, g_onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void SaveSettings(void)
{
  WINDOWPLACEMENT wp;
  wchar_t b[32];
  wp.length = sizeof wp;
  if (GetWindowPlacement(g_hMain, &wp)) {
    RECT r = wp.rcNormalPosition;
    _snwprintf(b, 31, L"%ld", r.left);   WritePrivateProfileStringW(L"Window", L"X", b, g_iniFile);
    _snwprintf(b, 31, L"%ld", r.top);    WritePrivateProfileStringW(L"Window", L"Y", b, g_iniFile);
    _snwprintf(b, 31, L"%ld", r.right - r.left);  WritePrivateProfileStringW(L"Window", L"W", b, g_iniFile);
    _snwprintf(b, 31, L"%ld", r.bottom - r.top);  WritePrivateProfileStringW(L"Window", L"H", b, g_iniFile);
  }
  WritePrivateProfileStringW(L"Options", L"NewWindow", g_newWin ? L"1" : L"0", g_iniFile);
  WritePrivateProfileStringW(L"Options", L"AlwaysOnTop", g_onTop ? L"1" : L"0", g_iniFile);
}

static void Layout(int cw, int ch)
{
  int m = 8, tw = cw * 42 / 100, x, rw, y = m, cmTop, y1, y2, ychk, tabH = 26, cntH = 18, treeTop, treeH, bw, bw3;
  if (tw < 200) tw = 200;
  if (tw > cw - 260) tw = cw - 260;
  MoveWindow(g_hTab, m, m, tw, tabH, TRUE);
  treeTop = m + tabH;
  treeH = ch - treeTop - m - cntH - 2;
  MoveWindow(g_hTree, m, treeTop, tw, treeH, TRUE);
  MoveWindow(g_hSaved, m, treeTop, tw, treeH, TRUE);
  MoveWindow(g_hCount, m, ch - m - cntH, tw, cntH, TRUE);
  x = tw + 2 * m;
  rw = cw - x - m;
  if (rw < 100) rw = 100;
  MoveWindow(g_hLbl[0], x, y, rw, 16, TRUE); y += 18;
  MoveWindow(g_hName, x, y, rw, 22, TRUE);   y += 28;
  MoveWindow(g_hLbl[1], x, y, rw, 16, TRUE); y += 18;
  MoveWindow(g_hPath, x, y, rw, 22, TRUE);   y += 28;
  MoveWindow(g_hLbl[2], x, y, rw, 16, TRUE); y += 18;
  cmTop = y;
  y2 = ch - m - 28;
  y1 = y2 - 6 - 28;
  ychk = y1 - 6 - 20;
  MoveWindow(g_hComment, x, cmTop, rw, ychk - 6 - cmTop, TRUE);
  MoveWindow(g_hChkNew, x, ychk, rw, 20, TRUE);
  bw = (rw - 8) / 2;
  MoveWindow(g_hBtnJump, x, y1, bw, 28, TRUE);
  MoveWindow(g_hBtnJumpNew, x + bw + 8, y1, bw, 28, TRUE);
  bw3 = (rw - 16) / 3;
  MoveWindow(g_hBtnX[0], x, y2, bw3, 28, TRUE);
  MoveWindow(g_hBtnX[1], x + bw3 + 8, y2, bw3, 28, TRUE);
  MoveWindow(g_hBtnX[2], x + 2 * (bw3 + 8), y2, bw3, 28, TRUE);
}

static void ShowTreeMenu(void)
{
  HMENU pm = CreatePopupMenu();
  POINT pt;
  NodeData *nd = GetNode(g_cur);
  GetCursorPos(&pt);
  if (nd && !nd->folder) {
    AppendMenuW(pm, MF_STRING, IDM_JUMP, L"Перейти в regedit");
    AppendMenuW(pm, MF_STRING, IDM_JUMPNEW, L"Перейти в новом окне (/m)");
    AppendMenuW(pm, MF_STRING, IDM_COPYPATH, L"Копировать путь");
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
    SetMenuDefaultItem(pm, IDM_JUMP, FALSE);
  }
  if (nd) {
    AppendMenuW(pm, MF_STRING, IDM_SAVE, L"Сохранить ветку(и) в .reg...\tCtrl+S");
    AppendMenuW(pm, MF_STRING, IDM_EXPBAT, L"Экспорт в .bat...");
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
  }
  AppendMenuW(pm, MF_STRING, IDM_NEWKEY, L"Добавить ветку\tCtrl+N");
  AppendMenuW(pm, MF_STRING, IDM_NEWFOLDER, L"Добавить папку (на этом уровне)\tCtrl+Shift+N");
  if (nd && nd->folder) AppendMenuW(pm, MF_STRING, IDM_NEWSUBFOLDER, L"Добавить подпапку (внутри)");
  AppendMenuW(pm, MF_STRING, IDM_NEWROOTFOLDER, L"Добавить папку в корень");
  if (nd) {
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(pm, MF_STRING, IDM_RENAME, L"Переименовать\tF2");
    AppendMenuW(pm, MF_STRING, IDM_DELETE, L"Удалить\tDel");
  }
  AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
  AppendMenuW(pm, MF_STRING, IDM_CHECKALL, L"Отметить все ветки");
  AppendMenuW(pm, MF_STRING, IDM_UNCHECKALL, L"Снять все отметки");
  TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hMain, NULL);
  DestroyMenu(pm);
}

/* Позиция вставки при перетаскивании: 0 - внутрь папки, 1 - перед элементом, 2 - после, 3 - в конец корня */
typedef struct { HTREEITEM target; int mode; } DropPos;

static BOOL ComputeDrop(DropPos *dp)
{
  POINT pt;
  TVHITTESTINFO hi;
  RECT rc, ir;
  NodeData *tn;
  int h, rel;
  dp->target = NULL;
  dp->mode = 3;
  GetCursorPos(&pt);
  GetWindowRect(g_hTree, &rc);
  if (!PtInRect(&rc, pt)) return FALSE;
  ScreenToClient(g_hTree, &pt);
  memset(&hi, 0, sizeof hi);
  hi.pt = pt;
  TreeView_HitTest(g_hTree, &hi);
  if (!hi.hItem) return TRUE; /* пустое место: в конец корня */
  dp->target = hi.hItem;
  if (dp->target == g_drag || IsDescendant(dp->target, g_drag)) return FALSE;
  if (!TreeView_GetItemRect(g_hTree, dp->target, &ir, FALSE)) { dp->mode = 2; return TRUE; }
  h = ir.bottom - ir.top;
  if (h < 4) h = 4;
  rel = pt.y - ir.top;
  tn = GetNode(dp->target);
  if (tn && tn->folder) dp->mode = (rel < h / 4) ? 1 : (rel > h * 3 / 4) ? 2 : 0;
  else dp->mode = (rel < h / 2) ? 1 : 2;
  return TRUE;
}

static void DropFeedback(const DropPos *dp)
{
  if (!dp) { TreeView_SelectDropTarget(g_hTree, NULL); TreeView_SetInsertMark(g_hTree, NULL, FALSE); return; }
  if (dp->mode == 0) { TreeView_SetInsertMark(g_hTree, NULL, FALSE); TreeView_SelectDropTarget(g_hTree, dp->target); }
  else if (dp->mode == 1 || dp->mode == 2) { TreeView_SelectDropTarget(g_hTree, NULL); TreeView_SetInsertMark(g_hTree, dp->target, dp->mode == 2); }
  else { TreeView_SelectDropTarget(g_hTree, NULL); TreeView_SetInsertMark(g_hTree, NULL, FALSE); }
}

static void DragAutoScroll(void)
{
  POINT pt;
  RECT rc;
  GetCursorPos(&pt);
  GetClientRect(g_hTree, &rc);
  ScreenToClient(g_hTree, &pt);
  if (pt.x < 0 || pt.x >= rc.right) return;
  if (pt.y >= 0 && pt.y < 18) SendMessageW(g_hTree, WM_VSCROLL, SB_LINEUP, 0);
  else if (pt.y > rc.bottom - 18 && pt.y <= rc.bottom) SendMessageW(g_hTree, WM_VSCROLL, SB_LINEDOWN, 0);
}

static void DoDrop(const DropPos *dp)
{
  HTREEITEM parent = TVI_ROOT, after = TVI_LAST, n, prev, t = dp->target;
  CommitEdits();
  if (dp->mode == 0) { parent = t; after = TVI_LAST; }
  else if (dp->mode == 1 || dp->mode == 2) {
    parent = TreeView_GetParent(g_hTree, t);
    if (!parent) parent = TVI_ROOT;
    if (dp->mode == 2) after = t;
    else { prev = TreeView_GetPrevSibling(g_hTree, t); after = prev ? prev : TVI_FIRST; }
  }
  n = CopySub(g_drag, parent, after);
  g_cur = NULL;
  TreeView_DeleteItem(g_hTree, g_drag);
  if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
  TreeView_SelectItem(g_hTree, n);
  InvalidateRect(g_hTree, NULL, TRUE);
  MarkDirty();
}

static void SeedDefaults(void)
{
  HTREEITEM f1 = InsertNode(TVI_ROOT, TVI_LAST, L"Автозагрузка", TRUE, L"", L"Где программы запускаются вместе с Windows");
  HTREEITEM f2 = InsertNode(TVI_ROOT, TVI_LAST, L"Контекстное меню", TRUE, L"", L"Пункты правой кнопки мыши");
  InsertNode(f1, TVI_LAST, L"Run (пользователь)", FALSE, L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Автозапуск для текущего пользователя");
  InsertNode(f1, TVI_LAST, L"Run (все пользователи)", FALSE, L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"Автозапуск для всех пользователей");
  InsertNode(f2, TVI_LAST, L"Меню для всех файлов", FALSE, L"HKEY_CLASSES_ROOT\\*\\shell", L"Команды, которые видны для любого файла");
  InsertNode(f2, TVI_LAST, L"Меню для папок", FALSE, L"HKEY_CLASSES_ROOT\\Directory\\shell", L"Команды для папок");
  ExpandAll(TVI_ROOT);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  switch (m) {
    case WM_CREATE: {
      HIMAGELIST il;
      SHFILEINFOW sfi;
      wchar_t win[MAX_PATH], rg[MAX_PATH];
      HWND *all[] = { &g_hTree, &g_hName, &g_hPath, &g_hComment, &g_hBtnJump, &g_hBtnJumpNew, &g_hChkNew, &g_hTab, &g_hSaved, &g_hCount, &g_hBtnX[0], &g_hBtnX[1], &g_hBtnX[2] };
      size_t i;
      g_hMain = h;
      g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_INFOTIP | TVS_EDITLABELS | TVS_CHECKBOXES, 0, 0, 10, 10, h, (HMENU)IDC_TREE, g_hInst, NULL);
      g_hLbl[0] = CreateWindowExW(0, L"STATIC", L"Название:", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)-1, g_hInst, NULL);
      g_hName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)IDC_NAME, g_hInst, NULL);
      g_hLbl[1] = CreateWindowExW(0, L"STATIC", L"Путь в реестре:", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)-1, g_hInst, NULL);
      g_hPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)IDC_PATH, g_hInst, NULL);
      g_hLbl[2] = CreateWindowExW(0, L"STATIC", L"Комментарий:", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)-1, g_hInst, NULL);
      g_hComment = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                                   ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, h, (HMENU)IDC_COMMENT, g_hInst, NULL);
      g_hChkNew = CreateWindowExW(0, L"BUTTON", L"Открывать в отдельном окне regedit (/m)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  0, 0, 10, 10, h, (HMENU)IDC_NEWWIN, g_hInst, NULL);
      g_hBtnJump = CreateWindowExW(0, L"BUTTON", L"Перейти", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)IDC_JUMP, g_hInst, NULL);
      g_hBtnJumpNew = CreateWindowExW(0, L"BUTTON", L"Перейти в новом окне", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)IDC_JUMPNEW, g_hInst, NULL);
      {
        TCITEMW ti;
        int k;
        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FOCUSNEVER, 0, 0, 10, 10, h, (HMENU)IDC_TAB, g_hInst, NULL);
        memset(&ti, 0, sizeof ti);
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"Закладки";
        TabCtrl_InsertItem(g_hTab, 0, &ti);
        ti.pszText = (LPWSTR)L"Сохранённые";
        TabCtrl_InsertItem(g_hTab, 1, &ti);
        g_hSaved = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
                                   TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS, 0, 0, 10, 10, h, (HMENU)IDC_SAVED, g_hInst, NULL);
        g_hCount = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 0, 0, 10, 10, h, (HMENU)IDC_COUNT, g_hInst, NULL);
        for (k = 0; k < 3; k++)
          g_hBtnX[k] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)(INT_PTR)(IDC_X1 + k), g_hInst, NULL);
      }
      for (i = 0; i < sizeof all / sizeof all[0]; i++) DlgSetFont(*all[i]);
      for (i = 0; i < 3; i++) DlgSetFont(g_hLbl[i]);
      SendMessageW(g_hName, EM_LIMITTEXT, 255, 0);
      SendMessageW(g_hPath, EM_LIMITTEXT, 2000, 0);
      /* значки: системный список иконок */
      GetWindowsDirectoryW(win, MAX_PATH);
      _snwprintf(rg, MAX_PATH - 1, L"%ls\\regedit.exe", win);
      rg[MAX_PATH - 1] = 0;
      memset(&sfi, 0, sizeof sfi);
      il = (HIMAGELIST)SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof sfi,
                                      SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
      g_imgFolder = sfi.iIcon;
      memset(&sfi, 0, sizeof sfi);
      SHGetFileInfoW(rg, 0, &sfi, sizeof sfi, SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
      g_imgKey = sfi.iIcon;
      if (il) TreeView_SetImageList(g_hTree, il, TVSIL_NORMAL);
      memset(&sfi, 0, sizeof sfi);
      SHGetFileInfoW(L"x.reg", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi, SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
      g_imgReg = sfi.iIcon;
      if (il) TreeView_SetImageList(g_hSaved, il, TVSIL_NORMAL);
      return 0;
    }
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED) Layout(LOWORD(lp), HIWORD(lp));
      return 0;
    case WM_GETMINMAXINFO: {
      MINMAXINFO *mi = (MINMAXINFO *)lp;
      mi->ptMinTrackSize.x = 640; mi->ptMinTrackSize.y = 400;
      return 0;
    }
    case WM_SETFOCUS: SetFocus(g_tab == 0 ? g_hTree : g_hSaved); return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if ((id == IDC_NAME || id == IDC_PATH || id == IDC_COMMENT) && code == EN_CHANGE) {
        if (!g_loading && g_tab == 0) {
          if (id == IDC_NAME && g_cur) {
            wchar_t nb[300];
            GetWindowTextW(g_hName, nb, 300);
            Trim(nb);
            if (nb[0]) SetItemTextW2(g_cur, nb);
          }
          MarkDirty();
        }
        return 0;
      }
      switch (id) {
        case IDOK: if (GetFocus() == g_hTree) ActJump(FALSE); else if (GetFocus() == g_hSaved) SavedJump(FALSE); break;
        case IDC_JUMP: case IDM_JUMP: if (g_tab == 0) ActJump(FALSE); else SavedJump(FALSE); break;
        case IDC_JUMPNEW: case IDM_JUMPNEW: if (g_tab == 0) ActJump(TRUE); else SavedJump(TRUE); break;
        case IDC_NEWWIN:
          g_newWin = SendMessageW(g_hChkNew, BM_GETCHECK, 0, 0) == BST_CHECKED;
          ApplyOptions();
          break;
        case IDM_OPT_NEWWIN: g_newWin = !g_newWin; ApplyOptions(); break;
        case IDM_OPT_ONTOP:  g_onTop = !g_onTop; ApplyOptions(); break;
        case IDM_OPT_SHELL:
          if (!SetShellMenu(!ShellMenuRegistered()))
            MessageBoxW(h, L"Не удалось изменить пункт контекстного меню.", APP_NAME, MB_ICONERROR);
          ApplyOptions();
          break;
        case IDM_NEWKEY: if (g_tab == 0) ActNewKey(); break;
        case IDM_NEWFOLDER: if (g_tab == 0) ActNewFolder(0); else SavedNewFolder(0); break;
        case IDM_NEWSUBFOLDER: if (g_tab == 0) ActNewFolder(1); else SavedNewFolder(1); break;
        case IDM_NEWROOTFOLDER: if (g_tab == 0) ActNewFolder(2); else SavedNewFolder(2); break;
        case IDM_RENAME:
          if (g_tab == 0) ActRename();
          else if (g_curS) { SetFocus(g_hSaved); TreeView_EditLabel(g_hSaved, g_curS); }
          break;
        case IDM_DELETE: if (g_tab == 0) ActDelete(); else SavedDelete(); break;
        case IDM_COPYPATH: ActCopyPath(); break;
        case IDM_ADDREG: ActAddReg(); break;
        case IDM_SAVE: ActSave(); break;
        case IDM_EXPBAT: ActExpBat(); break;
        case IDM_OPENREG_N: OpenRegedit(FALSE); break;
        case IDM_OPENREG_W: OpenRegedit(TRUE); break;
        case IDM_CHECKALL: if (g_tab == 0) { SetCheckAll(TRUE); UpdateCount(); } break;
        case IDM_UNCHECKALL: if (g_tab == 0) { SetCheckAll(FALSE); UpdateCount(); } break;
        case IDM_REFRESH: SavedRefresh(NULL); break;
        case IDM_OPENARCH: CreateDirectoryW(g_savedDir, NULL); ShellExecuteW(h, L"open", g_savedDir, NULL, NULL, SW_SHOWNORMAL); break;
        case IDM_NOTEPAD: SavedNotepad(); break;
        case IDM_SHOWFILE: SavedShow(); break;
        case IDM_RESTORE: SavedRestore(); break;
        case IDC_X1: PostMessageW(h, WM_COMMAND, g_tab == 0 ? IDM_SAVE : IDM_NOTEPAD, 0); break;
        case IDC_X2: PostMessageW(h, WM_COMMAND, g_tab == 0 ? IDM_EXPBAT : IDM_SHOWFILE, 0); break;
        case IDC_X3:
          if (g_tab == 0) OpenRegedit(g_newWin); else SavedRestore();
          break;
        case IDM_IMPORT: ActImport(); break;
        case IDM_EXPORT: ActExport(); break;
        case IDM_EXIT: SendMessageW(h, WM_CLOSE, 0, 0); break;
        case IDM_ABOUT: {
          wchar_t b[900];
          _snwprintf(b, 899, L"%ls %ls\n\nЗакладки веток реестра с папками и комментариями.\n"
                     L"Переход в regedit, в том числе в отдельном окне (/m).\n\nДанные:\n%ls\n\nЛог:\n%ls",
                     APP_NAME, APP_VER, g_dataFile, g_logFile);
          b[899] = 0;
          MessageBoxW(h, b, L"О программе", MB_ICONINFORMATION);
          break;
        }
      }
      return 0;
    }
    case WM_NOTIFY: {
      NMHDR *nh = (NMHDR *)lp;
      if (nh->hwndFrom == g_hTab) { if (nh->code == TCN_SELCHANGE) SwitchTab(TabCtrl_GetCurSel(g_hTab)); return 0; }
      if (nh->hwndFrom == g_hSaved) return SavedNotify(nh, lp);
      if (nh->hwndFrom != g_hTree) break;
      switch (nh->code) {
        case TVN_SELCHANGINGW: CommitEdits(); return FALSE;
        case TVN_SELCHANGEDW: {
          NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
          g_cur = n->itemNew.hItem;
          ShowNode(g_cur);
          return 0;
        }
        case TVN_DELETEITEMW: {
          NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
          NodeData *nd = (NodeData *)n->itemOld.lParam;
          if (n->itemOld.hItem == g_cur) g_cur = NULL;
          if (nd) { free(nd->path); free(nd->comment); free(nd); }
          return 0;
        }
        case TVN_ITEMEXPANDEDW: {
          NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
          NodeData *nd = GetNode(n->itemNew.hItem);
          if (nd && nd->folder) {
            TVITEMW tv;
            memset(&tv, 0, sizeof tv);
            tv.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            tv.hItem = n->itemNew.hItem;
            tv.iImage = tv.iSelectedImage = g_imgFolder;
            TreeView_SetItem(g_hTree, &tv);
          }
          return 0;
        }
        case TVN_GETINFOTIPW: {
          NMTVGETINFOTIPW *t = (NMTVGETINFOTIPW *)lp;
          NodeData *nd = GetNode(t->hItem);
          wchar_t tip[2600];
          if (nd && t->pszText && t->cchTextMax > 1) {
            tip[0] = 0;
            if (!nd->folder && nd->path[0]) wcsncat(tip, nd->path, 1200);
            if (!nd->folder && nd->path[0] && nd->comment[0]) wcscat(tip, L"\r\n");
            if (nd->comment[0]) wcsncat(tip, nd->comment, 1200);
            wcsncpy(t->pszText, tip, t->cchTextMax - 1);
            t->pszText[t->cchTextMax - 1] = 0;
          }
          return 0;
        }
        case TVN_BEGINLABELEDITW: {
          HWND e = TreeView_GetEditControl(g_hTree);
          if (e) SendMessageW(e, EM_LIMITTEXT, 255, 0);
          return FALSE;
        }
        case TVN_ENDLABELEDITW: {
          NMTVDISPINFOW *di = (NMTVDISPINFOW *)lp;
          wchar_t nb[300];
          if (!di->item.pszText) return FALSE;
          wcsncpy(nb, di->item.pszText, 299); nb[299] = 0;
          Trim(nb);
          if (!nb[0]) return FALSE;
          if (di->item.hItem == g_cur) {
            g_loading = TRUE;
            SetWindowTextW(g_hName, nb);
            g_loading = FALSE;
          }
          SetItemTextW2(di->item.hItem, nb);
          MarkDirty();
          return FALSE; /* текст уже применён вручную (с обрезкой пробелов) */
        }
        case TVN_KEYDOWN: {
          NMTVKEYDOWN *k = (NMTVKEYDOWN *)lp;
          if (k->wVKey == VK_ESCAPE && g_dragging) ReleaseCapture();
          else if (k->wVKey == VK_DELETE) ActDelete();
          else if (k->wVKey == VK_RETURN) ActJump(FALSE);
          else if (k->wVKey == VK_SPACE && g_cur) PostMessageW(h, WM_APP_CHECK, (WPARAM)g_cur, 0);
          return 0;
        }
        case NM_CLICK: {
          POINT pt;
          TVHITTESTINFO hi;
          GetCursorPos(&pt);
          ScreenToClient(g_hTree, &pt);
          memset(&hi, 0, sizeof hi);
          hi.pt = pt;
          TreeView_HitTest(g_hTree, &hi);
          if (hi.hItem && (hi.flags & TVHT_ONITEMSTATEICON)) PostMessageW(h, WM_APP_CHECK, (WPARAM)hi.hItem, 0);
          return 0;
        }
        case NM_DBLCLK: {
          NodeData *nd = GetNode(g_cur);
          if (nd && !nd->folder) { ActJump(FALSE); return TRUE; }
          return 0;
        }
        case NM_RCLICK: {
          POINT pt;
          TVHITTESTINFO hi;
          GetCursorPos(&pt);
          ScreenToClient(g_hTree, &pt);
          memset(&hi, 0, sizeof hi);
          hi.pt = pt;
          TreeView_HitTest(g_hTree, &hi);
          if (hi.flags & TVHT_ONITEM) TreeView_SelectItem(g_hTree, hi.hItem);
          ShowTreeMenu();
          return 0;
        }
        case TVN_BEGINDRAGW: {
          NMTREEVIEWW *n = (NMTREEVIEWW *)lp;
          g_drag = n->itemNew.hItem;
          g_dragging = TRUE;
          SetCapture(h);
          return 0;
        }
      }
      break;
    }
    case WM_MOUSEMOVE:
      if (g_dragging) {
        if (g_dragSaved) {
          wchar_t dest[MAX_PATH];
          HTREEITEM hl;
          BOOL ok = SavedComputeDrop(dest, &hl);
          TreeView_SelectDropTarget(g_hSaved, ok ? hl : NULL);
          SetCursor(LoadCursorW(NULL, ok ? IDC_ARROW : IDC_NO));
        } else {
          DropPos dp;
          BOOL ok = ComputeDrop(&dp);
          DropFeedback(ok ? &dp : NULL);
          SetCursor(LoadCursorW(NULL, ok ? IDC_ARROW : IDC_NO));
          DragAutoScroll();
        }
      }
      return 0;
    case WM_LBUTTONUP:
      if (g_dragging) {
        if (g_dragSaved) {
          wchar_t dest[MAX_PATH];
          HTREEITEM hl;
          BOOL ok = SavedComputeDrop(dest, &hl);
          ReleaseCapture();
          if (ok) SavedDoDrop(dest);
        } else {
          DropPos dp;
          BOOL ok = ComputeDrop(&dp);
          ReleaseCapture();
          if (ok) DoDrop(&dp);
        }
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (g_dragging) {
        g_dragging = FALSE;
        if (g_dragSaved) { TreeView_SelectDropTarget(g_hSaved, NULL); g_dragSaved = FALSE; }
        else DropFeedback(NULL);
      }
      return 0;
    case WM_TIMER:
      if (wp == TIMER_SAVE) { KillTimer(h, TIMER_SAVE); if (g_dirty) SaveAll(); }
      return 0;
    case WM_COPYDATA: {
      COPYDATASTRUCT *cd = (COPYDATASTRUCT *)lp;
      if (cd->dwData == 1 && cd->lpData && cd->cbData >= 2) {
        wchar_t *s = (wchar_t *)malloc(cd->cbData + 4);
        memcpy(s, cd->lpData, cd->cbData);
        s[cd->cbData / 2] = 0;
        s[cd->cbData / 2 + 1] = 0;
        EnqueueFile(s);
        free(s);
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return TRUE;
      }
      return FALSE;
    }
    case WM_DROPFILES: {
      HDROP hd = (HDROP)wp;
      UINT i, n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
      for (i = 0; i < n; i++) {
        wchar_t f[MAX_PATH];
        if (DragQueryFileW(hd, i, f, MAX_PATH)) EnqueueFile(f);
      }
      DragFinish(hd);
      return 0;
    }
    case WM_APP_CHECK: {
      HTREEITEM it = (HTREEITEM)wp;
      NodeData *nd = GetNode(it);
      if (nd && nd->folder) {
        BOOL st = TreeView_GetCheckState(g_hTree, it) == 1;
        HTREEITEM c;
        for (c = TreeView_GetChild(g_hTree, it); c; c = TreeView_GetNextSibling(g_hTree, c)) SetCheckRec(c, st);
      }
      UpdateCount();
      return 0;
    }
    case WM_APP_SAVEDREFRESH:
      SavedRefresh(g_pendingSelect[0] ? g_pendingSelect : NULL);
      g_pendingSelect[0] = 0;
      return 0;
    case WM_APP_QUEUE: ProcessQueue(); return 0;
    case WM_APP_JUMPFAIL:
      if (wp == 1) MessageBoxW(h, L"Не удалось записать параметр LastKey в реестр (HKCU\\...\\Applets\\Regedit).", APP_NAME, MB_ICONERROR);
      else MessageBoxW(h, L"Не удалось запустить regedit.exe (возможно, вы отменили запрос UAC).", APP_NAME, MB_ICONWARNING);
      return 0;
    case WM_ENDSESSION:
      if (wp) { SaveAll(); SaveSettings(); }
      return 0;
    case WM_CLOSE:
      SaveAll();
      SaveSettings();
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

static void InitPaths(void)
{
  wchar_t *sl, test[MAX_PATH + 24];
  HANDLE t;
  GetModuleFileNameW(NULL, g_exe, MAX_PATH);
  wcscpy(g_dataDir, g_exe);
  sl = wcsrchr(g_dataDir, L'\\');
  if (sl) *sl = 0;
  _snwprintf(test, MAX_PATH + 23, L"%ls\\keyshelf.wtest", g_dataDir);
  t = CreateFileW(test, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
  else {
    wchar_t ad[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH)) {
      _snwprintf(g_dataDir, MAX_PATH - 1, L"%ls\\KeyShelf", ad);
      g_dataDir[MAX_PATH - 1] = 0;
      CreateDirectoryW(g_dataDir, NULL);
    }
  }
  _snwprintf(g_dataFile, MAX_PATH - 1, L"%ls\\keyshelf.ksl", g_dataDir);
  _snwprintf(g_iniFile, MAX_PATH - 1, L"%ls\\keyshelf.ini", g_dataDir);
  _snwprintf(g_logFile, MAX_PATH - 1, L"%ls\\keyshelf.log", g_dataDir);
  {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(g_logFile, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 256 * 1024) DeleteFileW(g_logFile);
  }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
  INITCOMMONCONTROLSEX icc;
  WNDCLASSEXW wc;
  NONCLIENTMETRICSW ncm;
  int argc = 0, i, x, y, w, hgt;
  wchar_t **argv;
  HANDLE mtx;
  BOOL already;
  MSG msg;
  HACCEL acc;
  ACCEL ac[6];
  HMENU mb, mf, ms, mh;
  RECT vs;
  (void)hPrev; (void)cmd;
  g_hInst = hInst;
  InitPaths();
  _snwprintf(g_savedDir, MAX_PATH - 1, L"%ls\\Saved", g_dataDir);
  {
    wchar_t defbat[MAX_PATH];
    _snwprintf(defbat, MAX_PATH - 1, L"%ls\\Bat", g_dataDir);
    GetPrivateProfileStringW(L"Paths", L"BatDir", defbat, g_batDir, MAX_PATH, g_iniFile);
  }
  InitializeCriticalSection(&g_cs);
  OleInitialize(NULL);

  argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  mtx = CreateMutexW(NULL, FALSE, MUTEX_NAME);
  already = (GetLastError() == ERROR_ALREADY_EXISTS);
  if (already) {
    HWND other = NULL;
    for (i = 0; i < 80 && !other; i++) { other = FindWindowW(CLS_MAIN, NULL); if (!other) Sleep(100); }
    if (other) {
      AllowSetForegroundWindow(ASFW_ANY);
      if (argc >= 3 && _wcsicmp(argv[1], L"/addreg") == 0) {
        int k;
        for (k = 2; k < argc; k++) {
          COPYDATASTRUCT cd;
          cd.dwData = 1;
          cd.cbData = (DWORD)((wcslen(argv[k]) + 1) * sizeof(wchar_t));
          cd.lpData = argv[k];
          SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cd);
        }
      } else {
        if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
        SetForegroundWindow(other);
      }
    }
    return 0;
  }

  Log(L"==== запуск %ls %ls, данные: %ls ====", APP_NAME, APP_VER, g_dataDir);
  g_jumpMutex = CreateMutexW(NULL, FALSE, NULL);
  g_newWin = GetPrivateProfileIntW(L"Options", L"NewWindow", 1, g_iniFile) != 0;
  g_onTop = GetPrivateProfileIntW(L"Options", L"AlwaysOnTop", 0, g_iniFile) != 0;

  icc.dwSize = sizeof icc;
  icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&icc);
  memset(&ncm, 0, sizeof ncm);
  ncm.cbSize = sizeof ncm;
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  g_font = CreateFontIndirectW(&ncm.lfMessageFont);

  memset(&wc, 0, sizeof wc);
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = MainProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.hIcon = wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
  wc.lpszClassName = CLS_MAIN;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = DlgProc;
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = CLS_DLG;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = ExpProc;
  wc.lpszClassName = CLS_EXP;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = BusyProc;
  wc.lpszClassName = CLS_BUSY;
  RegisterClassExW(&wc);

  mb = CreateMenu(); mf = CreatePopupMenu(); ms = CreatePopupMenu(); mh = CreatePopupMenu();
  AppendMenuW(mf, MF_STRING, IDM_NEWKEY, L"Добавить &ветку\tCtrl+N");
  AppendMenuW(mf, MF_STRING, IDM_NEWFOLDER, L"Добавить &папку (на этом уровне)\tCtrl+Shift+N");
  AppendMenuW(mf, MF_STRING, IDM_NEWSUBFOLDER, L"Добавить &подпапку (внутри выбранной)");
  AppendMenuW(mf, MF_STRING, IDM_NEWROOTFOLDER, L"Добавить папку в &корень");
  AppendMenuW(mf, MF_STRING, IDM_RENAME, L"&Переименовать\tF2");
  AppendMenuW(mf, MF_STRING, IDM_DELETE, L"&Удалить\tDel");
  AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
  AppendMenuW(mf, MF_STRING, IDM_ADDREG, L"Добавить ветки из &.reg-файла...\tCtrl+O");
  AppendMenuW(mf, MF_STRING, IDM_IMPORT, L"&Импорт списка...");
  AppendMenuW(mf, MF_STRING, IDM_EXPORT, L"&Экспорт списка...");
  AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
  AppendMenuW(mf, MF_STRING, IDM_EXIT, L"В&ыход");
  AppendMenuW(ms, MF_STRING, IDM_OPT_NEWWIN, L"Открывать в отдельном окне regedit (/m)");
  AppendMenuW(ms, MF_STRING, IDM_OPT_ONTOP, L"Поверх всех окон");
  AppendMenuW(ms, MF_SEPARATOR, 0, NULL);
  AppendMenuW(ms, MF_STRING, IDM_OPT_SHELL, L"Пункт «Добавить в KeyShelf» в меню .reg-файлов");
  AppendMenuW(mh, MF_STRING, IDM_ABOUT, L"&О программе");
  AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, L"&Файл");
  {
    HMENU mv = CreatePopupMenu(), mc = CreatePopupMenu(), ma = CreatePopupMenu();
    AppendMenuW(mv, MF_STRING, IDM_JUMP, L"Перейти\tEnter");
    AppendMenuW(mv, MF_STRING, IDM_JUMPNEW, L"Перейти в новом окне (/m)");
    AppendMenuW(mv, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mv, MF_STRING, IDM_OPENREG_N, L"Открыть regedit (где был закрыт)");
    AppendMenuW(mv, MF_STRING, IDM_OPENREG_W, L"Открыть regedit в новом окне (/m)");
    AppendMenuW(mv, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mv, MF_STRING, IDM_SAVE, L"Сохранить ветку(и) в .reg...\tCtrl+S");
    AppendMenuW(mv, MF_STRING, IDM_EXPBAT, L"Экспорт ветки(ей) в .bat...");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mv, L"&Ветка");
    AppendMenuW(mc, MF_STRING, IDM_CHECKALL, L"Отметить все ветки");
    AppendMenuW(mc, MF_STRING, IDM_UNCHECKALL, L"Снять все отметки");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mc, L"&Выбор");
    AppendMenuW(ma, MF_STRING, IDM_REFRESH, L"Обновить\tF5");
    AppendMenuW(ma, MF_STRING, IDM_OPENARCH, L"Открыть папку архива в Проводнике");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)ma, L"&Архив");
  }
  AppendMenuW(mb, MF_POPUP, (UINT_PTR)ms, L"&Настройки");
  AppendMenuW(mb, MF_POPUP, (UINT_PTR)mh, L"&Справка");

  w = GetPrivateProfileIntW(L"Window", L"W", 900, g_iniFile);
  hgt = GetPrivateProfileIntW(L"Window", L"H", 560, g_iniFile);
  x = GetPrivateProfileIntW(L"Window", L"X", CW_USEDEFAULT, g_iniFile);
  y = GetPrivateProfileIntW(L"Window", L"Y", CW_USEDEFAULT, g_iniFile);
  if (w < 640) w = 640;
  if (hgt < 400) hgt = 400;
  vs.left = GetSystemMetrics(SM_XVIRTUALSCREEN); vs.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  vs.right = vs.left + GetSystemMetrics(SM_CXVIRTUALSCREEN); vs.bottom = vs.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (x != CW_USEDEFAULT && (x < vs.left - 50 || x > vs.right - 100 || y < vs.top - 10 || y > vs.bottom - 100)) x = y = CW_USEDEFAULT;

  g_hMain = CreateWindowExW(0, CLS_MAIN, L"KeyShelf — закладки веток реестра", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, w, hgt, NULL, mb, hInst, NULL);
  if (!g_hMain) { Log(L"ОШИБКА: не создано окно (%lu)", GetLastError()); return 1; }

  if (!LoadFrom(g_dataFile, TVI_ROOT)) SeedDefaults();
  else ExpandAll(TVI_ROOT);
  g_dirty = FALSE;
  ShowNode(NULL);
  SavedRefresh(NULL);
  SwitchTab(0);
  if (ShellMenuRegistered()) SetShellMenu(TRUE); /* обновить путь к exe, если файл переместили */
  DragAcceptFiles(g_hMain, TRUE);
  ApplyOptions();
  ShowWindow(g_hMain, show);
  UpdateWindow(g_hMain);
  {
    HTREEITEM first = TreeView_GetRoot(g_hTree);
    if (first) TreeView_SelectItem(g_hTree, first);
  }
  UpdateCount();

  ac[0].fVirt = FCONTROL | FVIRTKEY; ac[0].key = 'N'; ac[0].cmd = IDM_NEWKEY;
  ac[1].fVirt = FCONTROL | FSHIFT | FVIRTKEY; ac[1].key = 'N'; ac[1].cmd = IDM_NEWFOLDER;
  ac[2].fVirt = FCONTROL | FVIRTKEY; ac[2].key = 'O'; ac[2].cmd = IDM_ADDREG;
  ac[3].fVirt = FVIRTKEY; ac[3].key = VK_F2; ac[3].cmd = IDM_RENAME;
  ac[4].fVirt = FCONTROL | FVIRTKEY; ac[4].key = 'S'; ac[4].cmd = IDM_SAVE;
  ac[5].fVirt = FVIRTKEY; ac[5].key = VK_F5; ac[5].cmd = IDM_REFRESH;
  acc = CreateAcceleratorTableW(ac, 6);

  if (argc >= 3 && _wcsicmp(argv[1], L"/addreg") == 0) {
    for (i = 2; i < argc; i++) EnqueueFile(argv[i]);
  }
  if (argv) LocalFree(argv);

  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    {
      HWND ed = TreeView_GetEditControl(g_hTree), ed2 = TreeView_GetEditControl(g_hSaved);
      if ((ed && msg.hwnd == ed) || (ed2 && msg.hwnd == ed2)) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
    }
    if (TranslateAcceleratorW(g_hMain, acc, &msg)) continue;
    if (IsDialogMessageW(g_hMain, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  CloseHandle(mtx);
  return (int)msg.wParam;
}
