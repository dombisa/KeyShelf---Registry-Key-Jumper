#!/bin/sh
# Кросс-компиляция под Windows из Linux (Debian/Ubuntu):
#   sudo apt install gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 binutils-mingw-w64
set -e
cd "$(dirname "$0")/src"
LIBS="-lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -ladvapi32 -luser32 -lgdi32 -lole32"
x86_64-w64-mingw32-windres -O coff res.rc -o res64.o
i686-w64-mingw32-windres   -O coff res.rc -o res32.o
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -municode -mwindows -static -s -o ../KeyShelf_x64.exe keyshelf.c res64.o $LIBS
i686-w64-mingw32-gcc   -O2 -Wall -Wextra -municode -mwindows -static -s -o ../KeyShelf_x86.exe keyshelf.c res32.o $LIBS
rm -f res64.o res32.o
echo "OK: KeyShelf_x64.exe, KeyShelf_x86.exe"
