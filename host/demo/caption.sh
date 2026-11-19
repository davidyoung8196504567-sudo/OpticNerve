#!/bin/sh
# Shows a caption in a video's text overlay, for steps that are not a test (the tests write their own captions):
#
#   ON_CAPTION_FILE=demo-out/caption.txt sh host/demo/caption.sh "Starting the agent on the Pi"
#
# Writes the text to ON_CAPTION_FILE, which an OBS Text source reads from. The text goes to a file beside it first
# and is moved in, so the overlay never shows a half-written line. With no text, the caption is cleared.
[ -n "$ON_CAPTION_FILE" ] || { echo "caption.sh: set ON_CAPTION_FILE" >&2; exit 1; }
printf '%s' "$*" > "$ON_CAPTION_FILE.tmp" && mv -f "$ON_CAPTION_FILE.tmp" "$ON_CAPTION_FILE"
