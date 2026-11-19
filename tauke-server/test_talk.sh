#!/usr/bin/env bash
# End-to-end test of the deployed Tauke server with a WAV/PCM file (16 kHz mono s16).
# Usage: ./test_talk.sh sample.wav   (token is read from the gitignored sketch secrets.h; never printed)
set -euo pipefail
IN=$(realpath "${1:?wav or pcm file}"); OUT=$(realpath -m "${2:-reply.wav}")
cd "$(dirname "$0")"
S=../sketch-ota/puck_ota/secrets.h
URL=$(grep -oP 'TAUKE_URL "\K[^"]+' $S); TOK=$(grep -oP 'TAUKE_TOKEN "\K[^"]+' $S)

code=$(curl -s -o /tmp/tk_reply.pcm -D /tmp/tk_h.txt -w '%{http_code}' -H "Authorization: Bearer $TOK" \
  -H "X-Device-Id: BOX-TEST" -H "Content-Type: audio/pcm" --data-binary @"$IN" "$URL")
echo "HTTP $code"
py='import sys,urllib.parse as u;[print(k+":",u.unquote(v.strip())) for k,v in (l.split(":",1) for l in open("/tmp/tk_h.txt") if l.lower().startswith(("x-transcript","x-reply","x-error","x-timing")))]'
python3 -c "$py"
[ "$code" = 200 ] && ffmpeg -loglevel error -y -f s16le -ar 16000 -ac 1 -i /tmp/tk_reply.pcm "$OUT" && echo "reply audio -> $OUT ($(stat -c%s /tmp/tk_reply.pcm) bytes PCM)"
