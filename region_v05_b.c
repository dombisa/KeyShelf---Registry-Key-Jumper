
/* ---- интерфейс вкладки «Наблюдение» ---- */
static void WatchClearEvents(void)
{
  int i;
  EnterCriticalSection(&g_wcs);
  for (i = 0; i < g_nwev; i++) { free(g_wev[i].d.key); free(g_wev[i].d.name); free(g_wev[i].d.o); free(g_wev[i].d.n); }
  free(g_wev);
  g_wev = NULL; g_nwev = g_capwev = 0;
  LeaveCriticalSection(&g_wcs);
}

static void WatchRefreshUI(void)
{
  WBuf b = { 0 };
  wchar_t tmp[700];
  int i;
  if (!g_hWatchInfo) return;
  wb_adds(&b, g_obs.running ? L"Состояние: ИДЁТ НАБЛЮДЕНИЕ\r\n" : L"Состояние: наблюдение не запущено\r\n");
  if (g_obs.start[0]) { _snwprintf(tmp, 699, L"Начато: %ls\r\n", g_obs.start); wb_adds(&b, tmp); }
  if (g_obs.running) { _snwprintf(tmp, 699, L"Областей слежения: %d\r\n", g_obs.n); wb_adds(&b, tmp); }
  _snwprintf(tmp, 699, L"Событий за сеанс: %lu\r\n", g_obs.total);
  wb_adds(&b, tmp);
  if (g_obs.log[0]) { wb_adds(&b, L"\r\nЛог наблюдения:\r\n"); wb_adds(&b, g_obs.log); wb_adds(&b, L"\r\n"); }
  if (g_obs.running) {
    wb_adds(&b, L"\r\nСлежу за:\r\n");
    for (i = 0; i < g_obs.n && i < 40; i++) { wb_adds(&b, g_obs.w[i].root); wb_adds(&b, g_obs.w[i].hkey ? L"\r\n" : L"   (нет доступа или ветки нет, повторяю попытки)\r\n"); }
    if (g_obs.n > 40) wb_adds(&b, L"...\r\n");
  } else {
    wb_adds(&b, L"\r\nКак пользоваться: нажмите «Начать наблюдение», выберите ветки (из закладок, целые разделы или свои). "
                L"Затем изменяйте систему как обычно: каждое изменение в этих ветках появится в списке справа и в логе.\r\n\r\n"
                L"Windows сообщает «в ветке что-то изменилось», но не называет процесс, поэтому программа пересчитывает ветку и показывает разницу. "
                L"Для целых разделов это тяжело: выбирайте конкретные ветки там, где возможно.");
  }
  SetWindowTextW(g_hWatchInfo, b.p ? b.p : L"");
  free(b.p);
  if (g_tab == 3) {
    wchar_t cnt[100];
    _snwprintf(cnt, 99, L"Событий в списке: %d", g_nwev);
    SetWindowTextW(g_hCount, cnt);
    EnableWindow(g_hBtnJump, !g_obs.running);
    EnableWindow(g_hBtnJumpNew, g_obs.running);
    EnableWindow(g_hBtnX[1], g_obs.log[0] != 0);
  }
  if (g_tab == 4) ProtRefreshUI();
}

static LRESULT WatchLvNotify(NMHDR *nh, LPARAM lp)
{
  if (nh->code == LVN_GETDISPINFOW) {
    NMLVDISPINFOW *di = (NMLVDISPINFOW *)lp;
    int i = di->item.iItem;
    EnterCriticalSection(&g_wcs);
    if ((di->item.mask & LVIF_TEXT) && i >= 0 && i < g_nwev) {
      WItem *w = &g_wev[i];
      switch (di->item.iSubItem) {
        case 0: di->item.pszText = w->time; break;
        case 1: di->item.pszText = (LPWSTR)KindLabel(w->d.kind); break;
        case 2: di->item.pszText = w->d.key; break;
        case 3: di->item.pszText = w->d.name ? (w->d.name[0] ? w->d.name : (LPWSTR)L"(по умолчанию)") : (LPWSTR)L""; break;
        case 4: di->item.pszText = w->d.o ? w->d.o : (LPWSTR)L""; break;
        case 5: di->item.pszText = w->d.n ? w->d.n : (LPWSTR)L""; break;
      }
    }
    LeaveCriticalSection(&g_wcs);
  }
  return 0;
}

