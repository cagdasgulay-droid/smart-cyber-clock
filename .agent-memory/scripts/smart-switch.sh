#!/bin/bash
cd "/Users/cagdasgulay/Desktop/CYBER CLOCK"
MEMORY_DIR=".agent-memory"
AGENTS_DIR="$MEMORY_DIR/agents"
ACTIVE_FILE="$MEMORY_DIR/.active-agent"
CONTEXT_DIR="$MEMORY_DIR/context"

echo ""
echo "🔄 Quick Switch"
echo ""

i=1
agents=()
for agent_file in "$AGENTS_DIR"/*.json; do
    [ -f "$agent_file" ] || continue
    name=$(python3 -c "import json; print(json.load(open('$agent_file'))['name'])")
    agents+=("$name")
    active_mark=""
    [ -f "$ACTIVE_FILE" ] && [ "$(cat "$ACTIVE_FILE")" = "$name" ] && active_mark=" ●"
    echo "  $i) ${name}${active_mark}"
    ((i++))
done

echo ""
printf "Select agent [1-$((i-1))]: "
read -r choice

if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#agents[@]}" ]; then
    selected="${agents[$((choice-1))]}"
    if [ -f "$ACTIVE_FILE" ]; then
        current=$(cat "$ACTIVE_FILE")
        if [ "$current" != "$selected" ]; then
            echo "💾 Saving $current..."
            bash "$MEMORY_DIR/scripts/agent-hub.sh" save "$current" "Auto-save on switch to $selected" 2>/dev/null
        fi
    fi
    echo "$selected" > "$ACTIVE_FILE"
    bash "$MEMORY_DIR/scripts/agent-hub.sh" context "$selected" > /dev/null 2>&1

    agent_json="$AGENTS_DIR/${selected}.json"
    md_file=$(python3 -c "import json; print(json.load(open('$agent_json')).get('md_file',''))")

    # CLAUDE.md'yi seçilen agent ile değiştir
    {
        echo "# Active Agent: $selected"
        echo ""
        [ -f "$md_file" ] && cat "$md_file"
        echo ""
        echo "---"
        echo ""
        [ -f "$CONTEXT_DIR/${selected}_context.md" ] && cat "$CONTEXT_DIR/${selected}_context.md"
    } > CLAUDE.md

    echo ""
    echo "✅ $selected aktif!"
    echo "📄 CLAUDE.md güncellendi"
    echo "🚀 Claude Code açılıyor..."
    echo ""
    claude
else
    echo "❌ Geçersiz seçim"
fi
