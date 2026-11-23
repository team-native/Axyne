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
params = initialize.get("params", {})
for feature in ("hover", "signatureHelp", "rename", "codeAction", "formatting", "documentSymbol"):
    if feature not in capabilities:
        sys.exit(6)
if params.get("rootUri") is not None and not params.get("workspaceFolders"):
    sys.exit(7)
send_message({
    "jsonrpc": "2.0",
    "id": initialize["id"],
    "result": {"capabilities": {}},
}, extra_header=True)

# Requests the server sends to the client must be answered (string ids too).
send_message({"jsonrpc": "2.0", "id": "cfg-1", "method": "workspace/configuration",
              "params": {"items": [{"section": "a"}, {"section": "b"}]}})
send_message({"jsonrpc": "2.0", "id": 9, "method": "client/registerCapability",
              "params": {"registrations": []}})
send_message({"jsonrpc": "2.0", "id": 10, "method": "no/suchMethod", "params": {}})
expected = {"cfg-1": ("result", [None, None]), 9: ("result", None),
            10: ("error", None)}
pending = []
while expected:
    reply = read_message()
    if reply is None:
        sys.exit(5)
    key = reply.get("id")
    if "method" in reply or key not in expected:
        pending.append(reply)
        continue
    kind, value = expected.pop(key)
    if kind == "result" and reply.get("result") != value:
        sys.exit(5)
    if kind == "error" and reply.get("error", {}).get("code") != -32601:
        sys.exit(5)

while True:
    message = pending.pop(0) if pending else read_message()
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
