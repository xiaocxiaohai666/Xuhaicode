#!/bin/bash
# 启动预约系统 Web 界面
# 使用前请先确保 C++ 服务已启动 (./service service.conf)
set -e

cd "$(dirname "$0")"

# 使用虚拟环境（若存在），否则用系统 python3
if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

# 自动安装 Flask（仅在缺失时）
python3 - <<'PY'
import sys
try:
    import flask
except ImportError:
    print("Flask 未安装，正在安装...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "flask"])
PY

exec python3 app.py
