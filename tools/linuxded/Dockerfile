# Оригінальний виділений сервер BF2 1.5 під Linux у контейнері.
#
# Двійковий файл 2009 року й зібраний під x86-64, тож образ теж amd64 —
# на Apple Silicon Docker запускає його через емуляцію.
#
# Самих файлів гри в образі немає: вони монтуються ззовні. Це і законно
# правильно, і зручно — образ лишається маленьким.
FROM --platform=linux/amd64 debian:bullseye-slim

# Чого бракує двійковому файлу з 2009-го. libncurses5 потрібен для
# текстової консолі сервера, решта — звичайна база.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libncurses5 libstdc++6 zlib1g libgcc-s1 \
 && rm -rf /var/lib/apt/lists/*

# Сервер не потребує root.
RUN useradd --create-home --uid 1000 bf2
WORKDIR /server

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# 16567 — ігровий трафік, 29900 — GameSpy, 4711 — віддалена консоль.
EXPOSE 16567/udp 29900/udp 4711/tcp

USER bf2
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
