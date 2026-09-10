# -*- coding: utf-8 -*-
"""
预约系统 Web 可视化界面 - Flask 后端

职责：
  1. 提供网页 (templates/...)
  2. 提供 JSON API，转发到 C++ TCP 服务 (127.0.0.1:9527)
  3. 用 Flask session 维持登录态

启动：
    python3 app.py
    或: ./start.sh
默认监听 0.0.0.0:5500，浏览器访问 http://localhost:5500
"""
import os
import functools

from flask import (Flask, render_template, request, session,
                   jsonify, redirect, url_for, abort)

from cp_client import CPClient, CPClientError, STATUS_OK, STATUS_BUSY

app = Flask(__name__)
app.config["SECRET_KEY"] = os.environ.get(
    "APPOINT_SECRET_KEY", "dev-secret-please-change-in-prod"
)

# C++ 服务地址，可按需通过环境变量覆盖
CP_HOST = os.environ.get("CP_HOST", "127.0.0.1")
CP_PORT = int(os.environ.get("CP_PORT", "9527"))

cp = CPClient(host=CP_HOST, port=CP_PORT)


# ----------------------------------------------------------------- 工具
def login_required(view):
    """要求已登录才能访问的视图装饰器"""
    @functools.wraps(view)
    def wrapped(*args, **kwargs):
        if not session.get("user_tel"):
            # AJAX 请求返回 401，浏览器请求重定向到登录页
            if request.path.startswith("/api/"):
                return jsonify(ok=False, msg="请先登录"), 401
            return redirect(url_for("login"))
        return view(*args, **kwargs)
    return wrapped


def api_err(msg, status=400):
    return jsonify(ok=False, msg=msg), status


def api_busy():
    return jsonify(ok=False, msg="服务器繁忙，请稍后重试"), 503


# ----------------------------------------------------------------- 页面
@app.route("/")
def index():
    """主页：未登录跳登录页；已登录渲染主界面"""
    if not session.get("user_tel"):
        return redirect(url_for("login"))
    return render_template("index.html", user_name=session.get("user_name", ""))


@app.route("/login")
def login():
    """登录 / 注册页"""
    # 已登录直接进主页
    if session.get("user_tel"):
        return redirect(url_for("index"))
    return render_template("login.html")


@app.route("/logout", methods=["POST"])
def logout():
    session.clear()
    return jsonify(ok=True)


# ----------------------------------------------------------------- API
@app.route("/api/register", methods=["POST"])
def api_register():
    data = request.get_json(silent=True) or {}
    tel = (data.get("user_tel") or "").strip()
    name = (data.get("user_name") or "").strip()
    passwd = (data.get("passwd") or "").strip()

    if not (tel and name and passwd):
        return api_err("手机号、用户名、密码都不能为空")
    if len(passwd) < 4:
        return api_err("密码至少 4 位")

    try:
        status, resp = cp.register(tel, name, passwd)
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("注册失败，该手机号可能已存在")

    # 注册成功后视作已登录（与 C++ 客户端行为一致）
    session["user_tel"] = tel
    session["user_name"] = name
    return jsonify(ok=True, msg="注册成功")


@app.route("/api/login", methods=["POST"])
def api_login():
    data = request.get_json(silent=True) or {}
    tel = (data.get("user_tel") or "").strip()
    name = (data.get("user_name") or "").strip()
    passwd = (data.get("passwd") or "").strip()

    if not (tel and name and passwd):
        return api_err("手机号、用户名、密码都不能为空")

    try:
        status, resp = cp.login(tel, name, passwd)
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("用户名或密码错误")

    session["user_tel"] = tel
    session["user_name"] = name
    return jsonify(ok=True, msg="登录成功")


@app.route("/api/tickets", methods=["GET"])
@login_required
def api_tickets():
    """查看可预约的票"""
    try:
        status, resp = cp.show_tickets()
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("查看预约信息失败")

    num = resp.get("num", 0)
    arr = resp.get("ticket_arr", []) or []
    return jsonify(ok=True, num=num, tickets=arr)


@app.route("/api/appoint", methods=["POST"])
@login_required
def api_appoint():
    """预定一张票"""
    data = request.get_json(silent=True) or {}
    ticket_id = (data.get("ticket_id") or "").strip()
    if not ticket_id:
        return api_err("缺少 ticket_id")

    try:
        status, resp = cp.appoint(session["user_tel"], ticket_id)
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("预定失败，票数可能已满")
    return jsonify(ok=True, msg="预定成功")


@app.route("/api/my_tickets", methods=["GET"])
@login_required
def api_my_tickets():
    """查看我的预约"""
    try:
        status, resp = cp.show_my_tickets(session["user_tel"])
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("查看我的预约失败")

    num = resp.get("num", 0)
    arr = resp.get("my_ticket_arr", []) or []
    return jsonify(ok=True, num=num, tickets=arr)


@app.route("/api/cancel", methods=["POST"])
@login_required
def api_cancel():
    """取消预定"""
    data = request.get_json(silent=True) or {}
    yd_id = (data.get("yd_id") or "").strip()
    if not yd_id:
        return api_err("缺少 yd_id")

    try:
        status, resp = cp.cancel(session["user_tel"], yd_id)
    except CPClientError as e:
        return api_err(str(e), 502)

    if status == STATUS_BUSY:
        return api_busy()
    if status != STATUS_OK:
        return api_err("取消预约失败")
    return jsonify(ok=True, msg="取消预约成功")


# ----------------------------------------------------------------- 入口
@app.route("/api/me", methods=["GET"])
@login_required
def api_me():
    return jsonify(ok=True,
                   user_tel=session.get("user_tel", ""),
                   user_name=session.get("user_name", ""))


if __name__ == "__main__":
    print("=" * 60)
    print("预约系统 Web 界面启动中")
    print("C++ 服务地址: %s:%d" % (CP_HOST, CP_PORT))
    print("Web 监听: http://0.0.0.0:5500")
    print("浏览器请访问: http://localhost:5500")
    print("=" * 60)
    # debug=False 避免多进程导致 C++ 连接被并发打开
    app.run(host="0.0.0.0", port=5500, debug=False, threaded=True)
