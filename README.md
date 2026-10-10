# KeyShelf — Registry Key Jumper

**RU** · [EN below](#english)

Небольшая портативная программа для Windows 7–10: закладки на ветки реестра с папками и комментариями,
быстрый переход в `regedit`, сохранение веток в `.reg` с историей и экспорт в `.bat`.
Один exe-файл, без установки и без внешних библиотек (чистый Win32 API, C).

![screenshot](docs/screenshot.png)

## Возможности

- **Закладки с папками.** Вложенные папки на любом уровне, комментарий к каждой папке и ветке,
  переименование (`F2`), перетаскивание мышью (внутрь папки, между элементами, на верхний уровень).
- **Переход в regedit** двойным кликом / `Enter`. Можно открывать в **отдельном окне** (`regedit.exe /m`),
  чтобы работать с несколькими ветками одновременно. Отдельные команды «просто открыть regedit»
  (в месте, где он был закрыт) обычным способом или в новом окне.
- **Добавление из `.reg`-файлов**: через меню (`Ctrl+O`), перетаскиванием файла на окно или через пункт
  «Добавить в KeyShelf» в контекстном меню `.reg` (включается в «Настройки», права администратора не нужны).
- **Сохранение веток в `.reg`** (`Ctrl+S`) через штатный `reg export`: в архив программы (папка `Saved`
  рядом с exe, с иерархией папок) или в любую другую папку. В имени файла — дата и время и ваша заметка.
  Вкладка **«Сохранённые»**: просмотр, переименование, перенос, удаление (в корзину), открытие в Блокноте,
  «Восстановить в реестр» (с подтверждением).
- **Групповые операции.** Отметьте несколько веток флажками (флажок у папки отмечает всё внутри) и сохраните их
  отдельными файлами или одним общим `.reg`.
- **Экспорт в `.bat`**: скрипт открывает regedit на нужной ветке. Имя файла: `NO-HKLM_SOFTWARE_...bat`
  (обычное открытие) или `NW-HKLM_SOFTWARE_...bat` (новое окно, `/m`).

## Скачать

Готовые exe-файлы — в разделе [Releases](../../releases): `KeyShelf_x64.exe` (64-битная Windows),
`KeyShelf_x86.exe` (32-битная). Файлы не подписаны цифровой подписью, поэтому SmartScreen или антивирус
могут показать предупреждение. Исходный код открыт, можно собрать самостоятельно.

## Сборка

Релизные exe собираются автоматически в GitHub Actions (`.github/workflows/build.yml`): каждый push
собирает проект, а тег вида `v0.2` создаёт релиз с exe-файлами и контрольными суммами.

Локально из Linux (Debian/Ubuntu):

```sh
sudo apt install gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 binutils-mingw-w64
./build.sh
```

## Где хранятся данные

Рядом с exe: `keyshelf.ksl` (закладки), `keyshelf.ini` (настройки), `keyshelf.log` (лог), папки `Saved` и `Bat`.
Если папка программы недоступна для записи — в `%APPDATA%\KeyShelf`.

## Состояние проекта

Версия 0.2. Разработка велась кросс-компиляцией под Linux, интерфейс проверялся в Wine; на реальных
Windows 7–10 проверено ограниченно, возможны ошибки. Сообщения об ошибках приветствуются
(приложите последние строки `keyshelf.log`).

## Лицензия

MIT, см. [LICENSE](LICENSE).

---

## English

A small portable Windows 7–10 tool: bookmarks for registry keys with folders and comments, quick jump
into `regedit` (optionally in a separate window via `regedit.exe /m`), saving keys to `.reg` files
with history, and export to `.bat` launchers. Single exe, no installer, no external libraries (pure Win32 API, C).

**Features:** nested bookmark folders with comments, rename and drag & drop; jump to a key in regedit
(same or new window); add keys from `.reg` files (menu, drag & drop, or Explorer context menu);
save keys through `reg export` into an archive (`Saved` folder next to the exe) or any folder, with date/time
and a custom note in the file name; archive tab with browse/rename/move/delete/restore; checkbox-based group
save (separate files or one combined `.reg`); `.bat` export named `NO-HKLM_...bat` (normal open) /
`NW-HKLM_...bat` (new window).

**Download:** see [Releases](../../releases) (`KeyShelf_x64.exe`, `KeyShelf_x86.exe`). The executables are
unsigned, so SmartScreen/antivirus may warn. **Build:** `./build.sh` with mingw-w64 (see above).
**Status:** v0.2, tested mostly under Wine; limited testing on real Windows. Licensed under MIT.
