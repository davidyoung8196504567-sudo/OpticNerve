#!/bin/sh
# Puts a desktop shortcut for each PoC command on this user's desktop and in the applications list (from there they
# can be pinned to the dock). Run it again after moving this folder; -remove deletes the shortcuts.
#
#   sh host/poc/install-shortcuts.sh [-remove]
POC=$(cd "$(dirname "$0")" && pwd)
case $1 in '' | -remove) ;; *) sed -n '5p' "$0" >&2; exit 2 ;; esac
case $POC in *[\"\\\`\$%\ ]*) echo "move this repository to a folder without spaces or \" \\ \` \$ %: $POC" >&2; exit 1 ;; esac
APPS=$HOME/.local/share/applications
DESK=$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")
# file name | title | command | terminal | icon
LIST="oculix-ide|OculiX IDE|ide.sh|false|applications-development
oculix-viewer|OculiX VNC viewer|viewer.sh|true|preferences-desktop-remote-desktop
pi-ssh|SSH to the Pi|ssh-pi.sh|true|utilities-terminal
demo-panel|Start the demo panel|demo-panel.sh start|true|video-display
oculix-run-test|Run an OculiX test|run-test.sh|true|media-playback-start
sut-install|Install and start the agent on the Pi|sut.sh install|true|system-software-install"
remove=$1
mkdir -p "$APPS" "$DESK"
echo "$LIST" | while IFS='|' read -r file title cmd term icon; do
	for d in "$APPS" "$DESK"; do
		f=$d/opticnerve-$file.desktop
		if [ "$remove" = -remove ]; then rm -f "$f"; continue; fi
		hold=; [ "$term" = true ] && hold="env POC_HOLD=1 "
		printf '%s\n' '[Desktop Entry]' 'Type=Application' "Name=$title" \
			"Exec=${hold}sh $POC/$cmd" "Terminal=$term" "Icon=$icon" 'Categories=Development;' > "$f"
		chmod 755 "$f"
		gio set "$f" metadata::trusted true 2>/dev/null
	done
	[ "$remove" = -remove ] && echo "removed: $title" || echo "shortcut: $title"
done
