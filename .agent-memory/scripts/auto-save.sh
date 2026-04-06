#!/bin/bash
cd "/Users/cagdasgulay/Desktop/CYBER CLOCK"
MEMORY_DIR=".agent-memory"
ACTIVE_FILE="$MEMORY_DIR/.active-agent"

while true; do
  sleep 1800
  if [ -f "$ACTIVE_FILE" ]; then
    AGENT=$(cat "$ACTIVE_FILE")
    bash "$MEMORY_DIR/scripts/agent-hub.sh" save "$AGENT" "Auto-save (30dk)" 2>/dev/null
  fi
done
