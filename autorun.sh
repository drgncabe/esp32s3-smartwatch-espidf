echo "Building and uploading to device..."
pio run -e esp-smartwatch -t upload
pio device monitor -b 115200

## Or specific port:
# pio run -t upload --upload-port /dev/cu.usbmodemXXXX
# pio device list

