import http.server
import socketserver
import os
import json
import urllib.parse
import subprocess
import time

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PORT = 8000

LOG_DIR = os.path.join(BASE_DIR, "logs")
LOG_JSONL = os.path.join(LOG_DIR, "udp_log.jsonl")

STATE_JSON = os.path.join(LOG_DIR, "user_state.json")
SESSION_MAP_JSONL = os.path.join(LOG_DIR, "session_user_map.jsonl")

# NOVO: lista de usuários “cadastrados/salvos” (para aparecer no relatório mesmo com 0 sessões)
KNOWN_USERS_JSON = os.path.join(LOG_DIR, "known_users.json")

_last_report_gen_ts = 0.0


def safe(v):
    return "" if v is None else str(v)


def to_int(v, default=0):
    try:
        return int(v)
    except Exception:
        return default


def load_json(path, default):
    if not os.path.exists(path):
        return default
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default


def save_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)


def append_jsonl(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")


def default_state():
    return {
        "pending_user": "",
        "last_user": "",
        "active_user": "",
        "active_key": "",   # ip|session
        "active_uid": "",   # ip|session|start_line
        "start_line": 0,
        "last_seen_line": 0  # NOVO: última linha lida pelo /tail (ajuda no “finalizar agora”)
    }


def load_state():
    return load_json(STATE_JSON, default_state())


def save_state(st):
    save_json(STATE_JSON, st)


def load_known_users():
    data = load_json(KNOWN_USERS_JSON, {"users": []})
    users = data.get("users", [])
    if not isinstance(users, list):
        users = []
    # normaliza
    out = []
    seen = set()
    for u in users:
        u = safe(u).strip()
        if not u:
            continue
        k = u.upper()
        if k in seen:
            continue
        seen.add(k)
        out.append(u)
    return out


def add_known_user(name: str, max_keep: int = 50):
    name = safe(name).strip()
    if not name:
        return
    users = load_known_users()
    # coloca no topo (mais recente)
    users = [u for u in users if u.upper() != name.upper()]
    users.insert(0, name)
    users = users[:max_keep]
    save_json(KNOWN_USERS_JSON, {"users": users})


def read_jsonl_tail(after_line: int, max_lines: int = 2000):
    if not os.path.exists(LOG_JSONL):
        return {"ok": True, "items": [], "next_after": after_line}

    items = []
    line_no = 0
    try:
        with open(LOG_JSONL, "r", encoding="utf-8") as f:
            for line in f:
                line_no += 1
                if line_no <= after_line:
                    continue
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    obj["_line"] = line_no
                    items.append(obj)
                except Exception:
                    continue
                if len(items) >= max_lines:
                    break
    except Exception as e:
        return {"ok": False, "err": str(e), "items": [], "next_after": after_line}

    next_after = items[-1]["_line"] if items else after_line
    return {"ok": True, "items": items, "next_after": next_after}


def generate_report():
    global _last_report_gen_ts
    now = time.time()
    if now - _last_report_gen_ts < 0.8:
        return {"ok": True, "msg": "Ignorado (muito rápido)."}
    _last_report_gen_ts = now
    try:
        r = subprocess.run(
            ["python", os.path.join(BASE_DIR, "report.py")],
            cwd=BASE_DIR,
            timeout=25,
            capture_output=True,
            text=True
        )
        if r.returncode == 0:
            return {"ok": True, "msg": "Relatório gerado."}
        return {"ok": False, "err": (r.stderr or r.stdout or "erro")}
    except Exception as e:
        return {"ok": False, "err": str(e)}


def apply_user_by_session(items):
    """
    - pending_user vira active_user no próximo START
    - usuário é aplicado somente na sessão ativa
    - no STOP fecha, grava map (start/stop) com UID e gera relatório
    """
    global _last_report_gen_ts

    st = load_state()
    changed = False
    report_needed = False

    for item in items:
        ev = safe(item.get("event")).strip().lower()
        ip = safe(item.get("src_ip")).strip()
        s = to_int(item.get("session"), -1)
        key = f"{ip}|{s}"
        line_no = to_int(item.get("_line"), 0)

        # START
        if ev == "start":
            if not st.get("active_key"):
                chosen = safe(st.get("pending_user")).strip()
                if not chosen:
                    chosen = safe(st.get("last_user")).strip()
                if not chosen:
                    chosen = "SEM_USER"

                uid = f"{key}|{line_no}"

                st["active_user"] = chosen
                st["active_key"] = key
                st["active_uid"] = uid
                st["start_line"] = line_no
                st["pending_user"] = ""
                st["last_user"] = chosen
                changed = True

                append_jsonl(SESSION_MAP_JSONL, {
                    "uid": uid,
                    "src_ip": ip,
                    "session": s,
                    "user": chosen,
                    "start_line": line_no,
                    "stop_line": 0
                })

        # aplica user na sessão ativa
        if st.get("active_key") and key == st.get("active_key"):
            if safe(item.get("user")).strip() in ("", "SEM_USER"):
                item["user"] = st.get("active_user") or "SEM_USER"

        # STOP
        if ev == "stop" and st.get("active_key") and key == st.get("active_key"):
            stop_user = st.get("active_user") or "SEM_USER"
            uid = st.get("active_uid") or ""

            if safe(item.get("user")).strip() in ("", "SEM_USER"):
                item["user"] = stop_user

            if uid:
                append_jsonl(SESSION_MAP_JSONL, {
                    "uid": uid,
                    "src_ip": ip,
                    "session": s,
                    "user": stop_user,
                    "start_line": 0,
                    "stop_line": line_no
                })

            st["active_user"] = ""
            st["active_key"] = ""
            st["active_uid"] = ""
            st["start_line"] = 0
            changed = True

            report_needed = True

    if items:
        st["last_seen_line"] = max(st.get("last_seen_line", 0), to_int(items[-1].get("_line"), 0))
        changed = True

    if changed:
        save_state(st)

    if report_needed:
        now = time.time()
        if now - _last_report_gen_ts > 1.0:
            _last_report_gen_ts = now
            try:
                subprocess.run(["python", os.path.join(BASE_DIR, "report.py")], cwd=BASE_DIR, timeout=25)
            except Exception:
                pass

    return items


def finalize_active_session():
    """
    B) Finalizar sessão ativa sem precisar do STOP do cubo.
    Fecha usando a última linha vista no /tail.
    """
    st = load_state()
    if not st.get("active_key") or not st.get("active_uid"):
        return {"ok": False, "err": "Nenhuma sessão ativa para finalizar."}

    last_line = to_int(st.get("last_seen_line"), 0)
    if last_line <= 0:
        return {"ok": False, "err": "Sem linha de log para finalizar (ainda não chegou evento no /live)."}

    # extrai ip e session do active_key
    try:
        ip, sess = st["active_key"].split("|", 1)
        sess = to_int(sess, -1)
    except Exception:
        ip = ""
        sess = -1

    user = st.get("active_user") or st.get("last_user") or "SEM_USER"
    uid = st.get("active_uid")

    append_jsonl(SESSION_MAP_JSONL, {
        "uid": uid,
        "src_ip": ip,
        "session": sess,
        "user": user,
        "start_line": 0,
        "stop_line": last_line
    })

    # limpa estado
    st["active_user"] = ""
    st["active_key"] = ""
    st["active_uid"] = ""
    st["start_line"] = 0
    save_state(st)

    # gera relatório
    gen = generate_report()
    gen["finalized_user"] = user
    return gen


LIVE_HTML = r"""<!doctype html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <meta http-equiv="Cache-Control" content="no-store, no-cache, must-revalidate, max-age=0"/>
  <title>Cubo UDP — Ao vivo</title>
  <style>
    :root{--bg:#0b1020;--text:#eaf0ff;--muted:#9aa4c3;--line:rgba(255,255,255,.08);
      --ok:#2ecc71;--err:#ff5c5c;--start:#4ea1ff;--stop:#f1c40f;}
    *{box-sizing:border-box}
    body{margin:0;font-family:system-ui;background:var(--bg);color:var(--text)}
    a{color:#8cc6ff;text-decoration:none} a:hover{text-decoration:underline}
    .wrap{max-width:1100px;margin:0 auto;padding:22px}
    header{display:flex;gap:12px;justify-content:space-between;align-items:flex-end;flex-wrap:wrap}
    h1{margin:0;font-size:22px}
    .topbar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;justify-content:space-between;
      margin:16px 0;padding:12px;border:1px solid var(--line);border-radius:14px;background:rgba(255,255,255,.04)}
    input{padding:8px 10px;border-radius:10px;border:1px solid var(--line);background:rgba(0,0,0,.25);color:var(--text);outline:none;min-width:260px}
    .btn{padding:8px 12px;border-radius:10px;border:1px solid var(--line);background:rgba(255,255,255,.06);color:var(--text);cursor:pointer;font-weight:900}
    .btn:hover{background:rgba(255,255,255,.10)}
    .hint{color:var(--muted);font-size:12px}
    .badge{display:inline-block;padding:2px 8px;border-radius:999px;border:1px solid var(--line);font-weight:900;font-size:12px}
    .card{background:rgba(18,26,51,.86);border:1px solid var(--line);border-radius:18px;padding:16px;margin-bottom:14px}
    table{width:100%;border-collapse:collapse;font-size:13px}
    th,td{border-bottom:1px solid var(--line);padding:8px 6px;text-align:left}
    th{color:var(--muted)}
  </style>
</head>
<body>
  <div class="wrap">
    <header>
      <div>
        <h1>Ao vivo — Cubo UDP</h1>
        <div class="hint"><a href="/reports/index.html">Relatórios por usuário</a></div>
      </div>
      <div class="hint" id="status">after=0</div>
    </header>

    <div class="topbar">
      <div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center">
        <span class="hint">Próximo usuário:</span>
        <input id="user" placeholder="Ex: JOAO / MARIA / DAVI" />
        <button class="btn" onclick="saveUser()">Salvar</button>
        <button class="btn" onclick="genReport()">Gerar relatório</button>
        <button class="btn" onclick="finalizeNow()">Finalizar sessão agora</button>
      </div>
      <div class="hint">
        Regra: o nome vale para o <b>próximo START</b>. No STOP, fecha e gera relatório. Se não digitar nada, usa o último.
      </div>
    </div>

    <div class="card">
      <div class="hint">
        Pendente: <span class="badge" id="pend">-</span>
        &nbsp;|&nbsp; Ativo: <span class="badge" id="act">-</span>
        &nbsp;|&nbsp; Último: <span class="badge" id="last">-</span>
      </div>
    </div>

    <section class="card">
      <div class="hint">Eventos recebidos (últimos 200)</div>
      <div style="margin-top:10px;max-height:520px;overflow:auto;border:1px solid var(--line);border-radius:14px">
        <table>
          <thead>
            <tr>
              <th>dt</th><th>event</th><th>user</th><th>session</th><th>modo</th><th>ok</th><th>err</th>
            </tr>
          </thead>
          <tbody id="rows"></tbody>
        </table>
      </div>
    </section>
  </div>

<script>
let afterLine = 0;
let items = [];
function esc(s){ s=(s===null||s===undefined)?"":String(s); return s.replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;"); }

async function saveUser(){
  const name = (document.getElementById("user").value||"").trim();
  if(!name){ alert("Digite um nome."); return; }
  const res = await fetch("/api/set_pending_user", {
    method:"POST", headers:{ "Content-Type":"application/json" },
    body: JSON.stringify({ user: name })
  });
  const j = await res.json();
  if(j.ok) alert("Ok! Próximo usuário salvo: " + j.user);
  else alert("Falha: " + (j.err||""));
}

async function genReport(){
  const res = await fetch("/api/generate_report", { method:"POST" });
  const j = await res.json();
  if(j.ok) alert(j.msg || "Relatório gerado.");
  else alert("Falha: " + (j.err||""));
}

async function finalizeNow(){
  const ok = confirm("Finalizar a sessão ativa agora? (use se esqueceram o STOP)");
  if(!ok) return;
  const res = await fetch("/api/finalize_active", { method:"POST" });
  const j = await res.json();
  if(j.ok) alert((j.msg||"Finalizado!") + (j.finalized_user?(" Usuário: "+j.finalized_user):""));
  else alert("Falha: " + (j.err||""));
}

async function updateState(){
  try{
    const res = await fetch("/api/state");
    const j = await res.json();
    if(j.ok){
      document.getElementById("pend").innerText = j.pending_user || "-";
      document.getElementById("act").innerText  = j.active_user  || "-";
      document.getElementById("last").innerText = j.last_user    || "-";
    }
  }catch(e){}
}

function renderRows(){
  const tbody = document.getElementById("rows");
  const last200 = items.slice(-200).reverse();
  tbody.innerHTML = last200.map(e=>{
    return `<tr>
      <td>${esc(e.dt||"")}</td>
      <td>${esc(e.event||"")}</td>
      <td>${esc(e.user||"SEM_USER")}</td>
      <td>${esc(e.session??"")}</td>
      <td>${esc(e.modo||"")}</td>
      <td>${esc(e.ok_total??"")}</td>
      <td>${esc(e.err_total??"")}</td>
    </tr>`;
  }).join("");
}

async function tick(){
  try{
    const res = await fetch(`/api/tail?after=${afterLine}`);
    const data = await res.json();
    if(data.ok){
      if(Array.isArray(data.items) && data.items.length){
        items.push(...data.items);
        afterLine = data.next_after || afterLine;
        document.getElementById("status").innerText = `after=${afterLine} | eventos=${items.length}`;
      }
      renderRows();
      updateState();
    }else{
      document.getElementById("status").innerText = "erro: "+(data.err||"");
    }
  }catch(e){
    document.getElementById("status").innerText = "erro: "+e;
  }
}
setInterval(tick, 1000);
setInterval(updateState, 1500);
updateState();
</script>
</body>
</html>
"""


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def _send_json(self, code, obj):
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()
        self.wfile.write(json.dumps(obj, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        if self.path.startswith("/live"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(LIVE_HTML.encode("utf-8"))
            return

        if self.path.startswith("/api/state"):
            st = load_state()
            self._send_json(200, {
                "ok": True,
                "pending_user": st.get("pending_user", ""),
                "active_user": st.get("active_user", ""),
                "last_user": st.get("last_user", "")
            })
            return

        if self.path.startswith("/api/tail"):
            qs = urllib.parse.urlparse(self.path).query
            params = urllib.parse.parse_qs(qs)
            after = 0
            try:
                after = int((params.get("after", ["0"])[0]))
            except Exception:
                after = 0

            data = read_jsonl_tail(after_line=after, max_lines=2000)
            if data.get("ok") and data.get("items"):
                data["items"] = apply_user_by_session(data["items"])

            self._send_json(200, data)
            return

        return super().do_GET()

    def do_POST(self):
        if self.path == "/api/set_pending_user":
            try:
                length = int(self.headers.get("Content-Length", "0") or "0")
                raw = self.rfile.read(length).decode("utf-8", errors="ignore")
                data = json.loads(raw) if raw else {}
                user = safe(data.get("user")).strip()
                if not user:
                    self._send_json(400, {"ok": False, "err": "user vazio"})
                    return

                # salva como “pendente”
                st = load_state()
                st["pending_user"] = user
                save_state(st)

                # NOVO: registra na lista de cadastrados
                add_known_user(user)

                self._send_json(200, {"ok": True, "user": user})
            except Exception as e:
                self._send_json(500, {"ok": False, "err": str(e)})
            return

        if self.path == "/api/generate_report":
            out = generate_report()
            self._send_json(200, out if isinstance(out, dict) else {"ok": False, "err": "erro"})
            return

        if self.path == "/api/finalize_active":
            out = finalize_active_session()
            self._send_json(200, out)
            return

        self._send_json(404, {"ok": False, "err": "not found"})


os.chdir(BASE_DIR)

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    os.makedirs(LOG_DIR, exist_ok=True)
    if not os.path.exists(STATE_JSON):
        save_json(STATE_JSON, default_state())
    if not os.path.exists(KNOWN_USERS_JSON):
        save_json(KNOWN_USERS_JSON, {"users": []})

    print(f"[HTTP] Servindo {BASE_DIR} em: http://127.0.0.1:{PORT}/")
    print(f"[HTTP] AO VIVO: http://127.0.0.1:{PORT}/live")
    print(f"[HTTP] Relatórios: http://127.0.0.1:{PORT}/reports/index.html")
    httpd.serve_forever()
