#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
csproject2 压测脚本
用法:
    python3 stress_test.py login    [并发数] [每客户端请求数]
    python3 stress_test.py check    [并发数] [每客户端请求数]
    python3 stress_test.py register [并发数] [每客户端请求数]
    python3 stress_test.py appoint  [并发数]            # 并发抢同一张票
"""
import json
import socket
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 8888
RECV_SIZE = 65535
MAX_RETRY = 3

# 协议操作类型
LOGIN = 1
REGISTER = 2
CHECK = 3
APPOINT = 4

# 已存在的测试账号
USER_TEL = "13400000000"
USER_NAME = "小王"
USER_PASSWD = "123456"

results_lock = threading.Lock()


def one_request(payload, timeout=10):
    """建立连接 -> 发送一次请求 -> 读取一次响应，返回 (status, 耗时秒)"""
    start = time.time()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((HOST, PORT))
        s.sendall(json.dumps(payload).encode())
        data = s.recv(RECV_SIZE)
        cost = time.time() - start
        if not data:
            return "closed", cost
        try:
            resp = json.loads(data.decode(errors="ignore"))
        except Exception:
            return "parse_err", cost
        return resp.get("status", "no_status"), cost
    except Exception as e:
        return "err:" + type(e).__name__, time.time() - start
    finally:
        s.close()


def run_scenario(name, concurrency, per_client, payload_fn):
    stats = {"ok": 0, "false": 0, "busy": 0, "other": 0, "conn_err": 0,
             "err_types": {}, "retry": 0}
    latencies = []
    lock = threading.Lock()

    def worker(idx):
        local_ok = local_false = local_busy = local_other = 0
        local_conn_err = 0
        local_err_types = {}
        local_retry = {}
        local_lat = []
        s = None
        phase = ["init"]

        def ensure_conn():
            nonlocal s
            if s is None:
                phase[0] = "connect"
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(15)
                s.connect((HOST, PORT))

        for j in range(per_client):
            payload = payload_fn(idx, j)
            done = False
            for attempt in range(MAX_RETRY):
                try:
                    ensure_conn()
                    phase[0] = "io"
                    start = time.time()
                    s.sendall(json.dumps(payload).encode())
                    data = s.recv(RECV_SIZE)
                    local_lat.append(time.time() - start)
                    if not data:
                        local_other += 1
                        s.close()
                        s = None
                        done = True
                        break
                    try:
                        status = json.loads(data.decode(errors="ignore")).get("status", "no_status")
                    except Exception:
                        status = "parse_err"
                    if status == "ok":
                        local_ok += 1
                    elif status == "false":
                        local_false += 1
                    elif status == "MYSQL CLIENT BUSSY":
                        local_busy += 1
                    else:
                        local_other += 1
                    done = True
                    break
                except Exception as e:
                    key = "%s@%s" % (type(e).__name__, phase[0])
                    local_retry[key] = local_retry.get(key, 0) + 1
                    if s is not None:
                        try:
                            s.close()
                        except Exception:
                            pass
                        s = None
            if not done:
                local_conn_err += 1
        if s is not None:
            s.close()
        with lock:
            stats["retry"] += sum(local_retry.values())
            stats["ok"] += local_ok
            stats["false"] += local_false
            stats["busy"] += local_busy
            stats["other"] += local_other
            stats["conn_err"] += local_conn_err
            for k, v in local_retry.items():
                stats["err_types"][k] = stats["err_types"].get(k, 0) + v
            latencies.extend(local_lat)

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(concurrency)]
    t0 = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - t0

    total = stats["ok"] + stats["false"] + stats["busy"] + stats["other"]
    latencies.sort()

    def pct(p):
        if not latencies:
            return 0.0
        k = min(len(latencies) - 1, int(len(latencies) * p))
        return latencies[k] * 1000

    print("==== 场景: %s ====" % name)
    print("并发数: %d, 每客户端请求数: %d, 完成请求: %d" % (concurrency, per_client, total))
    print("总耗时: %.3f s" % elapsed)
    print("QPS: %.1f" % (total / elapsed if elapsed else 0))
    print("成功(ok): %d, 失败(false): %d, 池满(busy): %d, 其他: %d, 最终失败: %d"
          % (stats["ok"], stats["false"], stats["busy"], stats["other"], stats["conn_err"]))
    if stats["retry"]:
        print("连接重置(已重试): %d 次 %s" % (stats["retry"], stats["err_types"]))
    print("平均延迟: %.2f ms, P95: %.2f ms, 最大: %.2f ms"
          % (sum(latencies) * 1000 / len(latencies) if latencies else 0,
             pct(0.95), latencies[-1] * 1000 if latencies else 0))
    return {"total": total, "elapsed": elapsed, "stats": stats,
            "avg": (sum(latencies) * 1000 / len(latencies)) if latencies else 0,
            "p95": pct(0.95), "max": latencies[-1] * 1000 if latencies else 0,
            "qps": total / elapsed if elapsed else 0}


def scenario_login(concurrency, per_client):
    def payload(idx, j):
        return {"type": LOGIN, "user_tel": USER_TEL,
                "user_name": USER_NAME, "passwd": USER_PASSWD}
    return run_scenario("登录", concurrency, per_client, payload)


def scenario_check(concurrency, per_client):
    def payload(idx, j):
        return {"type": CHECK}
    return run_scenario("查票", concurrency, per_client, payload)


def scenario_register(concurrency, per_client):
    def payload(idx, j):
        tel = "19%09d" % (idx * 100000 + j)
        return {"type": REGISTER, "user_tel": tel,
                "user_name": "u%d_%d" % (idx, j), "passwd": "123456"}
    return run_scenario("注册", concurrency, per_client, payload)


def scenario_appoint(concurrency):
    """并发抢同一张票: 每个客户端 1 次预约，票 id 由命令行给出（默认 1）"""
    ticket_id = sys.argv[3] if len(sys.argv) > 3 else "1"

    def payload(idx, j):
        tel = "18%09d" % idx
        return {"type": APPOINT, "user_tel": tel, "ticket_id": ticket_id}
    return run_scenario("并发预约(票id=%s)" % ticket_id, concurrency, 1, payload)


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    c = int(sys.argv[2]) if len(sys.argv) > 2 else 50
    if mode == "login":
        run_scenario = scenario_login(c, int(sys.argv[3]) if len(sys.argv) > 3 else 20)
    elif mode == "check":
        run_scenario = scenario_check(c, int(sys.argv[3]) if len(sys.argv) > 3 else 20)
    elif mode == "register":
        scenario_register(c, int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    elif mode == "appoint":
        scenario_appoint(c)
    else:
        print("unknown mode")