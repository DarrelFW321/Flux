# ── Stage 1: build the flux compiler ─────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build \
        llvm-dev clang lld \
        wget gnupg ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY compiler/ compiler/

# Point CMake at the system LLVM (version determined by distro, typically 17-18 on 24.04).
RUN LLVM_CMAKE=$(llvm-config --cmakedir) && \
    cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=g++ \
        -DLLVM_DIR="$LLVM_CMAKE" \
        -DCMAKE_PREFIX_PATH=/usr && \
    cmake --build build

# ── Stage 2: runtime image ────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 python3-pip \
        libstdc++6 clang \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the compiled flux binary
COPY --from=builder /src/build/flux ./build/flux

# Install Python dependencies
COPY backend/requirements.txt .
RUN pip3 install --no-cache-dir -r requirements.txt

COPY backend/ backend/

EXPOSE 8080

# Render sets PORT env var; fall back to 8080.
CMD ["sh", "-c", "uvicorn backend.server:app --host 0.0.0.0 --port ${PORT:-8080}"]
