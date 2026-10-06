#!/bin/sh
# Renders cheatsheet.html to cheatsheet.png (2400 x 1500) with headless
# Chrome/Chromium. CHROME may name the binary; it needs the network once,
# for the font.
CHROME=${CHROME:-chromium}
cd "$(dirname "$0")"
"$CHROME" --headless --disable-gpu --hide-scrollbars --force-device-scale-factor=2 \
    --window-size=1200,750 --virtual-time-budget=5000 \
    --screenshot="$PWD/cheatsheet.png" "file://$PWD/cheatsheet.html"
