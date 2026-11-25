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


def pos(line, character):
    return {"line": line, "character": character}


def rng(sl, sc, el, ec):
    return {"start": pos(sl, sc), "end": pos(el, ec)}


initialize = read_message()
send({"jsonrpc": "2.0", "id": initialize["id"], "result": {"capabilities": {}}})

while True:
    message = read_message()
    if message is None:
        break
    method = message.get("method")
    ident = message.get("id")
    params = message.get("params", {})
    uri = params.get("textDocument", {}).get("uri", "")
    if method == "textDocument/didOpen":
        # The code action request must carry this diagnostic (numeric code kept).
        send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
              "params": {"uri": uri, "diagnostics": [
                  {"range": rng(0, 0, 0, 5), "severity": 1, "code": 2304,
                   "source": "ts", "message": "Cannot find name"},
                  {"range": rng(5, 0, 5, 1), "severity": 2, "code": 1, "message": "far away"}]}})
        continue
    if ident is None:
        continue
    if method == "textDocument/hover":
        result = {"contents": {"kind": "markdown",
                               "value": "```ts\nconst version: string\n```\nNode **version**"},
                  "range": rng(0, 6, 0, 13)}
    elif method == "textDocument/signatureHelp":
        result = {"signatures": [
            {"label": "other()", "parameters": []},
            {"label": "f(a: number, b: string)", "documentation": {"kind": "plaintext", "value": "Does f"},
             "parameters": [{"label": "a: number"}, {"label": [13, 22]}]}],
            "activeSignature": 1, "activeParameter": 1}
    elif method == "textDocument/rename":
        if params.get("newName") == "bad name":
            send({"jsonrpc": "2.0", "id": ident,
                  "error": {"code": -32602, "message": "Cannot rename"}})
            continue
        result = {"changes": {uri: [{"range": rng(0, 6, 0, 13), "newText": params["newName"]}]}}
    elif method == "textDocument/codeAction":
        diagnostics = params.get("context", {}).get("diagnostics", [])
        title = "Add import" if (len(diagnostics) == 1 and diagnostics[0].get("code") == 2304) \
            else "NO DIAGNOSTICS"
        result = [
            {"title": title, "kind": "quickfix", "isPreferred": True,
             "edit": {"documentChanges": [{"textDocument": {"uri": uri, "version": 1},
                                           "edits": [{"range": rng(0, 0, 0, 0),
                                                      "newText": "import x\n"}]}]}},
            {"title": "Lazy refactor", "kind": "refactor", "data": {"id": 7}},
            {"title": "Run command", "command": "do.it", "arguments": []}]
    elif method == "textDocument/formatting":
        options = params.get("options", {})
        result = [{"range": rng(1, 0, 1, 3),
                   "newText": "  " if options.get("insertSpaces") else "\t"},
                  {"range": rng(0, 17, 0, 17), "newText": str(options.get("tabSize"))}]
    elif method == "textDocument/documentSymbol":
        result = [{"name": "Foo", "kind": 5, "range": rng(1, 0, 4, 1),
                   "selectionRange": rng(1, 6, 1, 9),
                   "children": [{"name": "bar", "kind": 6, "range": rng(2, 2, 3, 3),
                                 "selectionRange": rng(2, 2, 2, 5)}]},
                  {"name": "top", "kind": 12, "location": {"uri": uri, "range": rng(0, 6, 0, 9)},
                   "containerName": "mod"}]
    elif method == "completionItem/resolve":
        result = dict(params, documentation={"kind": "markdown", "value": "resolved docs"},
                      additionalTextEdits=[{"range": rng(0, 0, 0, 0), "newText": "import y\n"}])
    elif method == "codeAction/resolve":
        result = dict(params, edit={"changes": {"file:///r.js": [
            {"range": rng(0, 0, 0, 1), "newText": "R"}]}})
    else:
        send({"jsonrpc": "2.0", "id": ident,
              "error": {"code": -32601, "message": "Method not found"}})
        continue
    send({"jsonrpc": "2.0", "id": ident, "result": result})
