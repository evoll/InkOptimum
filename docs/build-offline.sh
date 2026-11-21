#!/bin/sh
# Offline InkOptimum build (the PlatformIO registry is blocked by the proxy).
# Usage: /home/claude/build.sh x3x4-release|x4pro-release   (log: /tmp/build_<env>.log, last line FINISHED)
# Never run plain `pio run` here: it removes the libdeps it cannot re-download. platformio.ini stays untouched.
set -e
cd /home/claude/evoll/inkoptimum
python3 - <<'PY'
s=open('platformio.ini').read()
rep={'bblanchon/ArduinoJson @ 7.4.3':'ArduinoJson=symlink:///home/claude/libs/ArduinoJson\n  SdFat=symlink:///home/claude/libs/SdFat',
'ricmoo/QRCode @ 0.0.1':'QRCode=symlink:///home/claude/libs/QRCode',
'bitbank2/PNGdec @ 1.1.7':'PNGdec=symlink:///home/claude/libs/PNGdec',
'https://github.com/bitbank2/JPEGDEC.git#86282979224c8a32fd51e091ed5a35b0c699a52b':'JPEGDEC=symlink:///home/claude/libs/JPEGDEC',
'links2004/WebSockets @ 2.7.3':'WebSockets=symlink:///home/claude/libs/WebSockets',
'wolfssl/Arduino-wolfSSL @ 5.8.4':'wolfSSL=symlink:///home/claude/libs/wolfSSL'}
for k,v in rep.items():
    assert k in s,k; s=s.replace(k,v)
open('platformio.offline.ini','w').write(s)
PY
pio run -c platformio.offline.ini -e "$1" > /tmp/build_$1.log 2>&1 || true
rm -f platformio.offline.ini
rm -rf scripts/__pycache__
echo FINISHED >> /tmp/build_$1.log
