#!/bin/bash
# Buduje frontend React i tworzy obraz SPIFFS do wgrania na ESP32
# Użycie: ./build_spiffs.sh [flash]

set -e

echo "=== Building React frontend ==="
cd frontend
npm install
npm run build
cd ..

echo ""
echo "=== Frontend built to spiffs_image/ ==="
ls -la spiffs_image/

echo ""
echo "=== Creating SPIFFS image ==="
# Rozmiar partycji SPIFFS: 0x6F0000 = 7274496 bytes
# Offset partycji: 0x910000
SPIFFS_SIZE=7274496
SPIFFS_OFFSET=0x910000

# Użyj mkspiffs (dostępny z ESP-IDF) lub spiffsgen.py
if command -v mkspiffs &> /dev/null; then
    mkspiffs -c spiffs_image -s $SPIFFS_SIZE -b 4096 -p 256 spiffs.bin
    echo "Created spiffs.bin with mkspiffs"
else
    python3 $IDF_PATH/components/spiffs/spiffsgen.py \
        $SPIFFS_SIZE spiffs_image spiffs.bin
    echo "Created spiffs.bin with spiffsgen.py"
fi

echo ""
echo "SPIFFS image: $(du -h spiffs.bin | cut -f1)"

if [ "$1" == "flash" ]; then
    echo ""
    echo "=== Flashing SPIFFS ==="
    python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
        --chip esp32s3 \
        --baud 921600 \
        write_flash $SPIFFS_OFFSET spiffs.bin
    echo "SPIFFS flashed!"
fi

echo ""
echo "Done! To flash manually:"
echo "  esptool.py --chip esp32s3 --baud 921600 write_flash $SPIFFS_OFFSET spiffs.bin"
