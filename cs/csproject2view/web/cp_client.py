# -*- coding: utf-8 -*-
"""
与 C++ 预约系统服务端 (127.0.0.1:9999) 通信的客户端封装。

C++ 服务端协议 (JSON over TCP)：
    请求字段：
        - type:        操作类型 (1=Login,2=Register,3=CheckApoint,
                                  4=Appointment,5=AppoinMessage,6=CancelAppoin)
        - user_tel:    手机号
        - user_name:   用户名
        - passwd:      密码
        - ticket_id:   票 id (字符串)
        - yd_id:       预约 id (字符串)
    响应字段：
        - status:      "ok" / "false" / "MYSQL CLIENT BUSSY"
        - num:         记录数 (查询类)
        - ticket_arr:  票数组 (查看可预约)
        - my_ticket_arr: 我的预约数组
"""
import socket
import json
import time

# 与 C++ 端 enum OP_TYPE 完全一致
OP_LOGIN = 1
OP_REGISTER = 2
OP_CHECK_APOINT = 3
OP_APPOINTMENT = 4
OP_APPOIN_MESSAGE = 5
OP_CANCEL_APPOIN = 6

# 状态码常量
STATUS_OK = "ok"
STATUS_FALSE = "false"
STATUS_BUSY = "MYSQL CLIENT BUSSY"


class CPClientError(Exception):
    """与 C++ 服务通信相关的异常"""


class CPClient(object):
    """对 C++ 预约系统服务端的轻量 TCP 客户端。

    每个业务方法都会新建一条 TCP 连接：发送请求 -> 等待响应 -> 关闭连接。
    这与原 C++ 客户端行为一致，也避免长连接状态管理问题。
    """

    def __init__(self, host="127.0.0.1", port=9999,
                 connect_timeout=3.0, recv_timeout=5.0):
        self.host = host
        self.port = port
        self.connect_timeout = connect_timeout
        self.recv_timeout = recv_timeout

    # ----------------------------------------------------------------- 底层
    def _round_trip(self, req_obj):
        """发送一个 JSON 请求并接收完整 JSON 响应。

        返回解析后的 dict。若连接失败、超时或 JSON 解析失败，抛 CPClientError。
        """
        payload = json.dumps(req_obj, ensure_ascii=False)
        data = payload.encode("utf-8")

        # C++ 服务端用 buff[MAXLINE=256] recv，请求超长会被截断导致解析失败
        if len(data) >= 256:
            raise CPClientError(
                "请求过长 (%d 字节)，C++ 服务端 MAXLINE=256，请缩短输入" % len(data)
            )

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.connect_timeout)
            sock.connect((self.host, self.port))
        except socket.error as e:
            raise CPClientError(
                "无法连接 C++ 服务 %s:%d，请确认 ./service 已启动 (%s)"
                % (self.host, self.port, e)
            )

        try:
            sock.settimeout(self.recv_timeout)
            sock.sendall(data)

            chunks = []
            total = 0
            # 循环 recv，直到拿到完整 JSON 或连接关闭/超时
            # 服务端响应是 toStyledString，可能大于一个 TCP 报文
            while True:
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    # 接收空闲，尝试解析已收到的数据
                    break
                if not chunk:
                    # 对端关闭，结束接收
                    break
                chunks.append(chunk)
                total += len(chunk)

                # 尝试解析，成功则提前结束
                if self._try_parse(chunks) is not None:
                    break

                # 防御性上限，避免异常情况下无限增长
                if total >= 64 * 1024:
                    break

            raw = b"".join(chunks)
            if not raw:
                raise CPClientError("C++ 服务未返回任何数据")

            parsed = self._try_parse([raw])
            if parsed is None:
                raise CPClientError(
                    "C++ 服务返回的不是有效 JSON：%r" % raw[:200]
                )
            return parsed
        finally:
            try:
                sock.close()
            except Exception:
                pass

    @staticmethod
    def _try_parse(chunks):
        try:
            text = b"".join(chunks).decode("utf-8", errors="strict")
            return json.loads(text)
        except (ValueError, UnicodeDecodeError):
            return None

    # ----------------------------------------------------------- 业务方法
    def register(self, user_tel, user_name, passwd):
        """用户注册"""
        resp = self._round_trip({
            "type": OP_REGISTER,
            "user_tel": user_tel,
            "user_name": user_name,
            "passwd": passwd,
        })
        return self._normalize_status(resp), resp

    def login(self, user_tel, user_name, passwd):
        """用户登录"""
        resp = self._round_trip({
            "type": OP_LOGIN,
            "user_tel": user_tel,
            "user_name": user_name,
            "passwd": passwd,
        })
        return self._normalize_status(resp), resp

    def show_tickets(self):
        """查看可预约的票列表"""
        resp = self._round_trip({"type": OP_CHECK_APOINT})
        return self._normalize_status(resp), resp

    def appoint(self, user_tel, ticket_id):
        """预定一张票"""
        resp = self._round_trip({
            "type": OP_APPOINTMENT,
            "user_tel": user_tel,
            "ticket_id": str(ticket_id),
        })
        return self._normalize_status(resp), resp

    def show_my_tickets(self, user_tel):
        """查看我的预约"""
        resp = self._round_trip({
            "type": OP_APPOIN_MESSAGE,
            "user_tel": user_tel,
        })
        return self._normalize_status(resp), resp

    def cancel(self, user_tel, yd_id):
        """取消预约"""
        resp = self._round_trip({
            "type": OP_CANCEL_APPOIN,
            "user_tel": user_tel,
            "yd_id": str(yd_id),
        })
        return self._normalize_status(resp), resp

    # ----------------------------------------------------------- 辅助
    @staticmethod
    def _normalize_status(resp):
        """把服务端 status 字段归一化为 (ok/false/busy) 字符串"""
        if not isinstance(resp, dict):
            return STATUS_FALSE
        status = resp.get("status")
        if status == STATUS_OK:
            return STATUS_OK
        if status == STATUS_BUSY:
            return STATUS_BUSY
        # false 或其它都视为失败
        return STATUS_FALSE


if __name__ == "__main__":
    # 自测：直接运行会尝试连服务
    c = CPClient()
    try:
        ok, resp = c.show_tickets()
        print("show_tickets ->", ok)
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    except CPClientError as e:
        print("ERR:", e)
