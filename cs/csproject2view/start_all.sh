#!/bin/bash
# ============================================================
# 预约系统一键启动脚本
# 用法:  ./start_all.sh
# 做的事: 检查 MySQL -> 启动 C++ 服务 -> 启动 Web 服务 -> 打开浏览器
# ============================================================
set -e

ROOT=$(cd "$(dirname "$0")" && pwd)
WEB_DIR="$ROOT/web"
LOG_DIR="/tmp"

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

ok()   { echo -e "${GREEN}[OK]${NC}   $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; }
info() { echo -e "${YELLOW}[..]${NC}  $1"; }

# 检查某端口是否在监听
port_listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

# ------------------------------------------------- 1. 检查 MySQL
if ! port_listening 3306; then
    fail "MySQL 未启动（3306 未监听），请先启动 MySQL 再运行本脚本"
    exit 1
fi
ok "MySQL 已就绪 (3306)"

# ------------------------------------------------- 2. 启动 C++ 服务
if port_listening 9999; then
    ok "C++ 服务已在运行 (9999)，跳过"
else
    if [ ! -x "$ROOT/service" ]; then
        fail "未找到可执行文件 $ROOT/service，请先在项目根目录执行: make"
        exit 1
    fi
    info "启动 C++ 服务..."
    cd "$ROOT"
    nohup ./service service.conf > "$LOG_DIR/cs_service.log" 2>&1 &
    echo $! > "$ROOT/.service.pid"
    sleep 2
    if port_listening 9999; then
        ok "C++ 服务启动成功 (9999)"
    else
        fail "C++ 服务启动失败，日志: $LOG_DIR/cs_service.log"
        tail -n 10 "$LOG_DIR/cs_service.log" 2>/dev/null
        exit 1
    fi
fi

# ------------------------------------------------- 3. 启动 Web 服务
if port_listening 5000; then
    ok "Web 服务已在运行 (5000)，跳过"
else
    if [ ! -f "$WEB_DIR/app.py" ]; then
        fail "未找到 $WEB_DIR/app.py"
        exit 1
    fi
    info "检查 Flask 依赖..."
    if ! python3 -c "import flask" 2>/dev/null; then
        info "Flask 未安装，正在安装..."
        pip3 install flask >/dev/null 2>&1 || {
            fail "Flask 安装失败，请手动执行: pip3 install flask"
            exit 1
        }
        ok "Flask 安装完成"
    fi
    info "启动 Web 服务..."
    cd "$WEB_DIR"
    nohup python3 app.py > "$LOG_DIR/cs_web.log" 2>&1 &
    echo $! > "$ROOT/.web.pid"
    sleep 2
    if port_listening 5000; then
        ok "Web 服务启动成功 (5000)"
    else
        fail "Web 服务启动失败，日志: $LOG_DIR/cs_web.log"
        tail -n 10 "$LOG_DIR/cs_web.log" 2>/dev/null
        exit 1
    fi
fi

# ------------------------------------------------- 4. 打开浏览器
URL="http://localhost:5000"
if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$URL" >/dev/null 2>&1 || true
elif command -v sensible-browser >/dev/null 2>&1; then
    sensible-browser "$URL" >/dev/null 2>&1 || true
fi

echo
echo "============================================================"
echo -e "${GREEN} 预约系统已启动${NC}"
echo " 浏览器访问: $URL"
echo " C++ 服务日志: $LOG_DIR/cs_service.log"
echo " Web   服务日志: $LOG_DIR/cs_web.log"
echo " 停止服务:  ./stop_all.sh"
echo "============================================================"
