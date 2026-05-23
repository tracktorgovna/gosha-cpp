#!/bin/bash
# Cross-compile Windows .exe on macOS
# Требует: brew install mingw-w64

set -e

if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
    echo "MinGW не найден. Установи: brew install mingw-w64"
    exit 1
fi

echo "[1/3] Ресурсы..."
x86_64-w64-mingw32-windres app.rc -O coff -o app_res.o --codepage 65001

echo "[2/3] SQLite..."
x86_64-w64-mingw32-gcc -O2 -c sqlite3.c -o sqlite3.o

echo "[3/3] Компиляция..."
x86_64-w64-mingw32-g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -I. \
    main.cpp app_res.o sqlite3.o \
    -luser32 -lgdi32 -lcomctl32 -lshell32 \
    -mwindows -o cloud_audit.exe

rm -f app_res.o sqlite3.o
echo "Готово: cloud_audit.exe — скинь на Windows и запусти."
