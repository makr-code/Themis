# Multi-stage Docker build for ThemisDB
# Uses vcpkg for complete dependency management

FROM ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip tar pkg-config \
    ca-certificates python3 perl nasm autoconf automake libtool \
    && rm -rf /var/lib/apt/lists/*

# Bootstrap vcpkg - use stable 2024.12.16 release
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && cd ${VCPKG_ROOT} \
    && git checkout 2024.12.16 \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics \
    && VCPKG_BASELINE=$(git rev-parse HEAD) \
    && echo "Using vcpkg baseline: $VCPKG_BASELINE"

# Set up environment
ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++
ARG VCPKG_TRIPLET=x64-linux
ENV VCPKG_DEFAULT_TRIPLET=${VCPKG_TRIPLET}

WORKDIR /src

# Copy vcpkg manifest files first (for better layer caching)
# Use simplified vcpkg.docker.json for faster builds
COPY vcpkg.docker.json ./vcpkg.json
COPY vcpkg-configuration.json ./

# Update vcpkg-configuration.json with current baseline
RUN cd ${VCPKG_ROOT} \
    && VCPKG_BASELINE=$(git rev-parse HEAD) \
    && echo "{\"default-registry\":{\"kind\":\"builtin\",\"baseline\":\"$VCPKG_BASELINE\"}}" > /src/vcpkg-configuration.json

# Install dependencies via vcpkg manifest mode
# Disable compiler tracking and metrics for faster, more stable builds
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
ENV VCPKG_DISABLE_METRICS=1
RUN ${VCPKG_ROOT}/vcpkg install --triplet=${VCPKG_TRIPLET}

# Copy source code
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src

# Build ThemisDB
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DTHEMIS_BUILD_TESTS=OFF \
    -DTHEMIS_BUILD_BENCHMARKS=OFF \
    -DTHEMIS_ENABLE_TRACING=OFF \
    && cmake --build build --target themis_server -j$(nproc)

# Runtime stage - minimal Ubuntu image
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy built binary
COPY --from=build /src/build/themis_server /usr/local/bin/themis_server

# Copy vcpkg installed libraries that are needed at runtime
COPY --from=build /opt/vcpkg/installed/x64-linux/lib/*.so* /usr/local/lib/ || true

# Copy configuration files
RUN mkdir -p /etc/themis /usr/local/share/themis
COPY --from=build /src/config/config.json /etc/themis/config.json || true
COPY --from=build /src/config/config.qnap.json /usr/local/share/themis/config.qnap.json || true

# Setup runtime environment
RUN mkdir -p /data /var/log/themis && \
    chmod +x /usr/local/bin/themis_server && \
    ldconfig

ENV THEMIS_CONFIG_PATH=/etc/themis/config.json
ENV THEMIS_PORT=18765
ENV LD_LIBRARY_PATH=/usr/local/lib

VOLUME ["/data"]
EXPOSE 8080 18765

ENTRYPOINT ["/usr/local/bin/themis_server"]
CMD ["--config", "/etc/themis/config.json"]
