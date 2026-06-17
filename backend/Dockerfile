# =====================================================
# 多阶段构建: 地动仪 C++ 后端
# =====================================================

# ---------- 阶段 1: 构建 ----------
FROM gcc:13 AS builder

WORKDIR /src

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        libboost-all-dev \
        libspdlog-dev \
        pkg-config \
        ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY backend/CMakeLists.txt    ./CMakeLists.txt
COPY backend/include           ./include
COPY backend/src               ./src

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DUSE_BOOST_LOCKFREE=ON \
        -DUSE_SPDLOG=ON \
        -DCMAKE_CXX_FLAGS="-O2 -march=x86-64 -flto" \
 && cmake --build build --parallel

# ---------- 阶段 2: 运行时 ----------
FROM debian:bookworm-slim AS runtime

LABEL maintainer="seismograph-team" \
      description="古代地动仪都柱地震响应仿真与灵敏度分析系统 - C++ 后端"

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libboost-filesystem1.74.0 \
        libboost-system1.74.0 \
        libspdlog1.12 \
        libfmt9 \
        ca-certificates \
        curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/seismograph_backend ./bin/seismograph_backend
COPY backend/config.json                        ./config.json

RUN chmod +x ./bin/seismograph_backend \
 && mkdir -p /app/logs /app/data

ENV SEISMOGRAPH_CONFIG=/app/config.json
ENV TZ=Asia/Shanghai

EXPOSE 12345/udp
EXPOSE 8080/tcp

HEALTHCHECK --interval=30s --timeout=5s --retries=3 --start-period=10s \
    CMD curl -f http://127.0.0.1:8080/api/stats || exit 1

ENTRYPOINT ["/app/bin/seismograph_backend"]
CMD ["--config", "/app/config.json"]
