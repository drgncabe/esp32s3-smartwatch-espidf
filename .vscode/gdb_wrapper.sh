#!/bin/bash
# GDB wrapper script to work around VS Code architecture detection issues
# This script loads the ELF file via GDB commands instead of letting VS Code parse it

GDB_PATH="$HOME/.platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb"
GDBINIT="$1/.gdbinit"
ELF_FILE="$1/.pio/build/esp32-s3-devkitc-1-debug/firmware.elf"

# Start GDB with the init file and load the ELF
exec "$GDB_PATH" -x "$GDBINIT" -iex "file $ELF_FILE" "$@"

