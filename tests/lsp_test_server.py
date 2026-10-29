import json
import sys


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        name, value = line.decode("ascii").split(":", 1)
        headers[name.strip().lower()] = value.strip()
    size = int(headers["content-length"])
    return json.loads(sys.stdin.buffer.read(size).decode("utf-8"))


def send_message(message, extra_header=False):
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    if extra_header:
        sys.stdout.buffer.write(b"X-Axyne-Test: framing\r\n")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


initialize = read_message()
if initialize is None or initialize.get("method") != "initialize":
    sys.exit(2)
capabilities = initialize.get("params", {}).get("capabilities", {}).get("textDocument", {})
if (capabilities.get("completion", {}).get("completionItem", {}).get("snippetSupport")
        is not False or "definition" not in capabilities):
    sys.exit(3)
send_message({
    "jsonrpc": "2.0",
    "id": initialize["id"],
    "result": {"capabilities": {}},
}, extra_header=True)

while True:
    message = read_message()
    if message is None:
        break
    if message.get("method") != "textDocument/completion":
        continue
    params = message.get("params", {})
    position = params.get("position", {})
    uri = params.get("textDocument", {}).get("uri", "")
    if (not uri.endswith("/probe.c") or position.get("line") != 0
            or position.get("character") != 7):
        sys.exit(4)
    send_message({
        "jsonrpc": "2.0",
        "id": message["id"],
        "result": {"isIncomplete": False, "items": [{"label": "probeCompletion", "kind": 3}]},
    })
    break
