#!/bin/bash
# MonitorCrebirth - Quick Start
# Usage: ./start.sh [daemon|tui|both|stop|status]

DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON="$DIR/build/backend/mc-daemon"
TUI="$DIR/build/clients/tui/mc-tui"
CONFIG="$HOME/.monitorcrebirth/config.toml"
PIDFILE="$HOME/.monitorcrebirth/mc-daemon.pid"
LOGFILE="$HOME/.monitorcrebirth/daemon.log"

# Ensure config exists
if [ ! -f "$CONFIG" ]; then
    mkdir -p "$HOME/.monitorcrebirth"
    cp "$DIR/config_example/config.toml" "$CONFIG"
    echo "Config installed to $CONFIG"
fi

# Build if needed
build_if_needed() {
    local need_build=0
    if [ ! -f "$DAEMON" ]; then
        echo "Backend not built, building..."
        mkdir -p "$DIR/backend/build"
        cd "$DIR/backend/build"
        cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
        make -j$(nproc) 2>&1
        cd "$DIR"
        need_build=1
    fi
    if [ ! -f "$TUI" ]; then
        echo "TUI not built, building..."
        mkdir -p "$DIR/build"
        cd "$DIR/build"
        cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
        make -j$(nproc) 2>&1
        cd "$DIR"
        need_build=1
    fi
    if [ "$need_build" -eq 1 ]; then
        echo "Build complete."
    fi
}

# Kill all existing daemons
kill_daemons() {
    local killed=0
    # Kill by pidfile
    if [ -f "$PIDFILE" ]; then
        local pid
        pid=$(cat "$PIDFILE")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            killed=1
        fi
        rm -f "$PIDFILE"
    fi
    # Kill any remaining mc-daemon processes
    local pids
    pids=$(pgrep -f mc-daemon 2>/dev/null || true)
    if [ -n "$pids" ]; then
        kill $pids 2>/dev/null || true
        killed=1
    fi
    if [ "$killed" -eq 1 ]; then
        echo "Stopped existing daemon(s)."
        sleep 1
    fi
}

case "${1:-both}" in
    daemon)
        kill_daemons
        build_if_needed
        "$DAEMON" --config "$CONFIG" > "$LOGFILE" 2>&1 &
        echo $! > "$PIDFILE"
        sleep 2
        if kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "Daemon started (PID $(cat "$PIDFILE"))"
            echo "  HTTP API: http://localhost:8420"
            echo "  Logs:     $LOGFILE"
            echo "  Stop:     ./start.sh stop"
        else
            echo "ERROR: Daemon failed to start. Check $LOGFILE"
            exit 1
        fi
        ;;
    tui)
        build_if_needed
        exec "$TUI" "${@:2}"
        ;;
    both)
        kill_daemons
        build_if_needed
        echo "Starting daemon..."
        "$DAEMON" --config "$CONFIG" > "$LOGFILE" 2>&1 &
        echo $! > "$PIDFILE"
        sleep 2
        if kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "Daemon started (PID $(cat "$PIDFILE"), log: $LOGFILE)"
            echo "Launching TUI..."
            "$TUI" "${@:2}"
            # When TUI exits, stop daemon
            echo "Stopping daemon..."
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
        else
            echo "ERROR: Daemon failed to start. Check $LOGFILE"
            exit 1
        fi
        ;;
    stop)
        kill_daemons
        ;;
    status)
        if curl -s http://localhost:8420/api/v1/status 2>/dev/null | python3 -m json.tool; then
            echo ""
        else
            echo "Daemon is not running."
        fi
        ;;
    *)
        echo "Usage: $0 [daemon|tui|both|stop|status] [OPTIONS]"
        echo ""
        echo "  daemon   - Run daemon in foreground"
        echo "  tui      - Launch TUI client only"
        echo "  both     - Start daemon + TUI (default)"
        echo "  stop     - Stop background daemon"
        echo "  status   - Show daemon status"
        echo ""
        echo "Options (passed to TUI):"
        echo "  --light  Light terminal theme"
        echo "  --dark   Dark terminal theme (default)"
        echo ""
        echo "Example: $0 both --light"
        ;;
esac
