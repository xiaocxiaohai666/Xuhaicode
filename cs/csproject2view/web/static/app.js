/* ============================================================
   预约系统 Web 界面 - 前端逻辑
   ============================================================ */

(function () {
  "use strict";

  // --------- 工具 ---------
  function qs(sel, root) { return (root || document).querySelector(sel); }
  function qsa(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  function showMessage(el, text, type) {
    if (!el) return;
    el.textContent = text || "";
    el.className = "auth-msg " + (type || "");
  }

  function toast(text, type) {
    var el = qs("#global-msg");
    if (!el) return;
    el.textContent = text || "";
    el.className = "toast " + (type || "");
    if (type) {
      el.classList.add("show");
      setTimeout(function () {
        el.className = "toast";
      }, 2500);
    }
  }

  function fetchWithTimeout(url, opts, ms) {
    var ctrl = new AbortController();
    var timer = setTimeout(function () { ctrl.abort(); }, ms || 10000);
    opts = opts || {};
    opts.credentials = opts.credentials || "same-origin";
    opts.signal = ctrl.signal;
    return fetch(url, opts).finally(function () { clearTimeout(timer); });
  }

  function postJSON(url, body) {
    return fetchWithTimeout(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {})
    }).then(function (r) {
      return r.json().then(function (d) {
        return { ok: !!d.ok, status: r.status, data: d };
      });
    });
  }

  function getJSON(url) {
    return fetchWithTimeout(url, { method: "GET" }).then(function (r) {
      return r.json().then(function (d) {
        return { ok: !!d.ok, status: r.status, data: d };
      });
    });
  }

  function netErr(err) {
    if (err && err.name === "AbortError") return "请求超时：服务未响应，请确认服务已启动";
    return "网络错误：服务未运行或已断开，请确认服务已启动后刷新页面重试";
  }

  function formData(form) {
    var obj = {};
    var els = form.elements;
    for (var i = 0; i < els.length; i++) {
      var e = els[i];
      if (e.name) obj[e.name] = e.value.trim();
    }
    return obj;
  }

  // ===================== 登录 / 注册页 =====================
  function initAuthPage() {
    var card = qs(".auth-card");
    if (!card) return;

    // 标签切换
    qsa(".tab-btn").forEach(function (btn) {
      btn.addEventListener("click", function () {
        qsa(".tab-btn").forEach(function (b) { b.classList.remove("active"); });
        btn.classList.add("active");
        var tab = btn.getAttribute("data-tab");
        qsa(".auth-form").forEach(function (f) { f.classList.remove("active"); });
        qs("#" + tab + "-form").classList.add("active");
        showMessage(qs("#auth-msg"), "", "");
      });
    });

    // 登录提交
    qs("#login-form").addEventListener("submit", function (e) {
      e.preventDefault();
      var msg = qs("#auth-msg");
      var data = formData(this);
      if (!data.user_tel || !data.user_name || !data.passwd) {
        return showMessage(msg, "请填写完整信息", "err");
      }
      var btn = qs("#login-form button[type=submit]");
      btn.disabled = true; btn.textContent = "登录中...";
      postJSON("/api/login", data).then(function (r) {
        btn.disabled = false; btn.textContent = "登录";
        if (r.ok) {
          window.location.href = "/";
        } else {
          showMessage(msg, r.data && r.data.msg ? r.data.msg : "登录失败", "err");
        }
      }).catch(function () {
        btn.disabled = false; btn.textContent = "登录";
        showMessage(msg, "网络错误，请稍后重试", "err");
      });
    });

    // 注册提交
    qs("#register-form").addEventListener("submit", function (e) {
      e.preventDefault();
      var msg = qs("#auth-msg");
      var data = formData(this);
      if (!data.user_tel || !data.user_name || !data.passwd) {
        return showMessage(msg, "请填写完整信息", "err");
      }
      if (data.passwd.length < 4) {
        return showMessage(msg, "密码至少 4 位", "err");
      }
      var btn = qs("#register-form button[type=submit]");
      btn.disabled = true; btn.textContent = "注册中...";
      postJSON("/api/register", data).then(function (r) {
        btn.disabled = false; btn.textContent = "注册";
        if (r.ok) {
          showMessage(msg, "注册成功，正在进入...", "ok");
          setTimeout(function () { window.location.href = "/"; }, 600);
        } else {
          showMessage(msg, r.data && r.data.msg ? r.data.msg : "注册失败", "err");
        }
      }).catch(function () {
        btn.disabled = false; btn.textContent = "注册";
        showMessage(msg, "网络错误，请稍后重试", "err");
      });
    });
  }

  // ===================== 主页 =====================
  function initAppPage() {
    var page = qs(".app-page");
    if (!page) return;

    var currentView = "tickets";

    // 侧栏切换
    qsa(".nav-item").forEach(function (item) {
      item.addEventListener("click", function () {
        var view = item.getAttribute("data-view");
        if (view === currentView) return;
        qsa(".nav-item").forEach(function (b) { b.classList.remove("active"); });
        item.classList.add("active");
        qsa(".view").forEach(function (v) { v.classList.remove("active"); });
        qs("#view-" + view).classList.add("active");
        currentView = view;
        qs("#view-title").textContent = (view === "tickets") ? "可预约的票" : "我的预约";
        loadCurrent();
      });
    });

    // 退出
    qs("#btn-logout").addEventListener("click", function () {
      postJSON("/logout", {}).then(function () {
        window.location.href = "/login";
      });
    });

    // 刷新
    qs("#btn-refresh").addEventListener("click", loadCurrent);

    // 加载当前视图数据，返回 promise 便于刷新后回调
    function loadCurrent() {
      if (currentView === "tickets") return loadTickets();
      else return loadMine();
    }

    function loadTickets() {
      var tbody = qs("#tb-tickets");
      tbody.innerHTML = '<tr class="empty-row"><td colspan="7">加载中...</td></tr>';
      return getJSON("/api/tickets").then(function (r) {
        if (!r.ok) {
          tbody.innerHTML = '<tr class="empty-row"><td colspan="7">' +
            ((r.data && r.data.msg) || "加载失败") + "</td></tr>";
          return;
        }
        renderTickets(r.data.tickets || []);
      }).catch(function () {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">网络错误</td></tr>';
      });
    }

    function renderTickets(list) {
      var tbody = qs("#tb-tickets");
      if (!list.length) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">暂无可预约的票</td></tr>';
        return;
      }
      tbody.innerHTML = list.map(function (t) {
        var id = (t.ticket_id !== undefined && t.ticket_id !== null) ? t.ticket_id : "";
        var max = parseInt(t.ticket_max, 10) || 0;
        var count = parseInt(t.ticket_count, 10) || 0;
        var left = max - count;
        var full = left <= 0;
        var leftClass = full ? "row-full" : "row-available";
        var btn = full
          ? '<button class="btn-action" disabled>已满</button>'
          : '<button class="btn-action" data-appoint="' + id + '">预定</button>';
        return '<tr>' +
          '<td>' + id + '</td>' +
          '<td>' + esc(t.ticket_name) + '</td>' +
          '<td>' + max + '</td>' +
          '<td>' + count + '</td>' +
          '<td class="' + leftClass + '">' + left + '</td>' +
          '<td>' + esc(t.day_time) + '</td>' +
          '<td>' + btn + '</td>' +
          '</tr>';
      }).join("");

      // 绑定预定按钮
      qsa("[data-appoint]", tbody).forEach(function (b) {
        b.addEventListener("click", function () {
          var id = b.getAttribute("data-appoint");
          b.disabled = true; b.textContent = "预定中...";
          postJSON("/api/appoint", { ticket_id: id }).then(function (r) {
            if (r.ok) {
              loadCurrent().then(function () { alert("预定成功"); });
            } else {
              b.disabled = false; b.textContent = "预定";
              alert((r.data && r.data.msg) || "预定失败");
            }
          }).catch(function (err) {
            b.disabled = false; b.textContent = "预定";
            alert(netErr(err));
          });
        });
      });
    }

    function loadMine() {
      var tbody = qs("#tb-mine");
      tbody.innerHTML = '<tr class="empty-row"><td colspan="6">加载中...</td></tr>';
      return getJSON("/api/my_tickets").then(function (r) {
        if (!r.ok) {
          tbody.innerHTML = '<tr class="empty-row"><td colspan="6">' +
            ((r.data && r.data.msg) || "加载失败") + "</td></tr>";
          return;
        }
        renderMine(r.data.tickets || []);
      }).catch(function () {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="6">服务未运行或已断开</td></tr>';
      });
    }

    function renderMine(list) {
      var tbody = qs("#tb-mine");
      if (!list.length) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="6">您还没有任何预约</td></tr>';
        return;
      }
      tbody.innerHTML = list.map(function (t) {
        var ydId = (t.yd_id !== undefined && t.yd_id !== null) ? t.yd_id : "";
        return '<tr>' +
          '<td>' + ydId + '</td>' +
          '<td>' + esc(t.ticket_name) + '</td>' +
          '<td>' + esc(t.ticket_max) + '</td>' +
          '<td>' + esc(t.ticket_count) + '</td>' +
          '<td>' + esc(t.day_time) + '</td>' +
          '<td><button class="btn-action danger" data-cancel="' + ydId + '">取消</button></td>' +
          '</tr>';
      }).join("");

      qsa("[data-cancel]", tbody).forEach(function (b) {
        b.addEventListener("click", function () {
          var id = b.getAttribute("data-cancel");
          b.disabled = true; b.textContent = "取消中...";
          postJSON("/api/cancel", { yd_id: id }).then(function (r) {
            if (r.ok) {
              loadCurrent().then(function () { alert("取消成功"); });
            } else {
              b.disabled = false; b.textContent = "取消";
              alert((r.data && r.data.msg) || "取消失败");
            }
          }).catch(function (err) {
            b.disabled = false; b.textContent = "取消";
            alert(netErr(err));
          });
        });
      });
    }

    // 拉一下当前用户信息（显示手机号）
    getJSON("/api/me").then(function (r) {
      if (r.ok && r.data) {
        var tel = qs("#nav-user-tel");
        if (tel && r.data.user_tel) tel.textContent = r.data.user_tel;
        var name = qs("#nav-user-name");
        if (name && r.data.user_name) name.textContent = r.data.user_name;
      }
    }).catch(function () {});

    // 首次加载
    loadCurrent();
  }

  // 简单 HTML 转义，防止内容注入
  function esc(v) {
    if (v === undefined || v === null) return "";
    return String(v).replace(/[&<>"']/g, function (c) {
      return ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;",
        '"': "&quot;", "'": "&#39;"
      })[c];
    });
  }

  document.addEventListener("DOMContentLoaded", function () {
    initAuthPage();
    initAppPage();
  });
})();