static void WatchViewRefresh(void)
{
  ListView_SetItemCountEx(g_hWatchLV, g_nwev, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
  if (g_nwev > 0) ListView_EnsureVisible(g_hWatchLV, g_nwev - 1, FALSE);
  InvalidateRect(g_hWatchLV, NULL, FALSE);
  WatchRefreshUI();
}

static void ActWatchStart(void)
{
  TargetList tl;
  SnapDlg d;
  Scope sc;
  int bad = 0;
  if (g_obs.running) { MessageBoxW(g_hMain, L"Наблюдение уже идёт. Сначала остановите его.", APP_NAME, MB_ICONINFORMATION); return; }
  memset(&tl, 0, sizeof tl);
  CollectTargets(&tl);
  memset(&d, 0, sizeof d);
  d.nBook = tl.n;
  d.mode = 1;
  if (!RunSnapDlg(&d)) { free(tl.t); free(d.manual); return; }
  memset(&sc, 0, sizeof sc);
  if (d.useBook) { int i; for (i = 0; i < tl.n; i++) ScAddRoot(&sc, tl.t[i].path); }
  if (d.hklm) ScAddRoot(&sc, L"HKEY_LOCAL_MACHINE");
  if (d.hkcu) ScAddRoot(&sc, L"HKEY_CURRENT_USER");
  if (d.hku) ScAddRoot(&sc, L"HKEY_USERS");
  AddManualRoots(&sc, d.manual ? d.manual : L"", &bad);
  free(d.manual);
  free(tl.t);
  ScNormalizeRoots(&sc);
  if (sc.nroots == 0) {
    MessageBoxW(g_hMain, bad ? L"Не удалось распознать ни одной ветки." : L"Не выбрано ни одной ветки для наблюдения.", APP_NAME, MB_ICONINFORMATION);
    ScFree(&sc);
    return;
  }
  if (d.hklm || d.hkcu || d.hku) {
    if (MessageBoxW(g_hMain, L"Наблюдение за целым разделом создаёт заметную нагрузку: при любом изменении в нём соответствующая часть реестра пересканируется. "
                    L"Для точечного слежения выберите конкретные ветки.\n\nПродолжить?", APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) { ScFree(&sc); return; }
  }
  if (d.useExcl) LoadExclusions(&sc);
  if (!ObsBegin(&sc, TRUE, NULL, FALSE)) MessageBoxW(g_hMain, L"Наблюдение не запущено (отменено или нет доступных веток).", APP_NAME, MB_ICONINFORMATION);
  ScFree(&sc);
  SwitchTab(3);
  WatchViewRefresh();
}

static void ActWatchStop(void)
{
  if (!g_obs.running) return;
  WatchStop();
  WatchViewRefresh();
}

static void ActWatchOpenLog(void)
{
  wchar_t arg[MAX_PATH + 24];
  if (!g_obs.log[0]) return;
  _snwprintf(arg, MAX_PATH + 23, L"\"%ls\"", g_obs.log);
  ShellExecuteW(g_hMain, L"open", L"notepad.exe", arg, NULL, SW_SHOWNORMAL);
}

/* ---- интерфейс вкладки «Защита» (держать от изменений) ---- */
static void ProtGuiAppend(const wchar_t *line)
{
  int len;
  if (!g_hProtLog) return;
  len = GetWindowTextLengthW(g_hProtLog);
  if (len > 1500000) { SetWindowTextW(g_hProtLog, L""); len = 0; }
  SendMessageW(g_hProtLog, EM_SETSEL, len, len);
  SendMessageW(g_hProtLog, EM_REPLACESEL, FALSE, (LPARAM)line);
  SendMessageW(g_hProtLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
}

static void ActProtAdd(void)
{
  TargetList tl;
  SnapDlg d;
  Scope sc;
  int i, bad = 0;
  memset(&tl, 0, sizeof tl);
  CollectTargets(&tl);
  memset(&d, 0, sizeof d);
  d.nBook = tl.n;
  d.mode = 2;
  if (!RunSnapDlg(&d)) { free(tl.t); free(d.manual); return; }
  memset(&sc, 0, sizeof sc);
  if (d.useBook) for (i = 0; i < tl.n; i++) ScAddRoot(&sc, tl.t[i].path);
  AddManualRoots(&sc, d.manual ? d.manual : L"", &bad);
  free(d.manual);
  free(tl.t);
  if (sc.nroots == 0) MessageBoxW(g_hMain, bad ? L"Не удалось распознать ни одной ветки." : L"Не выбрано ни одной ветки.", APP_NAME, MB_ICONINFORMATION);
  for (i = 0; i < sc.nroots; i++) ProtAddPath(sc.roots[i], d.useExcl);
  ScFree(&sc);
}

static void ActProtDel(void)
{
  int sel = -1, i, j;
  BOOL *del = (BOOL *)calloc(g_npitems + 1, sizeof(BOOL));
  while ((sel = ListView_GetNextItem(g_hProtLV, sel, LVNI_SELECTED)) >= 0) if (sel < g_npitems) del[sel] = TRUE;
  for (i = 0, j = 0; i < g_npitems; i++) if (!del[i]) g_pitems[j++] = g_pitems[i];
  if (j != g_npitems) { g_npitems = j; ProtSaveCfg(); ProtRefreshUI(); }
  free(del);
}

static void ActProtToggle(void)
{
  if (g_prot.running) {
    WatchStopSet(&g_prot);
    ProtRefreshUI();
    return;
  }
  if (g_npitems == 0) { MessageBoxW(g_hMain, L"Список пуст. Добавьте ветки кнопкой «Добавить…».", APP_NAME, MB_ICONINFORMATION); return; }
  if (MessageBoxW(g_hMain, L"Исходным состоянием будет считаться ТЕКУЩЕЕ состояние выбранных веток. При любом изменении или удалении программа "
                  L"вернёт его и запишет это в лог защиты.\n\nДля веток HKLM нужны права администратора (иначе вернуть не получится, "
                  L"и это будет записано в лог).\n\nЗапустить защиту?", APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  if (!ProtBegin(g_pitems, g_npitems, TRUE, FALSE)) MessageBoxW(g_hMain, L"Защита не запущена (отменена или нет доступных веток).", APP_NAME, MB_ICONINFORMATION);
  ProtRefreshUI();
}

static void ActProtOpenLog(void)
{
  wchar_t arg[MAX_PATH + 24];
  _snwprintf(arg, MAX_PATH + 23, L"\"%ls\"", g_protLog);
  ShellExecuteW(g_hMain, L"open", L"notepad.exe", arg, NULL, SW_SHOWNORMAL);
}

static void ActProtAutoToggle(void)
{
  g_protAuto = !g_protAuto;
  ProtSaveCfg();
  ProtRefreshUI();
}

/* ---- закладки без окна: модель для командной строки ---- */
typedef struct { int depth; BOOL folder; wchar_t *name, *path, *comment; } BEnt;
typedef struct { BEnt *e; int n, cap; } BModel;

static void BmInsert(BModel *m, int at, int depth, BOOL folder, const wchar_t *name, const wchar_t *path, const wchar_t *comment)
{
  BEnt *e;
  if (m->n == m->cap) { m->cap = m->cap ? m->cap * 2 : 32; m->e = (BEnt *)realloc(m->e, m->cap * sizeof(BEnt)); }
  memmove(&m->e[at + 1], &m->e[at], (size_t)(m->n - at) * sizeof(BEnt));
  e = &m->e[at];
  e->depth = depth; e->folder = folder;
  e->name = Dup(name); e->path = Dup(path); e->comment = Dup(comment);
  m->n++;
}

static void BmFree(BModel *m)
{
  int i;
  for (i = 0; i < m->n; i++) { free(m->e[i].name); free(m->e[i].path); free(m->e[i].comment); }
  free(m->e);
  memset(m, 0, sizeof *m);
}

static void BmParse(BModel *m, const wchar_t *text)
{
  const wchar_t *p = text;
  while (*p) {
    const wchar_t *eol = p, *f[5], *q, *fs;
    size_t fl[5];
    int nf = 0, i;
    while (*eol && *eol != L'\n') eol++;
    for (i = 0; i < 5; i++) { f[i] = NULL; fl[i] = 0; }
    fs = p;
    for (q = p; q <= eol; q++) {
      if (q == eol || (*q == L'|' && nf < 4)) {
        size_t l = (size_t)(q - fs);
        if (l && fs[l - 1] == L'\r') l--;
        f[nf] = fs; fl[nf] = l; nf++;
        fs = q + 1;
        if (q == eol) break;
      }
    }
    if (nf >= 3 && fl[0] == 1 && (f[0][0] == L'F' || f[0][0] == L'K')) {
      wchar_t dn[16], *name, *path, *cm;
      size_t dl = fl[1] < 15 ? fl[1] : 15;
      int depth;
      wcsncpy(dn, f[1], dl); dn[dl] = 0;
      depth = _wtoi(dn);
      name = Unesc(f[2], fl[2]);
      path = nf > 3 ? Unesc(f[3], fl[3]) : Dup(L"");
      cm = nf > 4 ? Unesc(f[4], fl[4]) : Dup(L"");
      BmInsert(m, m->n, depth, f[0][0] == L'F', name, path, cm);
      free(name); free(path); free(cm);
    }
    p = *eol ? eol + 1 : eol;
  }
}

static void BmText(BModel *m, WBuf *b)
{
  int i;
  wchar_t num[16];
  for (i = 0; i < m->n; i++) {
    wb_addc(b, m->e[i].folder ? L'F' : L'K');
    wb_addc(b, L'|');
    _snwprintf(num, 15, L"%d", m->e[i].depth);
    wb_adds(b, num);
    wb_addc(b, L'|'); wb_esc(b, m->e[i].name);
    wb_addc(b, L'|'); wb_esc(b, m->e[i].path);
    wb_addc(b, L'|'); wb_esc(b, m->e[i].comment);
    wb_adds(b, L"\r\n");
  }
}

static int BmSubtreeEnd(BModel *m, int idx)
{
  int d, i;
  if (idx < 0) return m->n;
  d = m->e[idx].depth;
  for (i = idx + 1; i < m->n && m->e[i].depth > d; i++) {}
  return i;
}

static int BmFindChildFolder(BModel *m, int parent, const wchar_t *name)
{
  int i, end = BmSubtreeEnd(m, parent), start = parent < 0 ? 0 : parent + 1, depth = parent < 0 ? 0 : m->e[parent].depth + 1;
  for (i = start; i < end; i++) if (m->e[i].depth == depth && m->e[i].folder && !_wcsicmp(m->e[i].name, name)) return i;
  return -1;
}

/* найти или создать цепочку папок «A/B/C»; возвращает индекс последней папки (-1 - корень) */
static int BmEnsureFolders(BModel *m, const wchar_t *folderPath)
{
  wchar_t buf[600], *tok, *ctx = NULL;
  int parent = -1;
  if (!folderPath || !folderPath[0]) return -1;
  wcsncpy(buf, folderPath, 599); buf[599] = 0;
  for (tok = wcstok(buf, L"/\\", &ctx); tok; tok = wcstok(NULL, L"/\\", &ctx)) {
    int idx;
    if (!tok[0]) continue;
    idx = BmFindChildFolder(m, parent, tok);
    if (idx < 0) {
      int at = parent < 0 ? m->n : BmSubtreeEnd(m, parent);
      BmInsert(m, at, parent < 0 ? 0 : m->e[parent].depth + 1, TRUE, tok, L"", L"");
      idx = at;
    }
    parent = idx;
  }
  return parent;
}

static void BmAddKey(BModel *m, const wchar_t *folderPath, const wchar_t *name, const wchar_t *path, const wchar_t *comment)
{
  int parent = BmEnsureFolders(m, folderPath);
  int at = parent < 0 ? m->n : BmSubtreeEnd(m, parent);
  BmInsert(m, at, parent < 0 ? 0 : m->e[parent].depth + 1, FALSE, name, path, comment);
}

static void BmLoad(BModel *m) { memset(m, 0, sizeof *m); BmParse(m, g_cfgBook ? g_cfgBook : L""); }

static BOOL BmSave(BModel *m)
{
  WBuf b = { 0 };
  BOOL ok;
  BmText(m, &b);
  ok = SaveConfigWith(b.p ? b.p : L"", TRUE);
  free(b.p);
  return ok;
}

/* добавление закладки в запущенном окне (команда передаётся из командной строки) */
static HTREEITEM FindChildFolderGui(HTREEITEM parent, const wchar_t *name)
{
  HTREEITEM it;
  wchar_t t[300];
  for (it = FirstChild(parent); it; it = TreeView_GetNextSibling(g_hTree, it)) {
    NodeData *nd = GetNode(it);
    if (!nd || !nd->folder) continue;
    GetItemTextW2(it, t, 300);
    if (!_wcsicmp(t, name)) return it;
  }
  return NULL;
}

static void GuiAddBookmark(const wchar_t *key, const wchar_t *name, const wchar_t *folder)
{
  HTREEITEM parent = TVI_ROOT, it;
  wchar_t buf[600], *tok, *ctx = NULL, nm[300], cm[200], ts[32];
  const wchar_t *sl = wcsrchr(key, L'\\');
  CommitEdits();
  if (folder && folder[0]) {
    wcsncpy(buf, folder, 599); buf[599] = 0;
    for (tok = wcstok(buf, L"/\\", &ctx); tok; tok = wcstok(NULL, L"/\\", &ctx)) {
      HTREEITEM f = FindChildFolderGui(parent, tok);
      if (!f) f = InsertNode(parent, TVI_LAST, tok, TRUE, L"", L"");
      parent = f;
    }
  }
  wcsncpy(nm, name && name[0] ? name : (sl ? sl + 1 : key), 255); nm[255] = 0;
  NowStr(ts, 32);
  _snwprintf(cm, 199, L"Добавлено из командной строки: %ls", ts);
  it = InsertNode(parent, TVI_LAST, nm, FALSE, key, cm);
  if (parent != TVI_ROOT) TreeView_Expand(g_hTree, parent, TVE_EXPAND);
  (void)it;
  MarkDirty();
}

/* ====================================================================
 *  команды командной строки (без окна)
 * ==================================================================== */
static int UsageErr(const wchar_t *msg)
{
  ConPrint(L"Ошибка: %ls\nСправка: KeyShelfRegJumper.exe /?\n", msg);
  return 2;
}

static void CliSplit(TargetList *tl, wchar_t **others, int *nother)
{
  int i;
  *nother = 0;
  for (i = 0; i < g_cli.npos; i++) {
    wchar_t norm[2048];
    if (NormalizeKey(g_cli.pos[i], norm, 2048)) tl_add(tl, norm);
    else others[(*nother)++] = g_cli.pos[i];
  }
}

static BOOL IsGuiRunning(void) { return FindWindowW(CLS_MAIN, NULL) != NULL; }

static int CliJumpKey(const wchar_t *key, BOOL newWin)
{
  JumpReq *r = (JumpReq *)calloc(1, sizeof *r);
  wchar_t norm[2048];
  if (!NormalizeKey(key, norm, 2048)) { free(r); return UsageErr(L"не удалось распознать ветку реестра."); }
  wcscpy(r->path, norm);
  r->newWin = newWin;
  if (!g_jumpMutex) g_jumpMutex = CreateMutexW(NULL, FALSE, NULL);
  ConPrint(L"Открываю regedit на ветке %ls (%ls)...\n", norm, newWin ? L"новое окно /m" : L"обычный режим");
  JumpThread(r);
  if (g_jumpRc == 0) { ConPrint(L"Готово.\n"); return 0; }
  ConPrint(L"Не удалось открыть regedit (код %d). Подробности в логе.\n", g_jumpRc);
  return 1;
}

static void PrintBookmarks(BModel *m)
{
  int i;
  for (i = 0; i < m->n; i++) {
    wchar_t indent[80];
    int k, pos = 0;
    for (k = 0; k < m->e[i].depth * 2 && pos < 70; k++) indent[pos++] = L' ';
    indent[pos] = 0;
    if (m->e[i].folder) ConPrint(L"%ls%ls/\n", indent, m->e[i].name);
    else ConPrint(L"%ls%ls  ->  %ls%ls%ls\n", indent, m->e[i].name, m->e[i].path, m->e[i].comment[0] ? L"   # " : L"", m->e[i].comment);
  }
  if (m->n == 0) ConPrint(L"Закладок нет.\n");
}

static int CliAdd(void)
{
  TargetList tl;
  wchar_t *others[64], cm[200], ts[32], nm[300];
  int nother, rc = 0;
  BModel m;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  if (tl.n < 1) { free(tl.t); return UsageErr(L"/add: укажите ветку реестра."); }
  NowStr(ts, 32);
  _snwprintf(cm, 199, L"Добавлено из командной строки: %ls", ts);
  if (IsGuiRunning()) {
    HWND w = FindWindowW(CLS_MAIN, NULL);
    wchar_t pl[3000];
    COPYDATASTRUCT cd;
    _snwprintf(pl, 2999, L"%ls\x01%ls\x01%ls", tl.t[0].path, nother > 0 ? others[0] : L"", nother > 1 ? others[1] : L"");
    pl[2999] = 0;
    cd.dwData = 2; cd.cbData = (DWORD)((wcslen(pl) + 1) * sizeof(wchar_t)); cd.lpData = pl;
    AllowSetForegroundWindow(ASFW_ANY);
    SendMessageW(w, WM_COPYDATA, 0, (LPARAM)&cd);
    ConPrint(L"Закладка передана в открытое окно программы: %ls\n", tl.t[0].path);
    free(tl.t);
    return 0;
  }
  {
    const wchar_t *sl = wcsrchr(tl.t[0].path, L'\\');
    wcsncpy(nm, nother > 0 && others[0][0] ? others[0] : (sl ? sl + 1 : tl.t[0].path), 255);
    nm[255] = 0;
  }
  BmLoad(&m);
  BmAddKey(&m, nother > 1 ? others[1] : L"", nm, tl.t[0].path, cm);
  if (BmSave(&m)) ConPrint(L"Закладка добавлена: %ls  ->  %ls\n", nm, tl.t[0].path);
  else { ConPrint(L"Не удалось записать конфигурацию %ls\n", g_cfgFile); rc = 1; }
  BmFree(&m);
  free(tl.t);
  return rc;
}

static int CliGoto(void)
{
  BModel m;
  int i, found = -1, rc;
  wchar_t norm[2048];
  if (g_cli.npos < 1) return UsageErr(L"/goto: укажите имя закладки или путь.");
  BmLoad(&m);
  for (i = 0; i < m.n && found < 0; i++) if (!m.e[i].folder && !_wcsicmp(m.e[i].name, g_cli.pos[0])) found = i;
  if (found < 0 && NormalizeKey(g_cli.pos[0], norm, 2048)) {
    for (i = 0; i < m.n && found < 0; i++) if (!m.e[i].folder && !_wcsicmp(m.e[i].path, norm)) found = i;
  }
  if (found < 0) {
    for (i = 0; i < m.n && found < 0; i++) if (!m.e[i].folder && StrStrIW(m.e[i].name, g_cli.pos[0])) found = i;
  }
  if (found < 0) { ConPrint(L"Закладка не найдена: %ls\n", g_cli.pos[0]); BmFree(&m); return 3; }
  ConPrint(L"Закладка: %ls\n", m.e[found].name);
  rc = CliJumpKey(m.e[found].path, g_cli.newWin);
  BmFree(&m);
  return rc;
}

static int CliList(void)
{
  BModel m;
  BmLoad(&m);
  PrintBookmarks(&m);
  BmFree(&m);
  return 0;
}

static void EnsureParentDir(const wchar_t *file)
{
  wchar_t d[MAX_PATH + 8], *sl;
  wcsncpy(d, file, MAX_PATH + 7); d[MAX_PATH + 7] = 0;
  sl = wcsrchr(d, L'\\');
  if (sl && sl > d) { *sl = 0; EnsureDir(d); }
}

/* выгрузка нескольких веток в один файл */
static int ExportKeysToFile(TargetList *tl, const wchar_t *file, wchar_t *msg, size_t cap)
{
  EnsureParentDir(file);
  if (tl->n == 1) return RunRegExport(tl->t[0].path, file, msg, cap);
  {
    WBuf all = { 0 };
    wchar_t tmp[MAX_PATH + 40], tdir[MAX_PATH], *text;
    int i, ok = 0, rc = 0;
    GetTempPathW(MAX_PATH, tdir);
    for (i = 0; i < tl->n; i++) {
      wchar_t em[400];
      SetBusy(L"Экспорт %d из %d: %ls", i + 1, tl->n, tl->t[i].path);
      _snwprintf(tmp, MAX_PATH + 39, L"%lskeyshelf_%lu_%d.reg", tdir, GetTickCount(), i);
      if (RunRegExport(tl->t[i].path, tmp, em, 400) == 0) { AppendRegBody(&all, tmp); ok++; }
      else { rc = 1; _snwprintf(msg, cap - 1, L"%ls: %ls", tl->t[i].path, em); }
      DeleteFileW(tmp);
    }
    if (ok == 0) { free(all.p); return rc ? rc : 1; }
    text = (wchar_t *)malloc(((all.p ? wcslen(all.p) : 0) + 64) * sizeof(wchar_t));
    wcscpy(text, L"Windows Registry Editor Version 5.00\r\n\r\n");
    if (all.p) wcscat(text, all.p);
    if (!WriteUtf16File(file, text)) rc = 1;
    free(text); free(all.p);
    return rc;
  }
}

static int CliExport(void)
{
  TargetList tl;
  wchar_t *others[64], msg[500];
  int nother, rc;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  if (tl.n < 1 || nother < 1) { free(tl.t); return UsageErr(L"/export: укажите ветку (или несколько) и имя файла .reg."); }
  msg[0] = 0;
  g_conProgress = g_conIsConsole;
  rc = ExportKeysToFile(&tl, others[0], msg, 500);
  ConProgressEnd();
  if (rc == 0) ConPrint(L"Сохранено: %ls (веток: %d)\n", others[0], tl.n);
  else ConPrint(L"Не удалось выгрузить: %ls\n", msg);
  free(tl.t);
  return rc == 0 ? 0 : 1;
}

static int CliSaveTo(void)
{
  TargetList tl;
  wchar_t *others[64];
  int nother, rc;
  SaveJob *job;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  if (tl.n < 1) { free(tl.t); return UsageErr(L"/saveto: укажите ветку (и, при желании, заметку)."); }
  EnsureDir(g_savedDir);
  job = (SaveJob *)calloc(1, sizeof *job);
  job->tl = &tl;
  wcsncpy(job->dir, g_savedDir, MAX_PATH - 1);
  MakeTimestamp(job->ts, 32);
  if (nother > 0) wcsncpy(job->note, others[0], 199);
  job->useTag = TRUE;
  g_conProgress = g_conIsConsole;
  InterlockedExchange(&g_cancel, 0);
  SaveWorker(job);
  ConProgressEnd();
  rc = job->fail == 0 ? 0 : 1;
  if (job->ok > 0) ConPrint(L"Сохранено файлов: %d. %ls%ls\n", job->ok, tl.n == 1 ? L"Файл: " : L"Первый файл: ", job->firstFile);
  if (job->fail > 0) ConPrint(L"Не удалось: %d.\n%ls\n", job->fail, job->firstErr);
  free(job);
  free(tl.t);
  return rc;
}

static int CliScript(BOOL vbs)
{
  TargetList tl;
  wchar_t *others[64], dir[MAX_PATH];
  int nother, i, mode = 1, made = 0, failed = 0;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  if (tl.n < 1) { free(tl.t); return UsageErr(vbs ? L"/vbs: укажите ветку." : L"/bat: укажите ветку."); }
  wcsncpy(dir, g_batDir, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
  for (i = 0; i < nother; i++) {
    if (!_wcsicmp(others[i], L"NO") || !_wcsicmp(others[i], L"normal")) mode = 0;
    else if (!_wcsicmp(others[i], L"NW") || !_wcsicmp(others[i], L"new")) mode = 1;
    else if (!_wcsicmp(others[i], L"BOTH") || !_wcsicmp(others[i], L"all")) mode = 2;
    else { wcsncpy(dir, others[i], MAX_PATH - 1); dir[MAX_PATH - 1] = 0; }
  }
  if (!EnsureDir(dir)) { free(tl.t); ConPrint(L"Не удалось создать папку: %ls\n", dir); return 1; }
  for (i = 0; i < tl.n; i++) {
    int md;
    for (md = 0; md < 2; md++) {
      wchar_t name[400], full[MAX_PATH + 16], *text;
      BOOL nw = (md == 1);
      BOOL ok;
      if (mode == 0 && nw) continue;
      if (mode == 1 && !nw) continue;
      MakeBatName(name, 400, 250 - (int)wcslen(dir) - 1 - 4, nw, tl.t[i].path);
      _snwprintf(full, MAX_PATH + 15, L"%ls\\%ls", dir, name);
      if (vbs) {
        wcscpy(full + wcslen(full) - 4, L".vbs");
        text = BuildVbs(tl.t[i].path, nw);
        ok = text && WriteUtf16File(full, text);
      } else {
        text = BuildBat(tl.t[i].path, nw);
        ok = text && WriteUtf8File(full, text);
      }
      free(text);
      if (ok) { made++; ConPrint(L"Создан: %ls\n", full); } else { failed++; ConPrint(L"Не удалось создать: %ls\n", full); }
    }
  }
  free(tl.t);
  return failed ? 1 : 0;
}

/* ---- снимки ---- */
static void SnapEnumToArray(void)
{
  WIN32_FIND_DATAW fd;
  HANDLE h;
  wchar_t pat[MAX_PATH + 8], full[MAX_PATH + 24];
  int cap = 0;
  free(g_snaps);
  g_snaps = NULL;
  g_nsnaps = 0;
  _snwprintf(pat, MAX_PATH + 7, L"%ls\\*.ksn", g_snapDir);
  h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      SnapInfo si;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      _snwprintf(full, MAX_PATH + 23, L"%ls\\%ls", g_snapDir, fd.cFileName);
      if (!ReadSnapInfo(full, &si)) continue;
      if (g_nsnaps == cap) { cap = cap ? cap * 2 : 32; g_snaps = (SnapInfo *)realloc(g_snaps, cap * sizeof(SnapInfo)); }
      g_snaps[g_nsnaps++] = si;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  if (g_nsnaps > 1) qsort(g_snaps, g_nsnaps, sizeof(SnapInfo), QsCmpSnap);
}

/* снимок можно задать путём, именем файла в Snapshots или частью названия (берётся самый свежий) */
static BOOL ResolveSnap(const wchar_t *arg, wchar_t *out)
{
  wchar_t t[MAX_PATH + 24];
  int i;
  DWORD a = GetFileAttributesW(arg);
  if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) { wcsncpy(out, arg, MAX_PATH + 15); out[MAX_PATH + 15] = 0; return TRUE; }
  _snwprintf(t, MAX_PATH + 23, L"%ls\\%ls", g_snapDir, arg);
  a = GetFileAttributesW(t);
  if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) { wcsncpy(out, t, MAX_PATH + 15); out[MAX_PATH + 15] = 0; return TRUE; }
  _snwprintf(t, MAX_PATH + 23, L"%ls\\%ls.ksn", g_snapDir, arg);
  a = GetFileAttributesW(t);
  if (a != INVALID_FILE_ATTRIBUTES) { wcsncpy(out, t, MAX_PATH + 15); out[MAX_PATH + 15] = 0; return TRUE; }
  SnapEnumToArray();
  for (i = g_nsnaps - 1; i >= 0; i--) {
    if (StrStrIW(g_snaps[i].name, arg) || StrStrIW(PathFindFileNameW(g_snaps[i].file), arg)) { wcscpy(out, g_snaps[i].file); return TRUE; }
  }
  return FALSE;
}

static int CliSnapCmd(void)
{
  ScanJob *sj;
  int i, bad = 0;
  wchar_t *file = NULL, *name = NULL;
  if (g_cli.npos < 3) return UsageErr(L"/snap: нужны файл снимка, название и хотя бы одна ветка.");
  file = g_cli.pos[0]; name = g_cli.pos[1];
  sj = (ScanJob *)calloc(1, sizeof *sj);
  for (i = 2; i < g_cli.npos; i++) {
    wchar_t norm[2048];
    if (NormalizeKey(g_cli.pos[i], norm, 2048)) ScAddRoot(&sj->scope, norm); else bad++;
  }
  ScNormalizeRoots(&sj->scope);
  if (sj->scope.nroots == 0) { ScFree(&sj->scope); free(sj); return UsageErr(L"/snap: не удалось распознать ни одной ветки."); }
  if (g_cli.excl) LoadExclusions(&sj->scope);
  if (!wcschr(file, L'\\') && !wcschr(file, L'/') && !wcschr(file, L':')) {
    EnsureDir(g_snapDir);
    _snwprintf(sj->file, MAX_PATH + 15, L"%ls\\%ls%ls", g_snapDir, file, wcsrchr(file, L'.') ? L"" : L".ksn");
  } else { wcsncpy(sj->file, file, MAX_PATH + 15); EnsureParentDir(sj->file); }
  wcsncpy(sj->name, name, 199);
  g_conProgress = g_conIsConsole;
  InterlockedExchange(&g_cancel, 0);
  DoScanToFile(sj);
  ConProgressEnd();
  if (sj->ok) { ConPrint(L"Снимок готов: %ls\nНазвание: %ls\nДата и время: %ls\nВеток: %lu, значений: %lu, без доступа: %lu\n", sj->file, sj->name, sj->start, sj->keys, sj->vals, sj->errs); if (bad) ConPrint(L"Нераспознанных веток пропущено: %d\n", bad); }
  else ConPrint(L"%ls\n", sj->canceled ? L"Снимок отменён." : L"Не удалось создать снимок.");
  i = sj->ok ? 0 : 1;
  ScFree(&sj->scope);
  free(sj);
  return i;
}

static int CliSnapsList(void)
{
  int i;
  SnapEnumToArray();
  if (g_nsnaps == 0) { ConPrint(L"Снимков нет (папка %ls).\n", g_snapDir); return 0; }
  for (i = 0; i < g_nsnaps; i++)
    ConPrint(L"%ls  %-30ls веток %lu, значений %lu%ls\n  %ls\n", g_snaps[i].start, g_snaps[i].name, g_snaps[i].keys, g_snaps[i].vals, g_snaps[i].complete ? L"" : L"  (не завершён)", g_snaps[i].file);
  return 0;
}

static int CliCmpCmd(void)
{
  CmpJob *j;
  wchar_t a[MAX_PATH + 16], b[MAX_PATH + 16], ts[32], when[32], rp[MAX_PATH + 16];
  BOOL cur;
  if (g_cli.npos < 2) return UsageErr(L"/cmp: укажите два снимка (или снимок и current) и, при желании, файл отчёта.");
  if (!ResolveSnap(g_cli.pos[0], a)) { ConPrint(L"Снимок не найден: %ls\n", g_cli.pos[0]); return 3; }
  cur = !_wcsicmp(g_cli.pos[1], L"current");
  if (!cur && !ResolveSnap(g_cli.pos[1], b)) { ConPrint(L"Снимок не найден: %ls\n", g_cli.pos[1]); return 3; }
  j = (CmpJob *)calloc(1, sizeof *j);
  wcscpy(j->fileA, a);
  if (cur) {
    j->vsCurrent = TRUE;
    EnsureDir(g_snapDir);
    _snwprintf(j->tmpFile, MAX_PATH + 15, L"%ls\\_current.tmp", g_snapDir);
    wcscpy(j->curName, L"Текущее состояние реестра");
    wcscpy(g_cmpB, L"Текущее состояние реестра");
  } else wcscpy(j->fileB, b);
  g_conProgress = g_conIsConsole;
  InterlockedExchange(&g_cancel, 0);
  FreeDiff();
  CompareWorker(j);
  ConProgressEnd();
  if (j->vsCurrent) DeleteFileW(j->tmpFile);
  if (j->result != 1) { ConPrint(L"%ls\n", j->result == 2 ? L"Сравнение отменено." : (j->err[0] ? j->err : L"Не удалось выполнить сравнение.")); free(j); return 1; }
  NowStr(when, 32);
  wcscpy(g_cmpWhen, when);
  BuildSummary(g_cmpSummary, 900);
  if (g_cli.npos >= 3) wcsncpy(rp, g_cli.pos[2], MAX_PATH + 15);
  else {
    wchar_t nm2[600];
    EnsureDir(g_repDir);
    MakeTimestamp(ts, 32);
    MakeRegName(nm2, 600, 150, ts, L"сравнение", L"");
    _snwprintf(rp, MAX_PATH + 15, L"%ls\\%ls.txt", g_repDir, nm2);
    UniquePath(rp, MAX_PATH + 16);
  }
  EnsureParentDir(rp);
  ConPrint(L"Было:  %ls (%ls)\nСтало: %ls (%ls)\n%ls\n", g_cmpA, g_cmpTimeA, g_cmpB, g_cmpTimeB, g_cmpSummary);
  if (WriteReport(rp)) ConPrint(L"Отчёт: %ls\n", rp);
  else { ConPrint(L"Не удалось записать отчёт: %ls\n", rp); free(j); return 1; }
  free(j);
  return 0;
}

/* ---- наблюдение и защита в консоли ---- */
static void CliWait(int secs)
{
  DWORD t0 = GetTickCount();
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  while (!g_ctrlc) {
    Sleep(200);
    if (secs > 0 && GetTickCount() - t0 >= (DWORD)secs * 1000) break;
  }
}

static int CliWatchCmd(void)
{
  Scope sc;
  int i, secs = 0, first = 0;
  memset(&sc, 0, sizeof sc);
  if (g_cli.npos > 0) {
    BOOL num = g_cli.pos[0][0] != 0;
    const wchar_t *c;
    for (c = g_cli.pos[0]; *c; c++) if (*c < L'0' || *c > L'9') num = FALSE;
    if (num) { secs = _wtoi(g_cli.pos[0]); first = 1; }
  }
  for (i = first; i < g_cli.npos; i++) { wchar_t norm[2048]; if (NormalizeKey(g_cli.pos[i], norm, 2048)) ScAddRoot(&sc, norm); }
  ScNormalizeRoots(&sc);
  if (sc.nroots == 0) { ScFree(&sc); return UsageErr(L"/watch: укажите хотя бы одну ветку."); }
  if (g_cli.excl) LoadExclusions(&sc);
  g_conProgress = g_conIsConsole;
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  if (!ObsBegin(&sc, FALSE, g_cli.out[0] ? g_cli.out : NULL, TRUE)) { ConProgressEnd(); ConPrint(L"Наблюдение не запущено.\n"); ScFree(&sc); return 1; }
  ConProgressEnd();
  ScFree(&sc);
  ConPrint(L"Наблюдение запущено (областей: %d). %ls\nЛог: %ls\n\n", g_obs.n, secs > 0 ? L"Остановится само." : L"Остановить: Ctrl+C.", g_obs.log);
  CliWait(secs);
  WatchStop();
  ConPrint(L"Наблюдение остановлено. Событий: %lu\n", g_obs.total);
  return 0;
}

static int CliHoldCmd(void)
{
  TargetList tl;
  wchar_t *others[64];
  int nother, i, n = 0, rc;
  ProtItem *items;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  items = (ProtItem *)calloc((tl.n > 0 ? tl.n : (g_npitems > 0 ? g_npitems : 1)) + 1, sizeof(ProtItem));
  if (tl.n > 0) {
    for (i = 0; i < tl.n; i++) { wcscpy(items[n].path, tl.t[i].path); items[n].rec = !g_cli.norec; n++; }
  } else {
    for (i = 0; i < g_npitems; i++) items[n++] = g_pitems[i];
  }
  free(tl.t);
  if (n == 0) { free(items); return UsageErr(L"/hold: укажите ветки или добавьте их в список защиты в программе."); }
  g_conProgress = g_conIsConsole;
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  if (!ProtBegin(items, n, FALSE, TRUE)) { ConProgressEnd(); ConPrint(L"Защита не запущена.\n"); free(items); return 1; }
  ConProgressEnd();
  ConPrint(L"Защита от изменений включена (веток: %d). Остановить: Ctrl+C.\nЛог: %ls\nДля веток HKLM нужны права администратора.\n\n", g_prot.n, g_protLog);
  CliWait(0);
  WatchStopSet(&g_prot);
  ConPrint(L"Защита остановлена. Событий: %lu\n", g_prot.total);
  free(items);
  rc = 0;
  return rc;
}

/* ---- правка в редакторе, импорт, очистка ---- */
static int CliOpenCmd(int kind)
{
  TargetList tl;
  wchar_t *others[64], editF[MAX_PATH + 16], origF[MAX_PATH + 16], err[300], exe[MAX_PATH] = L"";
  int nother;
  HANDLE hp = NULL;
  memset(&tl, 0, sizeof tl);
  CliSplit(&tl, others, &nother);
  if (kind == 1) {
    if (nother < 1) { free(tl.t); return UsageErr(L"/open-with: укажите путь к редактору и ветку."); }
    wcsncpy(exe, others[0], MAX_PATH - 1);
  }
  if (tl.n < 1) { free(tl.t); return UsageErr(L"/open: укажите ветку."); }
  g_conProgress = g_conIsConsole;
  if (!EditExport(&tl, FALSE, editF, origF, err)) { ConProgressEnd(); ConPrint(L"Не удалось выгрузить ветку: %ls\n", err); free(tl.t); return 1; }
  ConProgressEnd();
  free(tl.t);
  ConPrint(L"Файл для правки: %ls\nОригинал (не меняется): %ls\n", editF, origF);
  if (!LaunchEditor(kind, exe, editF, &hp)) { ConPrint(L"Не удалось запустить редактор.\n"); return 1; }
  if (g_cli.apply) {
    if (!hp) { ConPrint(L"Редактор не позволяет дождаться закрытия. Внести правки позже: /import \"%ls\"\n", editF); return 0; }
    ConPrint(L"Жду закрытия редактора...\n");
    WaitForSingleObject(hp, INFINITE);
    CloseHandle(hp);
    if (ConAsk(L"Внести изменённый файл в реестр?")) {
      wchar_t em[500];
      int rc = RunRegImport(editF, em, 500);
      if (rc == 0) ConPrint(L"Внесено в реестр. Оригинал для отката: %ls\n", origF);
      else { ConPrint(L"Не удалось внести (код %d): %ls\nНужны права администратора?\n", rc, em); return 1; }
    } else ConPrint(L"Не внесено. Позже: /import \"%ls\"\n", editF);
  } else {
    if (hp) CloseHandle(hp);
    ConPrint(L"Внести правки в реестр после редактирования: /import \"%ls\"\nОткатить к оригиналу: /import \"%ls\"\n", editF, origF);
  }
  return 0;
}

static int CliImportCmd(void)
{
  wchar_t em[500];
  int rc;
  if (g_cli.npos < 1) return UsageErr(L"/import: укажите .reg-файл.");
  if (GetFileAttributesW(g_cli.pos[0]) == INVALID_FILE_ATTRIBUTES) { ConPrint(L"Файл не найден: %ls\n", g_cli.pos[0]); return 3; }
  {
    wchar_t q[MAX_PATH + 80];
    _snwprintf(q, MAX_PATH + 79, L"Внести %ls в реестр? Значения в указанных ветках изменятся или будут созданы.", g_cli.pos[0]);
    if (!ConAsk(q)) { ConPrint(L"Отменено.\n"); return 1; }
  }
  rc = RunRegImport(g_cli.pos[0], em, 500);
  if (rc == 0) { ConPrint(L"Внесено в реестр.\n"); return 0; }
  ConPrint(L"Не удалось внести (код %d): %ls\n", rc, em);
  return 1;
}

static int CliLogCmd(void)
{
  BOOL on;
  if (g_cli.npos < 1) return UsageErr(L"/log: укажите on или off.");
  if (!_wcsicmp(g_cli.pos[0], L"on") || !_wcsicmp(g_cli.pos[0], L"1")) on = TRUE;
  else if (!_wcsicmp(g_cli.pos[0], L"off") || !_wcsicmp(g_cli.pos[0], L"0")) on = FALSE;
  else return UsageErr(L"/log: допустимые значения on и off.");
  if (IsGuiRunning()) { ConPrint(L"Программа открыта в окне: измените лог в «Настройки», иначе при закрытии окна настройка перезапишется.\n"); return 1; }
  CfgSet(L"Options", L"LogEnabled", on ? L"1" : L"0");
  if (!SaveConfigWith(g_cfgBook ? g_cfgBook : L"", g_cfgHasBook)) { ConPrint(L"Не удалось записать конфигурацию.\n"); return 1; }
  ConPrint(L"Лог %ls.\n", on ? L"включён" : L"выключен");
  return 0;
}

static int CliAddRegCmd(void)
{
  int f, rc = 0;
  BModel m;
  if (g_cli.npos < 1) return UsageErr(L"/addreg: укажите .reg-файл.");
  if (IsGuiRunning()) { ConPrint(L"Программа открыта в окне: используйте /addreg без /nogui или закройте окно.\n"); return 1; }
  BmLoad(&m);
  for (f = 0; f < g_cli.npos; f++) {
    KeyList kl;
    wchar_t folder[300], cm[300];
    int i;
    memset(&kl, 0, sizeof kl);
    if (!ParseRegFile(g_cli.pos[f], &kl) || kl.n == 0) { ConPrint(L"В файле нет веток: %ls\n", g_cli.pos[f]); kl_free(&kl); rc = 1; continue; }
    wcsncpy(folder, PathFindFileNameW(g_cli.pos[f]), 299); folder[299] = 0;
    _snwprintf(cm, 299, L"Из файла: %ls", folder);
    PathRemoveExtensionW(folder);
    for (i = 0; i < kl.n; i++) {
      wchar_t *sl = wcsrchr(kl.keys[i], L'\\');
      BmAddKey(&m, folder, sl ? sl + 1 : kl.keys[i], kl.keys[i], cm);
    }
    ConPrint(L"Из %ls добавлено веток: %d (папка «%ls»)\n", g_cli.pos[f], kl.n, folder);
    kl_free(&kl);
  }
  if (!BmSave(&m)) { ConPrint(L"Не удалось записать конфигурацию.\n"); rc = 1; }
  BmFree(&m);
  return rc;
}

static int CliProbeCmd(void)
{
  wchar_t path[2048];
  int r = GetRegeditKey(path);
  if (r == 0) { ConPrint(L"Ветка в regedit: %ls\n", path); return 0; }
  ConPrint(L"%ls\n", r == 1 ? L"regedit не запущен." : L"Не удалось прочитать ветку из окна regedit (возможно, он запущен от администратора). Подробности в логе.");
  return r;
}

/* главный диспетчер консольных команд */
static int RunCli(void)
{
  if (g_cli.bad) { wchar_t m[300]; _snwprintf(m, 299, L"неизвестный или конфликтующий ключ: %ls", g_cli.badArg); return UsageErr(m); }
  if (g_cli.cmd == C_NONE) { ShowHelp(); return 1; }
  switch (g_cli.cmd) {
    case C_HELP: ShowHelp(); return 0;
    case C_VERSION: ConPrint(L"Key Shelf Reg Jumper %ls\n", APP_VER); return 0;
    case C_JUMP: case C_JUMPM:
      if (g_cli.npos < 1) return UsageErr(L"укажите ветку реестра.");
      return CliJumpKey(g_cli.pos[0], g_cli.cmd == C_JUMPM);
    case C_ADD: return CliAdd();
    case C_GOTO: return CliGoto();
    case C_LIST: return CliList();
    case C_EXPORT: return CliExport();
    case C_SAVETO: return CliSaveTo();
    case C_BAT: return CliScript(FALSE);
    case C_VBS: return CliScript(TRUE);
    case C_SNAP: return CliSnapCmd();
    case C_CMP: return CliCmpCmd();
    case C_SNAPS: return CliSnapsList();
    case C_WATCH: return CliWatchCmd();
    case C_HOLD: return CliHoldCmd();
    case C_OPEN: return CliOpenCmd(0);
    case C_OPENNP: return CliOpenCmd(2);
    case C_OPENWITH: return CliOpenCmd(1);
    case C_IMPORT: return CliImportCmd();
    case C_CLEANTMP: ClearTmpDir(); ConPrint(L"Временная папка очищена: %ls\n", g_tmpDir); return 0;
    case C_LOG: return CliLogCmd();
    case C_ADDREG: return CliAddRegCmd();
    case C_PROBE: return CliProbeCmd();
    default: break;
  }
  return 2;
}
