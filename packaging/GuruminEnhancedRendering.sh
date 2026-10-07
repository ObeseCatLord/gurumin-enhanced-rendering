#!/bin/sh
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
no_terminal=0
if [ "${1-}" = --no-terminal ]; then
    no_terminal=1
    shift
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf '%s\n' 'Python 3.9 or newer is required. Install your distribution’s python3 package, then run this patch again.' >&2
    exit 1
fi
if [ "$no_terminal" = 0 ] && [ ! -t 0 ] && { [ -n "${DISPLAY-}" ] || [ -n "${WAYLAND_DISPLAY-}" ]; }; then
    # Desktop launch and dropped files get the same interactive console as a
    # terminal launch. Paths stay positional arguments, never shell source.
    terminal_code='package_dir=$1; shift; python3 "$package_dir/tools/patch.py" "$@"; result=$?; printf "\nPress Enter to close.\n"; read -r answer; exit "$result"'
    for terminal in x-terminal-emulator konsole xterm gnome-terminal; do
        if command -v "$terminal" >/dev/null 2>&1; then
            if [ "$terminal" = gnome-terminal ]; then
                exec "$terminal" --wait -- sh -c "$terminal_code" patch "$package_dir" "$@"
            fi
            exec "$terminal" -e sh -c "$terminal_code" patch "$package_dir" "$@"
        fi
    done
fi
exec python3 "$package_dir/tools/patch.py" "$@"
