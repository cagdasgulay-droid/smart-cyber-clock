#!/bin/bash
# ╔══════════════════════════════════════════════════════════════╗
# ║  AGENT HUB - VS Code AI Agent Manager                       ║
# ║  Manage multiple AI agents with persistent memory            ║
# ║  Version: 1.0.0                                              ║
# ╚══════════════════════════════════════════════════════════════╝

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMORY_DIR=".agent-memory"
SESSIONS_DIR="$MEMORY_DIR/sessions"
AGENTS_DIR="$MEMORY_DIR/agents"
CONTEXT_DIR="$MEMORY_DIR/context"
CONFIG_FILE="$MEMORY_DIR/config.json"
ACTIVE_FILE="$MEMORY_DIR/.active-agent"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# Icons
ICON_ROBOT="🤖"
ICON_BRAIN="🧠"
ICON_SWITCH="🔄"
ICON_SAVE="💾"
ICON_LIST="📋"
ICON_PLAY="▶️"
ICON_STOP="⏹️"
ICON_CLOCK="🕐"
ICON_CHECK="✅"
ICON_WARN="⚠️"
ICON_FOLDER="📁"

# ─────────────────────────────────────────────
# INIT: Setup .agent-memory in current project
# ─────────────────────────────────────────────
cmd_init() {
    echo -e "${BOLD}${CYAN}${ICON_ROBOT} Agent Hub - Initializing...${NC}"
    echo ""

    mkdir -p "$SESSIONS_DIR" "$AGENTS_DIR" "$CONTEXT_DIR" "$MEMORY_DIR/scripts"

    # Create config if not exists
    if [ ! -f "$CONFIG_FILE" ]; then
        cat > "$CONFIG_FILE" << 'CONF'
{
  "version": "1.0.0",
  "project_name": "",
  "default_agent": "claude-code",
  "auto_save_sessions": true,
  "max_session_history": 50,
  "context_auto_load": true,
  "agents": {}
}
CONF
        # Set project name from directory
        local project_name=$(basename "$(pwd)")
        python3 -c "
import json
with open('$CONFIG_FILE', 'r') as f:
    cfg = json.load(f)
cfg['project_name'] = '$project_name'
with open('$CONFIG_FILE', 'w') as f:
    json.dump(cfg, f, indent=2)
"
    fi

    # Create .gitignore for sensitive data
    if [ ! -f "$MEMORY_DIR/.gitignore" ]; then
        cat > "$MEMORY_DIR/.gitignore" << 'GI'
# Ignore API keys and secrets
*.secret
*.key
.env
# Keep everything else tracked
!.gitignore
GI
    fi

    # Register default agents
    _register_default_agents

    echo -e "${ICON_CHECK} ${GREEN}Initialized in: $(pwd)${NC}"
    echo -e "${DIM}Memory dir: $MEMORY_DIR/${NC}"
    echo ""
    cmd_agents
}

