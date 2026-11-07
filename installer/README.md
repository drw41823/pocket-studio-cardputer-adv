# Offline Windows installer

This installer wrapper is distributed under GPLv2; see LICENSE. It uses esptool 5.4.0 and pyserial. It makes no network requests, checks the bundled image SHA256, and requires explicit backup confirmation before writing.

Build on Windows x64 with Python, esptool==5.4.0, pyserial and PyInstaller. Provide a clean merged image named pocket-studio-v0.25-cardputer-adv-full.bin and update SHA256 in install.py if rebuilding firmware. Use PyInstaller --onefile --windowed --collect-all esptool --collect-all serial --add-data to bundle the image in the archive root. Run the executable with --self-test --report FILE to validate image and ESP32-S3 stub without accessing hardware.
