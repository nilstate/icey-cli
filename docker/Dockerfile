FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ARG ICEY_SERVER_BINARY=icey-cli/build-dev/src/server/icey-server

RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  ffmpeg \
  libssl3t64 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN mkdir -p /app/media /app/recordings

COPY ${ICEY_SERVER_BINARY} /usr/local/bin/icey-server
COPY icey-cli/web/dist /app/web
COPY icey/data/test.mp4 /app/media/test.mp4
COPY icey-cli/docker/entrypoint.sh /app/entrypoint.sh

RUN chmod +x /app/entrypoint.sh

EXPOSE 4500 3478/tcp 3478/udp

ENTRYPOINT ["/app/entrypoint.sh"]
