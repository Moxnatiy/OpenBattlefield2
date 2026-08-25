#!/usr/bin/env python3
"""Витягує офіційні описи команд .con із редактора BF2.

`bf2editor/Help/CommandDescriptions.dat` — UTF-16LE, рядки розділені NUL і
йдуть парами «команда, опис». Це документація від самої DICE, тому вона
цінніша за будь-які здогадки: показує, що команда робить, а не лише те,
що вона існує.

    python3 tools/extract_command_descriptions.py \
        "Game Files/OtherFiles/bf2editor_and_tools/bf2editor/Help/CommandDescriptions.dat" \
        docs/reference/con-command-descriptions.txt
"""
import sys


def extract(path: str):
    with open(path, "rb") as handle:
        raw = handle.read()

    text = raw.decode("utf-16-le", errors="replace")
    parts = [part.strip() for part in text.split("\0")]
    parts = [part for part in parts if part]

    # Перші два рядки — службовий заголовок ("LANGUAGE", "Description").
    if len(parts) >= 2 and parts[0].upper() == "LANGUAGE":
        parts = parts[2:]

    # Не йдемо строго через один: у файлі трапляються порожні описи, і
    # від них увесь подальший потік зсувається назавжди. Натомість шукаємо
    # те, що виглядає як команда ("ціль.метод" без пробілів), і беремо
    # наступний рядок за опис — так розбір сам себе синхронізує.
    def looks_like_command(value: str) -> bool:
        return "." in value and " " not in value and not value.endswith(".")

    pairs = []
    index = 0
    while index < len(parts) - 1:
        if looks_like_command(parts[index]) and not looks_like_command(parts[index + 1]):
            pairs.append((parts[index], parts[index + 1]))
            index += 2
        else:
            index += 1
    return pairs


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    pairs = extract(sys.argv[1])
    pairs.sort(key=lambda item: item[0].lower())

    with open(sys.argv[2], "w", encoding="utf-8") as out:
        out.write("# Офіційні описи команд .con із редактора BF2\n")
        out.write("# Джерело: bf2editor/Help/CommandDescriptions.dat\n")
        out.write(f"# Команд: {len(pairs)}\n")
        for command, description in pairs:
            out.write(f"{command}\t{description}\n")

    print(f"команд: {len(pairs)} -> {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
