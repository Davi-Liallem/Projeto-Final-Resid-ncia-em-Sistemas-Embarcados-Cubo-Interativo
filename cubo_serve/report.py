import os
import json
import re
from datetime import datetime
from collections import defaultdict

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(BASE_DIR, "logs")

JSONL_PATH = os.path.join(LOG_DIR, "udp_log.jsonl")
SESSION_MAP_JSONL = os.path.join(LOG_DIR, "session_user_map.jsonl")
KNOWN_USERS_JSON = os.path.join(LOG_DIR, "known_users.json")

OUT_DIR = os.path.join(BASE_DIR, "reports")
USERS_DIR = os.path.join(OUT_DIR, "users")
INDEX_HTML = os.path.join(OUT_DIR, "index.html")

EVENTS_OK = {"start", "ok", "err", "stop"}
EVENTS_MIC = {"telemetry"}  # se existir

def now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def safe(v):
    return "" if v is None else str(v)

def to_int(v, default=0):
    try:
        return int(v)
    except Exception:
        return default

def slugify(name: str) -> str:
    s = safe(name).strip() or "SEM_USER"
    s = s.lower()
    s = re.sub(r"[^a-z0-9\-_]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s or "sem_user"

def fmt_ms(ms):
    ms = to_int(ms, 0)
    if ms <= 0:
        return "-"
    if ms < 1000:
        return f"{ms} ms"
    return f"{ms/1000:.2f} s"

def html_escape(s):
    return ("" if s is None else str(s)).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def load_json(path, default):
    if not os.path.exists(path):
        return default
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default

def load_jsonl(path):
    items = []
    if not os.path.exists(path):
        return items
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                items.append(json.loads(line))
            except Exception:
                pass
    return items

def load_session_user_map():
    mp = {}
    if not os.path.exists(SESSION_MAP_JSONL):
        return mp
    with open(SESSION_MAP_JSONL, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception:
                continue
            ip = safe(obj.get("src_ip")).strip()
            sid = to_int(obj.get("session"), -1)
            user = safe(obj.get("user")).strip()
            if ip and sid >= 0 and user:
                mp[(ip, sid)] = user
    return mp

def session_key(e):
    user = safe(e.get("user")).strip() or "SEM_USER"
    session = to_int(e.get("session"), -1)
    src_ip = safe(e.get("src_ip")).strip()
    return (user, src_ip, session)

def summarize_session(events):
    start = next((x for x in events if x.get("event") == "start"), None)
    stop  = next((x for x in reversed(events) if x.get("event") == "stop"), None)

    ok_total = to_int(stop.get("ok_total"), 0) if stop else 0
    err_total = to_int(stop.get("err_total"), 0) if stop else 0

    if (not stop) or (ok_total == 0 and err_total == 0):
        ok_total = sum(1 for x in events if safe(x.get("event")).lower() == "ok")
        err_total = sum(1 for x in events if safe(x.get("event")).lower() == "err")

    duration_ms = to_int(stop.get("total_ms"), 0) if stop else 0

    started_dt = safe(start.get("dt")) if start else "-"
    ended_dt = safe(stop.get("dt")) if stop else "-"
    sort_dt = ended_dt if ended_dt and ended_dt != "-" else started_dt

    mic_freqs = []
    mic_ints = []
    last_mic = {"freq": None, "int": None, "type": None}

    for x in events:
        mf = x.get("mic_freq")
        mi = x.get("mic_int")
        mt = x.get("mic_type")

        try:
            if mf is not None:
                v = float(mf)
                mic_freqs.append(v)
                last_mic["freq"] = v
        except Exception:
            pass

        try:
            if mi is not None:
                v = float(mi)
                mic_ints.append(v)
                last_mic["int"] = v
        except Exception:
            pass

        try:
            if mt is not None:
                last_mic["type"] = int(mt)
        except Exception:
            pass

    estado = "NÃO INFORMADO"
    if mic_freqs:
        avg = sum(mic_freqs)/len(mic_freqs)
        if avg < 80:
            estado = "CALMO"
        elif avg < 150:
            estado = "NORMAL"
        else:
            estado = "AGITADO"

    mic = {
        "count": len(mic_freqs),
        "avg_hz": (sum(mic_freqs)/len(mic_freqs)) if mic_freqs else None,
        "min_hz": min(mic_freqs) if mic_freqs else None,
        "max_hz": max(mic_freqs) if mic_freqs else None,
        "avg_int": (sum(mic_ints)/len(mic_ints)) if mic_ints else None,
        "min_int": min(mic_ints) if mic_ints else None,
        "max_int": max(mic_ints) if mic_ints else None,
        "last": last_mic,
        "estado": estado,
    }

    by_level = defaultdict(lambda: {"ok": 0, "err": 0})
    for x in events:
        ev = safe(x.get("event")).lower()
        if ev not in ("ok", "err"):
            continue
        lvl = safe(x.get("modo")).strip() or safe(x.get("level")).strip() or "NÃO INFORMADO"
        if ev == "ok":
            by_level[lvl]["ok"] += 1
        else:
            by_level[lvl]["err"] += 1

    main_modo = (safe(start.get("modo")).strip() if start else (safe(events[-1].get("modo")).strip() if events else ""))

    return {
        "started_dt": started_dt or "-",
        "ended_dt": ended_dt or "-",
        "duration_ms": duration_ms,
        "ok_total": ok_total,
        "err_total": err_total,
        "has_stop": bool(stop),
        "sort_dt": sort_dt or "",
        "mic": mic,
        "by_level": dict(by_level),
        "main_modo": main_modo,
    }

def build_index(users, generated_dt):
    rows = []
    for u in users:
        rows.append(f"""
        <tr>
          <td><a href="users/{html_escape(u['slug'])}.html">{html_escape(u['user'])}</a></td>
          <td>{html_escape(u['last_dt'] or "-")}</td>
          <td>{u['count']}</td>
        </tr>
        """)

    return f"""<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8"/>
<meta http-equiv="Cache-Control" content="no-store, no-cache, must-revalidate, max-age=0"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Relatórios — Cubo</title>
<style>
  body{{margin:0;font-family:system-ui;background:#0b1020;color:#eaf0ff}}
  a{{color:#8cc6ff;text-decoration:none}} a:hover{{text-decoration:underline}}
  .wrap{{max-width:900px;margin:0 auto;padding:22px}}
  .hint{{color:#9aa4c3;font-size:12px}}
  table{{width:100%;border-collapse:collapse;margin-top:14px}}
  th,td{{padding:10px;border-bottom:1px solid rgba(255,255,255,.08);text-align:left}}
  th{{color:#9aa4c3;font-size:12px;text-transform:uppercase;letter-spacing:.08em}}
  .top{{display:flex;justify-content:space-between;flex-wrap:wrap;gap:10px;align-items:flex-end}}
  .btn{{display:inline-block;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);
        border-radius:999px;padding:8px 12px;font-size:12px}}
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div>
      <h2 style="margin:0">Relatórios</h2>
      <div class="hint"><a class="btn" href="/live">Ao vivo</a></div>
    </div>
    <div class="hint">Gerado em: {html_escape(generated_dt)}</div>
  </div>

  <table>
    <thead><tr><th>Usuário</th><th>Última atividade</th><th>Sessões</th></tr></thead>
    <tbody>
      {''.join(rows) if rows else "<tr><td colspan='3' class='hint'>Sem dados ainda.</td></tr>"}
    </tbody>
  </table>

</div>
</body>
</html>
"""

def build_user_page(user, sessions, generated_dt):
    slug = slugify(user)
    cards = []
    for s in sessions:
        (_, ip, sid) = s["key"]
        summ = s["summ"]
        modo = summ.get("main_modo","")
        warn = "" if summ["has_stop"] else "<span style='color:#ffcc66;font-weight:900'>⚠ sem STOP</span>"
        cards.append(f"""
        <div style="background:rgba(18,26,51,.86);border:1px solid rgba(255,255,255,.08);border-radius:18px;padding:16px;margin:12px 0">
          <div style="font-weight:900;font-size:18px">Sessão {sid} — {html_escape(modo or "-")} {warn}</div>
          <div style="color:#9aa4c3;font-size:12px">IP: {html_escape(ip)}</div>

          <div style="display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:10px;margin-top:10px">
            <div><div style="color:#9aa4c3;font-size:12px">Início</div><div>{html_escape(summ["started_dt"])}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">Fim</div><div>{html_escape(summ["ended_dt"])}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">Duração</div><div>{html_escape(fmt_ms(summ["duration_ms"]))}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">OK</div><div>{summ["ok_total"]}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">ERR</div><div>{summ["err_total"]}</div></div>
          </div>

          <div style="display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:10px">
            <div><div style="color:#9aa4c3;font-size:12px">Microfone</div>
              <div style="font-weight:800">{html_escape(summ.get("mic",{}).get("estado","NÃO INFORMADO"))}</div>
              <div style="color:#9aa4c3;font-size:12px">leituras: {to_int(summ.get("mic",{}).get("count"),0)}</div>
            </div>
            <div><div style="color:#9aa4c3;font-size:12px">Freq média</div><div>{html_escape(("-" if not summ.get("mic",{}).get("avg_hz") else f"{summ['mic']['avg_hz']:.1f} Hz"))}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">Freq mín</div><div>{html_escape(("-" if not summ.get("mic",{}).get("min_hz") else f"{summ['mic']['min_hz']:.1f} Hz"))}</div></div>
            <div><div style="color:#9aa4c3;font-size:12px">Freq máx</div><div>{html_escape(("-" if not summ.get("mic",{}).get("max_hz") else f"{summ['mic']['max_hz']:.1f} Hz"))}</div></div>
          </div>

          <div style="margin-top:10px">
            <div style="color:#9aa4c3;font-size:12px;margin-bottom:6px">Acertos/Erros por nível (na sessão)</div>
            <div style="display:flex;flex-wrap:wrap;gap:8px">
              {''.join([f"<span style='background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);border-radius:999px;padding:6px 10px;font-size:12px'>{html_escape(k)}: <b>{v['ok']}</b> ok | <b>{v['err']}</b> err</span>" for k,v in (summ.get("by_level") or {}).items()]) or "<span class='hint'>-</span>"}
            </div>
          </div>

        </div>
        """)

    body = ''.join(cards) if cards else "<div class='hint'>Sem sessões ainda para este usuário.</div>"

    return slug, f"""<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8"/>
<meta http-equiv="Cache-Control" content="no-store, no-cache, must-revalidate, max-age=0"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Relatório — {html_escape(user)}</title>
<style>
  body{{margin:0;font-family:system-ui;background:#0b1020;color:#eaf0ff}}
  a{{color:#8cc6ff;text-decoration:none}} a:hover{{text-decoration:underline}}
  .wrap{{max-width:1100px;margin:0 auto;padding:22px}}
  .hint{{color:#9aa4c3;font-size:12px}}
</style>
</head>
<body>
<div class="wrap">
  <div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:10px;align-items:flex-end">
    <div>
      <h2 style="margin:0">Usuário: {html_escape(user)}</h2>
      <div class="hint"><a href="../index.html">← Voltar</a> | <a href="/live">Ao vivo</a></div>
    </div>
    <div class="hint">Gerado em: {html_escape(generated_dt)}</div>
  </div>

  {body}

</div>
</body>
</html>
"""

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(USERS_DIR, exist_ok=True)

    raw = load_jsonl(JSONL_PATH)
    mp = load_session_user_map()

    evs = []
    for e in raw:
        ev = safe(e.get("event")).lower()
        if ev not in EVENTS_OK and ev not in EVENTS_MIC:
            continue

        ip = safe(e.get("src_ip")).strip()
        sid = to_int(e.get("session"), -1)
        u = safe(e.get("user")).strip()

        # troca user vazio OU "SEM_USER" pelo mapeado do /live
        if ((not u) or (u == "SEM_USER")) and ip and sid >= 0 and (ip, sid) in mp:
            e["user"] = mp[(ip, sid)]

        uu = safe(e.get("user")).strip()
        if not uu:
            e["user"] = "SEM_USER"

        evs.append(e)

    grouped = defaultdict(list)
    for e in evs:
        grouped[session_key(e)].append(e)

    for k in list(grouped.keys()):
        grouped[k].sort(key=lambda x: (to_int(x.get("ts"), 0), safe(x.get("dt"))))

    sessions = []
    for k, items in grouped.items():
        sessions.append({"key": k, "events": items, "summ": summarize_session(items)})

    by_user = defaultdict(list)
    for s in sessions:
        user = s["key"][0]
        by_user[user].append(s)

    for u in by_user:
        by_user[u].sort(key=lambda s: s["summ"]["sort_dt"], reverse=True)

    known = load_json(KNOWN_USERS_JSON, {"users": []}).get("users", [])
    known = [safe(u).strip() for u in known if safe(u).strip()]

    gen = now_str()
    all_users = set(by_user.keys())
    for u in known:
        all_users.add(u)

    users_list = []
    for user in sorted(all_users, key=lambda x: x.lower()):
        sessions_u = by_user.get(user, [])
        slug, html_u = build_user_page(user, sessions_u, gen)
        with open(os.path.join(USERS_DIR, f"{slug}.html"), "w", encoding="utf-8") as f:
            f.write(html_u)

        last_dt = sessions_u[0]["summ"]["sort_dt"] if sessions_u else ""
        users_list.append({"user": user, "slug": slug, "count": len(sessions_u), "last_dt": last_dt})

    users_list.sort(key=lambda u: u.get("last_dt", ""), reverse=True)

    with open(INDEX_HTML, "w", encoding="utf-8") as f:
        f.write(build_index(users_list, gen))

    print("[OK] Relatório gerado:", INDEX_HTML)

if __name__ == "__main__":
    main()
