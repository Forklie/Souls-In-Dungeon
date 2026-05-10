#!/usr/bin/env bash
set -euo pipefail

MCP_URL="${MCP_URL:-http://localhost:9316/mcp}"

usage() {
  cat <<'EOF'
Usage: Scripts/mcp-refresh.sh [status|compile|logs|errors]

Refresh helpers for Monolith MCP without restarting Unreal Editor.

Commands:
  status   Check whether the editor MCP server is reachable and report build state.
  compile  Trigger Live Coding compile for .cpp body changes, then print compile output.
  logs     Show recent editor logs.
  errors   Show recent build/compiler errors and warnings.

Notes:
  - Asset and Blueprint MCP changes should be compiled/saved with the relevant
    Monolith action, such as blueprint_query compile_blueprint/save_asset.
  - .cpp body changes can usually use: Scripts/mcp-refresh.sh compile
  - Header, new source file, Build.cs, uplugin, and module layout changes still
    require closing Unreal and running a full UBT build.
EOF
}

mcp_call() {
  local id="$1"
  local tool="$2"
  local args_json="$3"

  curl -sS --max-time 30 -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' \
    -d "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args_json}}" \
    | python3 -m json.tool
}

editor_query() {
  local id="$1"
  local action="$2"
  local extra="${3:-}"
  local args

  if [[ -n "$extra" ]]; then
    args="{\"action\":\"$action\",$extra}"
  else
    args="{\"action\":\"$action\"}"
  fi

  mcp_call "$id" "editor_query" "$args"
}

command="${1:-status}"

case "$command" in
  status)
    mcp_call 1 "monolith_status" "{}"
    editor_query 2 "get_build_status"
    ;;
  compile)
    editor_query 1 "trigger_build" '"wait":true'
    editor_query 2 "get_compile_output"
    ;;
  logs)
    editor_query 1 "tail_log" '"count":80'
    ;;
  errors)
    editor_query 1 "get_build_errors" '"compile_only":true'
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
