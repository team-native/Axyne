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
    return json.loads(sys.stdin.buffer.read(int(headers["content-length"])).decode("utf-8"))


def send(message):
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


initialize = read_message()
send({"jsonrpc": "2.0", "id": initialize["id"], "result": {"capabilities": {}}})

# Like a busy tsserver: flood the client with output before reading anything
# more. The client must keep draining stdout while it is writing big documents
# to our stdin, or both sides block forever.
filler = "x" * 4000
for index in range(400):
    send({"jsonrpc": "2.0", "method": "window/logMessage",
          "params": {"type": 4, "message": f"{index} {filler}"}})

while True:
    message = read_message()
    if message is None:
        break
    if message.get("method") == "textDocument/hover":
        send({"jsonrpc": "2.0", "id": message["id"],
              "result": {"contents": "flood survived"}})
        break
