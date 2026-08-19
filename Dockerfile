# Multi-stage lightweight build for Needlefish Search Engine REST API Server
FROM alpine:3.19 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    linux-headers

WORKDIR /app
COPY . .

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEEDLEFISH_BUILD_TESTS=OFF \
    -DNEEDLEFISH_BUILD_BENCHMARKS=OFF \
    -DNEEDLEFISH_BUILD_FUZZERS=OFF

RUN cmake --build build --target needlefish_cli

# Minimal deployment stage
FROM alpine:3.19

RUN apk add --no-cache libstdc++ libgcc

WORKDIR /app
COPY --from=builder /app/build/needlefish_cli /app/needlefish_cli
COPY web /app/web

EXPOSE 8080

ENTRYPOINT ["/app/needlefish_cli", "serve"]
CMD ["--index", "/data/index.idx", "--port", "8080", "--static", "/app/web"]
