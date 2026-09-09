# The original BF2 1.5 dedicated server for Linux, in a container.
#
# The binary is from 2009 and built for x86-64, so the image is amd64 too —
# on Apple Silicon Docker runs it through emulation.
#
# The game's files are not in the image: they are mounted from outside. That is
# both legally right and convenient — the image stays small.
FROM --platform=linux/amd64 debian:bullseye-slim

# What a 2009 binary is missing. libncurses5 is needed for the server's text
# console, the rest is an ordinary base.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libncurses5 libstdc++6 zlib1g libgcc-s1 \
 && rm -rf /var/lib/apt/lists/*

# The server does not need root.
RUN useradd --create-home --uid 1000 bf2
WORKDIR /server

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# 16567 is the game traffic, 29900 GameSpy, 4711 the remote console.
EXPOSE 16567/udp 29900/udp 4711/tcp

USER bf2
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
