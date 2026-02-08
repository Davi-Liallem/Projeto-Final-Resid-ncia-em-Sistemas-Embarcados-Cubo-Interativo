import os
import json
import socket
from datetime import datetime

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(BASE_DIR, "logs")
os.makedirs(LOG_DIR, exist_ok=True)

JSONL_PATH = os.path.join(LOG_DIR, "udp_log.jsonl")

HOST = "0.0.0.0"
PORT = 5000  # precisa bater com LOCAL_SERVER_PORT no secrets.h

def now_dt():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def append_jsonl(obj: dict):
    with open(JSONL_PATH, "a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, PORT))
    print(f"[UDP] Escutando em {HOST}:{PORT}")
    print(f"[UDP] Gravando em: {JSONL_PATH}")

    while True:
        data, addr = sock.recvfrom(2048)
        src_ip, src_port = addr[0], addr[1]

        try:
            payload = data.decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        if not payload:
            continue

        # tenta entender JSON vindo do Pico
        try:
            obj = json.loads(payload)
        except Exception:
            obj = {"raw": payload}

        obj["dt"] = now_dt()
        obj["src_ip"] = src_ip
        obj["src_port"] = src_port

        append_jsonl(obj)

        # log simples
        ev = obj.get("event") or obj.get("raw") or "?"
        user = obj.get("user", "")
        sess = obj.get("session", "")
        modo = obj.get("modo", "")
        print(f"[UDP] {src_ip} ev={ev} user={user} session={sess} modo={modo}")

if __name__ == "__main__":
    main()
