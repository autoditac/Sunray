# Multi-stage build for Sunray Alfred firmware on RPi 4B (aarch64)
#
# Build:
#   docker buildx build --platform linux/arm64 --build-arg CONFIG_FILE=configs/robin.h -t sunray-robin .
#   docker buildx build --platform linux/arm64 --build-arg CONFIG_FILE=configs/batman.h -t sunray-batman .

# ---- Builder stage ----
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ make \
    libbluetooth-dev libssl-dev libjpeg-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/Sunray

COPY sunray/ sunray/
COPY linux/ linux/
COPY configs/ configs/

ARG CONFIG_FILE=linux/config_alfred.h

RUN cd linux \
    && mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DCONFIG_FILE=/build/Sunray/${CONFIG_FILE} .. \
    && make -j$(nproc)

# ---- Runtime stage ----
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbluetooth3 libssl3 libjpeg62-turbo \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/Sunray/linux/build/sunray /opt/sunray/sunray
COPY docker-entrypoint.sh /opt/sunray/

RUN chmod +x /opt/sunray/docker-entrypoint.sh

WORKDIR /opt/sunray

# HTTP API port (for CaSSAndRA)
EXPOSE 80

ENTRYPOINT ["/opt/sunray/docker-entrypoint.sh"]
