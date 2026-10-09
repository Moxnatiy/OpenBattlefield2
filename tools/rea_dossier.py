#!/usr/bin/env python3
"""Read a REA function dossier without pulling all of it into view.

    tools/rea_dossier.py <dossier.json> [--code] [--grep REGEX] [--context N]

A REA `analyze_function` answer is the whole function — and for a big one that
is megabytes, mostly its value flow. This prints what a first look needs: the
evidence ID, the name and body, who calls it, what it calls, the strings and
names it touches, and the size of the pseudocode. `--code` adds the
pseudocode; `--grep` prints only the pseudocode lines that match, each with
`--context` lines around it (default 3).

Takes either shape REA writes: the MCP tool's `{result, evidence_id, ...}` or
the CLI's Evidence record `{evidence_id, raw_result, ...}`.
"""
import argparse
import json
import re
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("dossier")
    parser.add_argument("--code", action="store_true")
    parser.add_argument("--grep")
    parser.add_argument("--context", type=int, default=3)
    args = parser.parse_args()

    data = json.load(open(args.dossier))
    result = data.get("result") or data.get("raw_result") or data
    evidence = data.get("evidence_id")
    if isinstance(result, dict) and "items" in result:
        # A `batch_decompile` answer: one pseudocode per address.
        print(f"evidence  {evidence}  ({result.get('succeeded')} of {result.get('total')})")
        for item in result["items"]:
            print(f"==== {item.get('address') or item.get('procedure')}  {item.get('status', '')}")
            code = item.get("pseudocode") or item.get("result") or item.get("error")
            print(code if isinstance(code, str) else json.dumps(code)[:2000])
        return
    proc = result.get("procedure", {})
    body = proc.get("body", {})
    print(f"evidence  {evidence}")
    print(f"function  {proc.get('name')} @ {proc.get('address')}  {proc.get('signature')}")
    ranges = ", ".join(f"{r['start']}-{r['end']}" for r in body.get("ranges", []))
    print(f"body      {body.get('total_bytes')} bytes: {ranges}")

    def names(items, key="name"):
        out = []
        for item in items:
            name = item.get(key) or item.get("name") or "?"
            out.append(f"{name}@{item.get('address', '?')}")
        return out

    callers = result.get("callers", [])
    callees = result.get("callees", [])
    print(f"callers   {len(callers)}: " + " ".join(names(callers)[:40]))
    print(f"callees   {len(callees)}: " + " ".join(names(callees)[:60]))
    strings = result.get("referenced_strings", [])
    print(f"strings   {len(strings)}")
    for s in strings[:60]:
        print(f"  {s.get('source_address')}  {s.get('address')}  {s.get('value')!r}")
    code = result.get("pseudocode") or ""
    lines = code.split("\n")
    print(f"pseudocode {len(lines)} lines")
    if args.code:
        print(code)
    elif args.grep:
        pattern = re.compile(args.grep)
        shown = set()
        for i, line in enumerate(lines):
            if pattern.search(line):
                for j in range(max(0, i - args.context), min(len(lines), i + args.context + 1)):
                    if j not in shown:
                        print(f"{j:5d}  {lines[j]}")
                        shown.add(j)
                print("   --")
    for limitation in (result.get("limitations") or [])[:5]:
        print(f"limit     {limitation}", file=sys.stderr)


if __name__ == "__main__":
    main()
