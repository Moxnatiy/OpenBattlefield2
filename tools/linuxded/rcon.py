#!/usr/bin/env python3
"""The remote console of an original BF2 server.

The protocol is simple: the server sends `### Digest seed: <seed>`, the answer is
`login <md5(seed + password)>`, then commands through `exec`.

    python tools/linuxded/rcon.py "game.listPlayers"
"""
import hashlib
import socket
import sys


def rcon(commands, host="127.0.0.1", port=4711, password="openbf2", timeout=10.0):
    sock = socket.create_connection((host, port), timeout)
    sock.settimeout(timeout)

    # The greeting and the seed may arrive as two separate messages.
    marker = "### Digest seed: "
    greeting = ""
    for _ in range(4):
        try:
            chunk = sock.recv(4096).decode("latin-1", "replace")
        except socket.timeout:
            break
        if not chunk:
            break
        greeting += chunk
        if marker in greeting:
            break

    if marker in greeting:
        # The standard way: the server gives a seed, we answer with a hash.
        seed = greeting.split(marker, 1)[1].split("\n", 1)[0].strip()
        secret = hashlib.md5((seed + password).encode("latin-1")).hexdigest()
    else:
        # The "default" script is simpler: the password goes as it is.
        secret = password
    sock.sendall(("login " + secret + "\n").encode("latin-1"))
    answer = ""
    for _ in range(4):
        try:
            chunk = sock.recv(4096).decode("latin-1", "replace")
        except socket.timeout:
            break
        if not chunk:
            break
        answer += chunk
        if "successful" in answer.lower() or "failed" in answer.lower():
            break
    if "successful" not in answer.lower():
        return "login failed: " + answer.strip()

    out = []
    for command in commands:
        sock.sendall(("exec " + command + "\n").encode("latin-1"))
        try:
            reply = sock.recv(65536).decode("latin-1", "replace").strip()
        except socket.timeout:
            reply = "(no answer)"
        # The engine itself says when a command does not exist — the same as we do.
        out.append("%-30s -> %s" % (command, reply))
    sock.close()
    return "\n".join(out)


if __name__ == "__main__":
    print(rcon(sys.argv[1:] or ["game.serverName"]))