# ─────────────────────────────────────────────
# AGENTS: List all registered agents
# ─────────────────────────────────────────────
cmd_agents() {
    echo -e "${BOLD}${CYAN}${ICON_LIST} Registered Agents${NC}"
    echo -e "${DIM}─────────────────────────────────────${NC}"

    local active=""
    [ -f "$ACTIVE_FILE" ] && active=$(cat "$ACTIVE_FILE")

    for agent_file in "$AGENTS_DIR"/*.json; do
        [ -f "$agent_file" ] || continue
        local name=$(python3 -c "import json; d=json.load(open('$agent_file')); print(d.get('name','?'))")
        local type=$(python3 -c "import json; d=json.load(open('$agent_file')); print(d.get('type','?'))")
        local cmd=$(python3 -c "import json; d=json.load(open('$agent_file')); print(d.get('launch_cmd','?'))")
        local sessions=$(ls "$SESSIONS_DIR"/${name}_*.json 2>/dev/null | wc -l)

        local marker=""
        [ "$name" = "$active" ] && marker="${GREEN} ● ACTIVE${NC}"

        echo -e "  ${ICON_ROBOT} ${BOLD}$name${NC}$marker"
        echo -e "     Type: ${CYAN}$type${NC} | Sessions: ${YELLOW}$sessions${NC}"
        echo -e "     Cmd:  ${DIM}$cmd${NC}"
        echo ""
    done
}

# ─────────────────────────────────────────────
# LAUNCH: Start an agent with memory context
# ─────────────────────────────────────────────
cmd_launch() {
    local agent_name="${1:-}"
    if [ -z "$agent_name" ]; then
        echo -e "${ICON_WARN} ${YELLOW}Usage: agent-hub launch <agent-name>${NC}"
        echo -e "${DIM}Available agents:${NC}"
        _list_agent_names
        return 1
    fi

    local agent_file="$AGENTS_DIR/${agent_name}.json"
    if [ ! -f "$agent_file" ]; then
        echo -e "${RED}Agent '$agent_name' not found.${NC}"
        return 1
    fi

    # Load last session context
    local context_file="$CONTEXT_DIR/${agent_name}_context.md"
    _build_context "$agent_name"

    # Mark as active
    echo "$agent_name" > "$ACTIVE_FILE"

    local launch_cmd=$(python3 -c "import json; print(json.load(open('$agent_file'))['launch_cmd'])")
    local type=$(python3 -c "import json; print(json.load(open('$agent_file'))['type'])")

    echo -e "${ICON_PLAY} ${GREEN}Launching ${BOLD}$agent_name${NC}${GREEN}...${NC}"
    echo -e "${ICON_BRAIN} ${DIM}Context loaded from last session${NC}"
    echo ""

    if [ "$type" = "md-agent" ]; then
        echo -e "${CYAN}─── MD Agent Instructions ───${NC}"
        local md_file=$(python3 -c "import json; print(json.load(open('$agent_file')).get('md_file',''))")
        if [ -f "$md_file" ]; then
            head -20 "$md_file"
            echo -e "${DIM}... (full instructions in $md_file)${NC}"
        fi
        echo ""
        echo -e "${YELLOW}Copy the context below and paste into your AI chat:${NC}"
        echo -e "${DIM}────────────────────────────────────${NC}"
        [ -f "$context_file" ] && cat "$context_file"
        echo -e "${DIM}────────────────────────────────────${NC}"
    elif [ "$type" = "cli" ] || [ "$type" = "terminal" ]; then
        # Show context summary before launching
        if [ -f "$context_file" ]; then
            echo -e "${ICON_BRAIN} ${CYAN}Session Context:${NC}"
            echo -e "${DIM}────────────────────────────────────${NC}"
            head -30 "$context_file"
            echo -e "${DIM}────────────────────────────────────${NC}"
            echo ""
        fi
        echo -e "${YELLOW}Launching terminal agent...${NC}"
        eval "$launch_cmd"
    else
        echo -e "${YELLOW}Launch command: ${BOLD}$launch_cmd${NC}"
        echo -e "${DIM}(Open VS Code command palette or run in terminal)${NC}"
    fi
}

# ─────────────────────────────────────────────
# SAVE: Save current session
# ─────────────────────────────────────────────
cmd_save() {
    local agent_name="${1:-}"
    local summary="${2:-}"

    if [ -z "$agent_name" ]; then
        [ -f "$ACTIVE_FILE" ] && agent_name=$(cat "$ACTIVE_FILE")
    fi

    if [ -z "$agent_name" ]; then
        echo -e "${ICON_WARN} ${YELLOW}Usage: agent-hub save <agent-name> [summary]${NC}"
        return 1
    fi

    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local session_file="$SESSIONS_DIR/${agent_name}_${timestamp}.json"

    # Interactive summary if not provided
    if [ -z "$summary" ]; then
        echo -e "${ICON_SAVE} ${CYAN}Save session for ${BOLD}$agent_name${NC}"
        echo -e "${YELLOW}What did you work on? (brief summary):${NC}"
        read -r summary
    fi

    # Collect git diff info
    local git_changes=""
    if git rev-parse --is-inside-work-tree &>/dev/null; then
        git_changes=$(git diff --stat HEAD 2>/dev/null || echo "no changes")
    fi

    # Get list of modified files
    local modified_files=""
    if git rev-parse --is-inside-work-tree &>/dev/null; then
        modified_files=$(git diff --name-only HEAD 2>/dev/null | head -20 || echo "")
    fi

    python3 << PYEOF
import json
from datetime import datetime

session = {
    "agent": "$agent_name",
    "timestamp": "$timestamp",
    "date": datetime.now().isoformat(),
    "summary": """$summary""",
    "git_changes": """$git_changes""",
    "modified_files": """$modified_files""".strip().split("\n") if """$modified_files""".strip() else [],
    "status": "saved",
    "tags": [],
    "notes": ""
}

with open("$session_file", "w") as f:
    json.dump(session, f, indent=2, ensure_ascii=False)

print(f"Session saved: $session_file")
PYEOF

    echo -e "${ICON_CHECK} ${GREEN}Session saved!${NC}"
    echo -e "${DIM}File: $session_file${NC}"

    # Update context for next launch
    _update_context "$agent_name" "$summary" "$modified_files"
}

# ─────────────────────────────────────────────
# HISTORY: Show session history for an agent
# ─────────────────────────────────────────────
cmd_history() {
    local agent_name="${1:-}"
    local count="${2:-10}"

    if [ -z "$agent_name" ]; then
        echo -e "${ICON_WARN} ${YELLOW}Usage: agent-hub history <agent-name> [count]${NC}"
        _list_agent_names
        return 1
    fi

    echo -e "${BOLD}${CYAN}${ICON_CLOCK} Session History: $agent_name${NC}"
    echo -e "${DIM}─────────────────────────────────────${NC}"

    local files=($(ls -t "$SESSIONS_DIR"/${agent_name}_*.json 2>/dev/null | head -$count))

    if [ ${#files[@]} -eq 0 ]; then
        echo -e "${DIM}  No sessions found.${NC}"
        return
    fi

    for session_file in "${files[@]}"; do
        python3 << PYEOF
import json
with open("$session_file") as f:
    s = json.load(f)
date = s.get('date', '?')[:16]
summary = s.get('summary', 'No summary')
files_count = len(s.get('modified_files', []))
print(f"  📅 {date}")
print(f"     {summary}")
if files_count > 0:
    print(f"     Files changed: {files_count}")
print()
PYEOF
    done
}

# ─────────────────────────────────────────────
# CONTEXT: Show/edit current context for agent
# ─────────────────────────────────────────────
cmd_context() {
    local agent_name="${1:-}"

    if [ -z "$agent_name" ]; then
        [ -f "$ACTIVE_FILE" ] && agent_name=$(cat "$ACTIVE_FILE")
    fi

    if [ -z "$agent_name" ]; then
        echo -e "${ICON_WARN} ${YELLOW}Usage: agent-hub context <agent-name>${NC}"
        return 1
    fi

    local context_file="$CONTEXT_DIR/${agent_name}_context.md"

    if [ ! -f "$context_file" ]; then
        _build_context "$agent_name"
    fi

    echo -e "${BOLD}${CYAN}${ICON_BRAIN} Context: $agent_name${NC}"
    echo -e "${DIM}─────────────────────────────────────${NC}"

    if [ -f "$context_file" ]; then
        cat "$context_file"
    else
        echo -e "${DIM}No context yet. Launch the agent to start building context.${NC}"
    fi
}

# ─────────────────────────────────────────────
# SWITCH: Quick switch between agents
# ─────────────────────────────────────────────
cmd_switch() {
    local agent_name="${1:-}"

    if [ -z "$agent_name" ]; then
        echo -e "${ICON_SWITCH} ${CYAN}${BOLD}Quick Switch${NC}"
        echo ""
        local i=1
        local agents=()
        for agent_file in "$AGENTS_DIR"/*.json; do
            [ -f "$agent_file" ] || continue
            local name=$(python3 -c "import json; print(json.load(open('$agent_file'))['name'])")
            agents+=("$name")
            local active_mark=""
            [ -f "$ACTIVE_FILE" ] && [ "$(cat "$ACTIVE_FILE")" = "$name" ] && active_mark=" ${GREEN}●${NC}"
            echo -e "  ${BOLD}$i)${NC} $name$active_mark"
            ((i++))
        done
        echo ""
        echo -n -e "${YELLOW}Select agent [1-$((i-1))]: ${NC}"
        read -r choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#agents[@]}" ]; then
            agent_name="${agents[$((choice-1))]}"
        else
            echo -e "${RED}Invalid choice.${NC}"
            return 1
        fi
    fi

    # Save current agent's session if active
    if [ -f "$ACTIVE_FILE" ]; then
        local current=$(cat "$ACTIVE_FILE")
        if [ "$current" != "$agent_name" ]; then
            echo -e "${ICON_SAVE} ${DIM}Auto-saving session for $current...${NC}"
            cmd_save "$current" "Auto-save on switch to $agent_name"
        fi
    fi

    cmd_launch "$agent_name"
}

# ─────────────────────────────────────────────
# ADD: Register a new agent
# ─────────────────────────────────────────────
cmd_add() {
    local name="${1:-}"
    local type="${2:-}"
    local cmd="${3:-}"

    if [ -z "$name" ]; then
        echo -e "${ICON_ROBOT} ${CYAN}${BOLD}Add New Agent${NC}"
        echo ""
        echo -n -e "${YELLOW}Agent name: ${NC}"
        read -r name
        echo -e "${DIM}Types: cli, vscode-extension, md-agent, terminal${NC}"
        echo -n -e "${YELLOW}Agent type: ${NC}"
        read -r type
        echo -n -e "${YELLOW}Launch command: ${NC}"
        read -r cmd
    fi

    local md_file=""
    if [ "$type" = "md-agent" ]; then
        echo -n -e "${YELLOW}MD instruction file path: ${NC}"
        read -r md_file
    fi

    python3 << PYEOF
import json
agent = {
    "name": "$name",
    "type": "$type",
    "launch_cmd": "$cmd",
    "md_file": "$md_file",
    "created": __import__('datetime').datetime.now().isoformat(),
    "settings": {}
}
with open("$AGENTS_DIR/${name}.json", "w") as f:
    json.dump(agent, f, indent=2)
PYEOF

    echo -e "${ICON_CHECK} ${GREEN}Agent '$name' registered!${NC}"
}

# ─────────────────────────────────────────────
# STATUS: Overview of all agents and sessions
# ─────────────────────────────────────────────
cmd_status() {
    echo -e ""
    echo -e "${BOLD}${MAGENTA}╔══════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${MAGENTA}║  ${ICON_ROBOT} AGENT HUB STATUS                     ║${NC}"
    echo -e "${BOLD}${MAGENTA}╚══════════════════════════════════════════╝${NC}"
    echo ""

    local project=$(python3 -c "import json; print(json.load(open('$CONFIG_FILE')).get('project_name','?'))" 2>/dev/null || echo "unknown")
    echo -e "  ${ICON_FOLDER} Project: ${BOLD}$project${NC}"

    local active="none"
    [ -f "$ACTIVE_FILE" ] && active=$(cat "$ACTIVE_FILE")
    echo -e "  ${ICON_PLAY} Active:  ${GREEN}${BOLD}$active${NC}"
    echo ""

    local total_sessions=$(ls "$SESSIONS_DIR"/*.json 2>/dev/null | wc -l)
    local total_agents=$(ls "$AGENTS_DIR"/*.json 2>/dev/null | wc -l)
    echo -e "  Agents: ${CYAN}$total_agents${NC}  |  Total Sessions: ${YELLOW}$total_sessions${NC}"
    echo ""

    echo -e "${DIM}─────────────────────────────────────${NC}"
    echo -e "${BOLD}Recent Activity:${NC}"

    local recent_files=($(ls -t "$SESSIONS_DIR"/*.json 2>/dev/null | head -5))
    for sf in "${recent_files[@]}"; do
        [ -f "$sf" ] || continue
        python3 << PYEOF
import json
with open("$sf") as f:
    s = json.load(f)
agent = s.get('agent','?')
date = s.get('date','?')[:16]
summary = s.get('summary','')[:60]
print(f"  {date} | {agent:15s} | {summary}")
PYEOF
    done
    echo ""
}

# ─────────────────────────────────────────────
# RESUME: Resume last session of an agent
# ─────────────────────────────────────────────
cmd_resume() {
    local agent_name="${1:-}"

    if [ -z "$agent_name" ]; then
        [ -f "$ACTIVE_FILE" ] && agent_name=$(cat "$ACTIVE_FILE")
    fi

    if [ -z "$agent_name" ]; then
        echo -e "${ICON_WARN} ${YELLOW}Usage: agent-hub resume <agent-name>${NC}"
        return 1
    fi

    echo -e "${ICON_SWITCH} ${CYAN}Resuming last session for ${BOLD}$agent_name${NC}"

    # Build context from last session
    _build_context "$agent_name"

    cmd_launch "$agent_name"
}

# ═══════════════════════════════════════════
# INTERNAL FUNCTIONS
# ═══════════════════════════════════════════

_register_default_agents() {
    # Claude Code
    _create_agent_if_missing "claude-code" "cli" "claude" ""

    # GitHub Copilot
    _create_agent_if_missing "copilot" "vscode-extension" "code --goto ." ""

    # Cline / Roo Code
    _create_agent_if_missing "cline" "vscode-extension" "code --goto ." ""

    # Cursor-style (can be Cursor IDE or Continue.dev)
    _create_agent_if_missing "cursor" "terminal" "cursor ." ""

    echo -e "${DIM}Default agents registered. Add custom MD agents with: agent-hub add${NC}"
}

_create_agent_if_missing() {
    local name="$1" type="$2" cmd="$3" md="$4"
    local file="$AGENTS_DIR/${name}.json"
    [ -f "$file" ] && return

    python3 << PYEOF
import json, datetime
agent = {
    "name": "$name",
    "type": "$type",
    "launch_cmd": "$cmd",
    "md_file": "$md",
    "created": datetime.datetime.now().isoformat(),
    "settings": {}
}
with open("$file", "w") as f:
    json.dump(agent, f, indent=2)
PYEOF
}

_list_agent_names() {
    for f in "$AGENTS_DIR"/*.json; do
        [ -f "$f" ] || continue
        python3 -c "import json; print('  -', json.load(open('$f'))['name'])"
    done
}

_build_context() {
    local agent_name="$1"
    local context_file="$CONTEXT_DIR/${agent_name}_context.md"

    # Gather last N sessions
    local sessions=($(ls -t "$SESSIONS_DIR"/${agent_name}_*.json 2>/dev/null | head -5))

    python3 << PYEOF
import json, os

context_lines = []
context_lines.append("# Agent Context: $agent_name")
context_lines.append("")

# Project info
config_file = "$CONFIG_FILE"
if os.path.exists(config_file):
    with open(config_file) as f:
        cfg = json.load(f)
    context_lines.append(f"**Project:** {cfg.get('project_name', 'unknown')}")
    context_lines.append("")

# Recent sessions
session_files = """${sessions[*]:-}""".split()
if session_files and session_files[0]:
    context_lines.append("## Recent Session History")
    context_lines.append("")
    for sf in session_files:
        if not os.path.exists(sf):
            continue
        with open(sf) as f:
            s = json.load(f)
        date = s.get('date', '?')[:16]
        summary = s.get('summary', '')
        files = s.get('modified_files', [])
        context_lines.append(f"### {date}")
        context_lines.append(f"**Summary:** {summary}")
        if files:
            context_lines.append(f"**Files changed:** {', '.join(files[:10])}")
        context_lines.append("")

# Current git status
import subprocess
try:
    branch = subprocess.run(['git', 'branch', '--show-current'], capture_output=True, text=True).stdout.strip()
    context_lines.append(f"## Current State")
    context_lines.append(f"**Git branch:** {branch}")

    status = subprocess.run(['git', 'status', '--short'], capture_output=True, text=True).stdout.strip()
    if status:
        context_lines.append(f"**Pending changes:**")
        for line in status.split('\n')[:15]:
            context_lines.append(f"  {line}")
    context_lines.append("")
except:
    pass

# Write context
with open("$context_file", "w") as f:
    f.write("\n".join(context_lines))
PYEOF
}

_update_context() {
    local agent_name="$1"
    local summary="$2"
    local files="$3"

    # Rebuild full context
    _build_context "$agent_name"
}

# ─────────────────────────────────────────────
# HELP
# ─────────────────────────────────────────────
cmd_help() {
    echo -e ""
    echo -e "${BOLD}${CYAN}${ICON_ROBOT} Agent Hub - AI Agent Manager${NC}"
    echo -e "${DIM}Manage multiple AI agents with persistent memory${NC}"
    echo -e ""
    echo -e "${BOLD}Commands:${NC}"
    echo -e "  ${GREEN}init${NC}              Initialize agent-hub in current project"
    echo -e "  ${GREEN}agents${NC}            List all registered agents"
    echo -e "  ${GREEN}launch${NC}  <name>    Launch an agent with memory context"
    echo -e "  ${GREEN}switch${NC}  [name]    Quick switch between agents"
    echo -e "  ${GREEN}resume${NC}  [name]    Resume last session"
    echo -e "  ${GREEN}save${NC}    [name]    Save current session"
    echo -e "  ${GREEN}history${NC} <name>    Show session history"
    echo -e "  ${GREEN}context${NC} [name]    Show current context"
    echo -e "  ${GREEN}add${NC}              Register a new agent"
    echo -e "  ${GREEN}status${NC}            Overview dashboard"
    echo -e "  ${GREEN}help${NC}              Show this help"
    echo -e ""
    echo -e "${BOLD}Examples:${NC}"
    echo -e "  ${DIM}agent-hub init${NC}"
    echo -e "  ${DIM}agent-hub launch claude-code${NC}"
    echo -e "  ${DIM}agent-hub switch copilot${NC}"
    echo -e "  ${DIM}agent-hub save claude-code \"Fixed encoder UI scaling\"${NC}"
    echo -e "  ${DIM}agent-hub add my-agent md-agent \"cat agent.md\"${NC}"
    echo -e ""
}

# ─────────────────────────────────────────────
# MAIN ROUTER
# ─────────────────────────────────────────────
main() {
    local cmd="${1:-help}"
    shift 2>/dev/null || true

    case "$cmd" in
        init)    cmd_init "$@" ;;
        agents)  cmd_agents "$@" ;;
        launch)  cmd_launch "$@" ;;
        switch)  cmd_switch "$@" ;;
        resume)  cmd_resume "$@" ;;
        save)    cmd_save "$@" ;;
        history) cmd_history "$@" ;;
        context) cmd_context "$@" ;;
        add)     cmd_add "$@" ;;
        status)  cmd_status "$@" ;;
        help)    cmd_help "$@" ;;
        *)       echo -e "${RED}Unknown command: $cmd${NC}"; cmd_help ;;
    esac
}

main "$@"
