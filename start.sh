#!/bin/bash
# MonitorCrebirth - Quick Start
# Usage: ./start.sh [daemon|tui|both|stop|status]

DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/.local-deps/lib:$LD_LIBRARY_PATH"
DAEMON="$DIR/build/backend/mc-daemon"
TUI="$DIR/build/clients/tui/mc-tui"
PIDFILE="$HOME/.monitorcrebirth/mc-daemon.pid"

# Ensure config exists
if [ ! -f "$HOME/.monitorcrebirth/config.toml" ]; then
    mkdir -p "$HOME/.monitorcrebirth"
    cp "$DIR/config_example/config.toml" "$HOME/.monitorcrebirth/config.toml"
    echo "Config installed to ~/.monitorcrebirth/config.toml"
fi

# Build if needed
if [ ! -f "$DAEMON" ] || [ ! -f "$TUI" ]; then
    echo "Building..."
    mkdir -p "$DIR/build"
    cd "$DIR/build"
    PKG_CONFIG_PATH="$DIR/.local-deps/lib/pkgconfig:$PKG_CONFIG_PATH" cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -j$(nproc) > /dev/null 2>&1
    echo "Build complete."
fi

case "${1:-both}" in
    daemon)
        echo "Starting mc-daemon (foreground)..."
        echo "  HTTP API: http://localhost:8420"
        echo "  Press Ctrl+C to stop"
        exec "$DAEMON"
        ;;
    tui)
        echo "Connecting to mc-daemon..."
        exec "$TUI" "${@:2}"
        ;;
    both)
        echo "Starting mc-daemon in background..."
        "$DAEMON" > "$HOME/.monitorcrebirth/daemon.log" 2>&1 &
        echo $! > "$PIDFILE"
        sleep 2
        if kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "Daemon started (PID $(cat "$PIDFILE"))"
            echo "Launching TUI..."
            "$TUI"
            # When TUI exits, stop daemon
            echo "Stopping daemon..."
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
        else
            echo "ERROR: Daemon failed to start. Check ~/.monitorcrebirth/daemon.log"
            exit 1
        fi
        ;;
    stop)
        if [ -f "$PIDFILE" ]; then
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
            echo "Daemon stopped."
        else
            pkill -f mc-daemon 2>/dev/null
            echo "Daemon stopped."
        fi
        ;;
    status)
        if curl -s http://localhost:8420/api/v1/status 2>/dev/null | python3 -m json.tool; then
            echo ""
        else
            echo "Daemon is not running."
        fi
        ;;
    *)
        echo "Usage: $0 [daemon|tui|both|stop|status]"
        echo ""
        echo "  daemon  - Run daemon in foreground"
        echo "  tui     - Launch TUI client only"
        echo "  both    - Start daemon + TUI (default)"
        echo "  stop    - Stop background daemon"
        echo "  status  - Show daemon status"
        ;;
esac
