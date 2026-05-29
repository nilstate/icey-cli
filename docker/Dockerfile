FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ARG ICEY_SERVER_BINARY=icey-cli/build-dev/src/server/icey-server
ARG ICEY_WEB_ROOT=icey-cli/web/dist
ARG ICEY_SAMPLE_MEDIA=icey/data/test.mp4
ARG ICEY_ENTRYPOINT=icey-cli/docker/entrypoint.sh

RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  ffmpeg \
  libssl3t64 \
  && rm -rf /var/lib/apt/lists/*

RUN useradd --system --create-home --home-dir /home/icey --shell /usr/sbin/nologin icey

WORKDIR /app

RUN mkdir -p /app/media /app/recordings && chown -R icey:icey /app

COPY --chown=icey:icey ${ICEY_SERVER_BINARY} /usr/local/bin/icey-server
COPY --chown=icey:icey ${ICEY_WEB_ROOT} /app/web
COPY --chown=icey:icey ${ICEY_SAMPLE_MEDIA} /app/media/test.mp4
COPY --chown=icey:icey ${ICEY_ENTRYPOINT} /app/entrypoint.sh

RUN chmod +x /app/entrypoint.sh

EXPOSE 4500 3478/tcp 3478/udp

USER icey

ENTRYPOINT ["/app/entrypoint.sh"]
