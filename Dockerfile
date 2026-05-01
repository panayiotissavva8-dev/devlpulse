# ─────────────────────────────────────────────────────────────────────────────
# Stage 1 – Builder
# ─────────────────────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    python3 \
    python3-dev \
    libasio-dev \
    libssl-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    pkg-config \
    zlib1g-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMake definition first – FetchContent downloads are cached in this layer
COPY CMakeLists.txt .

# Copy all source
COPY src/ src/

RUN cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/app/dist \
      -DPython3_EXECUTABLE=/usr/bin/python3 \
  && cmake --build build --parallel "$(nproc)" \
  && cmake --install build

# ─────────────────────────────────────────────────────────────────────────────
# Stage 2 – Runtime
# ─────────────────────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libsqlite3-0 \
    libcurl4 \
    ca-certificates \
    tini \
  && rm -rf /var/lib/apt/lists/*

RUN useradd --no-create-home --shell /bin/false appuser

WORKDIR /app

# Binary
COPY --from=builder /app/dist/bin/devpulse .

# Static web assets served by serveFile("web/...")
COPY web/ web/

# SQLite data directory – will be overridden by a Fly volume in production.
# In local Docker Compose it maps to a named volume.
RUN mkdir -p data && chown -R appuser:appuser /app

# The app calls loadEnv() which reads secret.env from CWD.
# In production (Fly.io) all secrets are already in the environment,
# so missing secret.env is harmless (it logs a warning and continues).
# For local Docker Compose, secret.env is bind-mounted at runtime (see docker-compose.yml).

EXPOSE 8080

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["./devpulse"]