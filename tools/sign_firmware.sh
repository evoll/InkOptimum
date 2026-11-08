#!/usr/bin/env bash
# Signs a firmware image: writes <image>.sig (e.g. firmware.bin.sig), the 64-byte raw Ed25519 signature of
# "INKOPT-FW-v1\n" + SHA-256(<image>) (45 bytes). Verified by lib/FirmwareSignature with the public key
# the firmware is built with.
#
#   tools/sign_firmware.sh .pio/build/<env>/firmware.bin
#
# Key: $INKOPT_SIGNING_KEY_FILE, else ~/.inkoptimum/signing-ed25519.pem (PEM, never committed).
# Make one with:  openssl genpkey -algorithm ed25519 -out ~/.inkoptimum/signing-ed25519.pem
# Its public half as a C array:  tools/sign_firmware.sh --public-key
# OpenSSL 3 or newer is required (pkeyutl -rawin).
set -euo pipefail

die() { echo "sign_firmware: $*" >&2; exit 1; }

[[ $# -eq 1 ]] || die "usage: $0 <firmware.bin> | --public-key"

KEY="${INKOPT_SIGNING_KEY_FILE:-$HOME/.inkoptimum/signing-ed25519.pem}"
[[ -r "$KEY" ]] || die "signing key not readable: $KEY (set INKOPT_SIGNING_KEY_FILE)"

if [[ -x /opt/homebrew/bin/openssl ]]; then
  OPENSSL=/opt/homebrew/bin/openssl
else
  OPENSSL="$(command -v openssl)" || die "openssl not found"
fi
"$OPENSSL" version | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.' || die "OpenSSL 3+ required, got: $("$OPENSSL" version)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [[ "$1" == "--public-key" ]]; then
  # The raw 32-byte public key is the last 32 bytes of the DER SubjectPublicKeyInfo.
  "$OPENSSL" pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | sed -e 's/ \([0-9a-f][0-9a-f]\)/0x\1, /g' -e 's/, $//'
  exit 0
fi
BIN="$1"
[[ -f "$BIN" ]] || die "no such file: $BIN"

# Message = 13-byte prefix + 32-byte binary SHA-256 of the whole image.
printf 'INKOPT-FW-v1\n' > "$TMP/msg"
"$OPENSSL" dgst -sha256 -binary "$BIN" >> "$TMP/msg"
[[ "$(wc -c < "$TMP/msg" | tr -d ' ')" == 45 ]] || die "message is not 45 bytes"

"$OPENSSL" pkeyutl -sign -inkey "$KEY" -rawin -in "$TMP/msg" -out "$TMP/sig"
[[ "$(wc -c < "$TMP/sig" | tr -d ' ')" == 64 ]] || die "signature is not 64 bytes"

# Self-check against the key's own public half before publishing.
"$OPENSSL" pkey -in "$KEY" -pubout -out "$TMP/pub.pem"
"$OPENSSL" pkeyutl -verify -pubin -inkey "$TMP/pub.pem" -rawin -in "$TMP/msg" -sigfile "$TMP/sig" >/dev/null ||
  die "signature does not verify"

mv "$TMP/sig" "$BIN.sig"
echo "signed: $BIN.sig ($(head -c 8 "$BIN.sig" | od -An -v -tx1 | tr -d " \n")…)"
