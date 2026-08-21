# Multi-stage lightweight build for Needlefish Search Engine REST API Server
FROM alpine:3.19 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    linux-headers \
    zlib-dev

WORKDIR /app
COPY . .

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DBUILD_FUZZERS=OFF

RUN cmake --build build --target needlefish

# Minimal deployment stage
FROM alpine:3.19

RUN apk add --no-cache libstdc++ libgcc zlib wget ca-certificates \
    && addgroup -S needlefish && adduser -S needlefish -G needlefish

WORKDIR /app
COPY --from=builder /app/build/bin/needlefish /app/needlefish
COPY web /app/web

VOLUME /data
EXPOSE 8080

USER needlefish

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD wget --no-verbose --tries=1 --spider http://127.0.0.1:8080/api/health || exit 1

ENTRYPOINT ["/app/needlefish", "serve"]
CMD ["--index", "/data/index.idx", "--host", "0.0.0.0", "--port", "8080", "--web-dir", "/app/web"]
