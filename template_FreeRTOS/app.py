from flask import Flask, request, jsonify
import sqlite3, time

app = Flask(__name__)
DB = "cubo.db"

def db():
    con = sqlite3.connect(DB)
    con.row_factory = sqlite3.Row
    return con

def init_db():
    con = db()
    cur = con.cursor()
    cur.execute("""
    CREATE TABLE IF NOT EXISTS eventos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ts INTEGER,
        user TEXT,
        session TEXT,
        modo TEXT,
        event TEXT,
        last_ms INTEGER,
        avg_ms INTEGER,
        ok_total INTEGER,
        err_total INTEGER
    )
    """)
    con.commit()
    con.close()

@app.post("/evento")
def evento():
    data = request.get_json(force=True, silent=True) or {}
    con = db()
    cur = con.cursor()
    cur.execute("""
        INSERT INTO eventos(ts,user,session,modo,event,last_ms,avg_ms,ok_total,err_total)
        VALUES(?,?,?,?,?,?,?,?,?)
    """, (
        int(time.time()),
        data.get("user",""),
        data.get("session",""),
        data.get("modo",""),
        data.get("event",""),
        int(data.get("last_ms",0) or 0),
        int(data.get("avg_ms",0) or 0),
        int(data.get("ok_total",0) or 0),
        int(data.get("err_total",0) or 0),
    ))
    con.commit()
    con.close()
    return jsonify({"ok": True})

@app.get("/")
def home():
    con = db()
    cur = con.cursor()

    users = cur.execute("SELECT DISTINCT user FROM eventos WHERE user<>'' ORDER BY user").fetchall()
    html = ["<h2>Cubo - Relatórios (Local)</h2>"]
    html.append("<p>Usuários:</p><ul>")
    for u in users:
        html.append(f"<li><a href='/relatorio?user={u['user']}'>{u['user']}</a></li>")
    html.append("</ul>")
    html.append("<p>Dica: /relatorio?user=PAC_001</p>")
    con.close()
    return "\n".join(html)

@app.get("/relatorio")
def relatorio():
    user = request.args.get("user","")
    con = db()
    cur = con.cursor()

    sessions = cur.execute("""
        SELECT session,
               SUM(CASE WHEN event='OK' THEN 1 ELSE 0 END) AS ok_count,
               SUM(CASE WHEN event='ERR' THEN 1 ELSE 0 END) AS err_count,
               MAX(avg_ms) AS avg_ms,
               MAX(ok_total) AS ok_total,
               MAX(err_total) AS err_total,
               MIN(ts) AS ts_start,
               MAX(ts) AS ts_end
        FROM eventos
        WHERE user=?
        GROUP BY session
        ORDER BY ts_start DESC
    """, (user,)).fetchall()

    html = [f"<h2>Relatório: {user}</h2><a href='/'>Voltar</a><hr>"]
    if not sessions:
        html.append("<p>Sem sessões.</p>")
    else:
        html.append("<table border='1' cellpadding='6' cellspacing='0'>")
        html.append("<tr><th>Sessão</th><th>OK</th><th>ERR</th><th>avg_ms</th><th>Início</th><th>Fim</th></tr>")
        for s in sessions:
            html.append(
                f"<tr><td>{s['session']}</td>"
                f"<td>{s['ok_count'] or 0}</td>"
                f"<td>{s['err_count'] or 0}</td>"
                f"<td>{s['avg_ms'] or 0}</td>"
                f"<td>{s['ts_start']}</td>"
                f"<td>{s['ts_end']}</td></tr>"
            )
        html.append("</table>")

    con.close()
    return "\n".join(html)

if __name__ == "__main__":
    init_db()
    # 0.0.0.0 pra abrir na rede local (celular/PC)
    app.run(host="0.0.0.0", port=5000, debug=False)
