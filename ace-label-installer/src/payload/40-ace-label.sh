# /etc/hooks/lmd.d/40-ace-label.sh - sourced by /etc/init.d/S90lmd (busybox sh), like paxx12's own hooks.
# Loads libace-label.so into the stock touch-screen program so it shows "ACE" under each toolhead
# icon that multiACE drives from a connected ACE. Read-only; never changes /usr/bin/gui.
#
# Off switches (any one):
#   - file /home/lava/printer_data/config/extended/ace-label.disabled  (Fluidd/Mainsail: config/extended)
#   - [components] gui: helixscreen in extended2.cfg (the add-on is only for the stock screen)
#   - two screen crashes with the add-on loaded since boot (/tmp/ace-label.crashes)
if [ "$1" = start ]; then
	ACE_LABEL_LIB=/usr/local/lib/libace-label.so
	[ -r /tmp/ace-label/libace-label.so ] && [ ! -r "$ACE_LABEL_LIB" ] && ACE_LABEL_LIB=/tmp/ace-label/libace-label.so
	ACE_LABEL_GUI=$(/usr/local/bin/extended-config.py get /oem/printer_data/config/extended/extended2.cfg components gui snapmaker 2>/dev/null)
	ACE_LABEL_CRASHES=$(cat /tmp/ace-label.crashes 2>/dev/null || echo 0)
	if [ -e /home/lava/printer_data/config/extended/ace-label.disabled ]; then
		logger -t ace-label "disabled by config/extended/ace-label.disabled"
	elif [ "$ACE_LABEL_GUI" = helixscreen ]; then
		logger -t ace-label "HelixScreen selected - not loading"
	elif [ "${ACE_LABEL_CRASHES:-0}" -ge 2 ] 2>/dev/null; then
		logger -t ace-label "screen crashed $ACE_LABEL_CRASHES times with the add-on - off until reboot"
	elif [ -r "$ACE_LABEL_LIB" ]; then
		export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$ACE_LABEL_LIB"
		logger -t ace-label "loading $ACE_LABEL_LIB into the screen program"
	fi
	unset ACE_LABEL_LIB ACE_LABEL_GUI ACE_LABEL_CRASHES
fi
