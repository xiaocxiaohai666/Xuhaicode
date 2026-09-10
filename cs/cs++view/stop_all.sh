#!/bin/bash
# ============================================================
# 预约系统一键停止脚本（只停本项目，不误杀其他项目）
# 用法:  ./stop_all.sh
# 原理:  优先用 start_all.sh 写的 PID 文件精确 kill；
#        PID 文件不存在时按端口 9527/5500 兜底，绝不误杀其他项目。
# ============================================================

ROOT=$(cd "$(dirname "$0")" && pwd)
SVC_PID_FILE="$ROOT/.service.pid"
WEB_PID_FILE="$ROOT/.web.pid"

GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'
ok()   { echo -e "${GREEN}[OK]${NC}   $1"; }
info() { echo -e "${YELLOW}[..]${NC}  $1"; }

# 按端口找进程 PID（仅占用 9527/5500 的进程，不会碰 8888）
port_pid() { ss -ltnp 2>/dev/null | grep ":$1 " | grep -oP 'pid=\K[0-9]+' | head -1; }

# kill 一个 PID（先 TERM 后 KILL）
kill_pid() {
    local pid=$1 name=$2
    if [ -z "$pid" ]; then
        ok "$name 未运行，跳过"
        return
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        ok "$name PID $pid 已不存在"
        return
    fi
    info "停止 $name: PID $pid"
    kill "$pid" 2>/dev/null
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
    fi
    ok "$name 已停止"
}

# ---- 停止 Web 服务 ----
WEB_PID=""
if [ -f "$WEB_PID_FILE" ]; then
    WEB_PID=$(cat "$WEB_PID_FILE" 2>/dev/null)
fi
if [ -z "$WEB_PID" ]; then
    WEB_PID=$(port_pid 5500)   # PID 文件不存在时按端口兜底
fi
kill_pid "$WEB_PID" "Web 服务"
rm -f "$WEB_PID_FILE"

# ---- 停止 C++ 服务 ----
SVC_PID=""
if [ -f "$SVC_PID_FILE" ]; then
    SVC_PID=$(cat "$SVC_PID_FILE" 2>/dev/null)
fi
if [ -z "$SVC_PID" ]; then
    SVC_PID=$(port_pid 9527)   # PID 文件不存在时按端口兜底
fi
kill_pid "$SVC_PID" "C++ 服务"
rm -f "$SVC_PID_FILE"

# ---- 确认端口已释放 ----
sleep 1
echo
if ! ss -ltn 2>/dev/null | grep -qE ':9527|:5500'; then
    ok "端口 9527 / 5500 已全部释放"
else
    info "仍有端口占用，请检查: ss -ltn | grep -E ':9527|:5500'"
fi
