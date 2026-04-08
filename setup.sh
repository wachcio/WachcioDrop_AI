#!/bin/bash
# Jednorazowa konfiguracja projektu: submoduły git + środowisko
set -e

echo "=== Inicjalizacja submodułów ==="
git submodule update --init --recursive

echo ""
echo "=== Sprawdzenie środowiska ESP-IDF ==="
if [ -z "$IDF_PATH" ]; then
    echo "UWAGA: IDF_PATH nie jest ustawione."
    echo "Uruchom: source ~/esp/esp-idf/export.sh"
    exit 1
fi
echo "IDF_PATH = $IDF_PATH"
idf.py --version

echo ""
echo "=== Instalacja Node.js dependencies (frontend) ==="
cd frontend
npm install
cd ..

echo ""
echo "=== Konfiguracja projektu ==="
idf.py set-target esp32s3

echo ""
echo "OK! Możesz teraz uruchomić:"
echo "  idf.py build          # zbuduj firmware"
echo "  idf.py flash monitor  # wgraj i monitoruj"
echo "  ./build_spiffs.sh flash  # zbuduj i wgraj frontend"
