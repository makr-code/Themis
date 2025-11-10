# Multi-stage Docker build for VCCDB (Linux x86_64)

FROM ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ninja-build \
    git \
    python3 \
    curl \
    unzip \
    tar \
    wget \
    pkg-config \
    file \
    patchelf \
    binutils \
    chrpath \
    ca-certificates \
    zlib1g-dev \
    libssl-dev \
    libicu-dev \
    zip \
    perl \
    nasm \
    && rm -rf /var/lib/apt/lists/*

# Install modern CMake (Boost 1.89+ requires >=3.25; system is 3.22 on Ubuntu 22.04)
ARG CMAKE_VERSION=3.27.9
RUN set -eux; \
    wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh; \
    sh cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/usr/local --skip-license; \
    # Ensure cmake binaries are on PATH for subsequent RUN steps
    cp -a /usr/local/cmake-${CMAKE_VERSION}-linux-x86_64/bin/* /usr/local/bin/; \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh; \
    cmake --version

# Install vcpkg
WORKDIR /opt
# Allow overriding target triplet (x64-linux default for QNAP x86_64)
ARG VCPKG_TRIPLET=x64-linux
ENV VCPKG_DEFAULT_TRIPLET=${VCPKG_TRIPLET}
RUN git clone https://github.com/microsoft/vcpkg.git vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
ENV VCPKG_FEATURE_FLAGS=manifests,registries
# Reduce noise and allow vcpkg to size parallelism automatically
ENV VCPKG_DISABLE_METRICS=1
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Ensure compilers are discoverable by CMake
ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++

# Make sure vcpkg is updated and clear stale state
RUN set -eux; \
    cd ${VCPKG_ROOT}; \
    git fetch --all --tags || true; \
    git reset --hard origin/master || true; \
    ./vcpkg update || true; \
    # remove stale or partial build state that often causes parallel configure failures (keep downloads for caching)
    rm -rf buildtrees packages || true

# Allow overriding target triplet at build time too (kept for later stages)
ENV VCPKG_TRIPLET=${VCPKG_TRIPLET}

# Build
WORKDIR /src
COPY . .

# Pre-resolve and build all dependencies in manifest mode so that any vcpkg
# post-build validation errors are surfaced clearly before configuring our CMake project
RUN set -eux; \
    cd /src; \
    # Vollständige Logs behalten (kein --clean-after-build) und Debug aktivieren
    vcpkg install --triplet=${VCPKG_TRIPLET} --debug 2>&1 | tee /tmp/vcpkg-install.log || ( \
        echo "===== vcpkg install failed; filtered summary ====="; \
        grep -i -E 'error|failed|missing|policy' /tmp/vcpkg-install.log | tail -n 300 || true; \
        echo "===== FULL vcpkg install log (BEGIN) ====="; \
        cat /tmp/vcpkg-install.log || true; \
        echo "===== FULL vcpkg install log (END) ====="; \
        echo "===== buildtrees detail logs (last 150 lines each) ====="; \
        ls -la /opt/vcpkg/buildtrees || true; \
        find /opt/vcpkg/buildtrees -maxdepth 5 -type f -name '*.log' \
            -exec bash -lc 'for f in "$@"; do echo "=== $f ==="; tail -n 150 "$f"; done' bash {} + 2>/dev/null || true; \
        false )

RUN set -eux; \
        cmake -S . -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
            -DVCPKG_TARGET_TRIPLET=${VCPKG_TRIPLET} \
            -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
            -DCMAKE_C_COMPILER=/usr/bin/gcc \
            -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
            -DTHEMIS_BUILD_TESTS=OFF \
            -DTHEMIS_BUILD_BENCHMARKS=OFF \
        2>&1 | tee /tmp/cmake-config.log; \
        cmake_config_status=${PIPESTATUS[0]}; \
        if [ $cmake_config_status -ne 0 ]; then \
            echo "===== CMake configure failed; dumping logs ====="; \
            tail -n 400 /tmp/cmake-config.log || true; \
            ls -la /opt/vcpkg/buildtrees || true; \
            find /opt/vcpkg/buildtrees -maxdepth 2 -type f -name '*.log' \
                -exec bash -lc 'for f in "$@"; do echo "=== $f ==="; tail -n 200 "$f"; done' bash {} + 2>/dev/null || true; \
            if [ -f build/CMakeFiles/CMakeError.log ]; then echo "=== build/CMakeFiles/CMakeError.log ==="; tail -n 400 build/CMakeFiles/CMakeError.log; fi; \
            if [ -f build/CMakeFiles/CMakeOutput.log ]; then echo "=== build/CMakeFiles/CMakeOutput.log ==="; tail -n 200 build/CMakeFiles/CMakeOutput.log; fi; \
            false; \
        fi; \
        cmake --build build -j 2>&1 | tee /tmp/cmake-build.log; \
        cmake_build_status=${PIPESTATUS[0]}; \
        if [ $cmake_build_status -ne 0 ]; then \
            echo "===== CMake build failed; dumping logs ====="; \
            tail -n 400 /tmp/cmake-build.log || true; \
            ls -la /opt/vcpkg/buildtrees || true; \
            find /opt/vcpkg/buildtrees -maxdepth 2 -type f -name '*.log' \
                -exec bash -lc 'for f in "$@"; do echo "=== $f ==="; tail -n 200 "$f"; done' bash {} + 2>/dev/null || true; \
            false; \
        fi

# Runtime image
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl jq \
    && rm -rf /var/lib/apt/lists/*

# Copy binary and vcpkg-installed libs to satisfy shared deps
COPY --from=build /src/build/themis_server /usr/local/bin/themis_server
COPY --from=build /opt/vcpkg/installed /opt/vcpkg/installed

# Triplet must match the build stage ARG; default to x64-linux
ARG VCPKG_TRIPLET=x64-linux
ENV LD_LIBRARY_PATH=/opt/vcpkg/installed/${VCPKG_TRIPLET}/lib

# Default config and data
COPY config/config.json /etc/vccdb/config.json
# Default Linux/QNAP config template inside image
RUN mkdir -p /usr/local/share/themis
COPY config/config.qnap.json /usr/local/share/themis/config.qnap.json
VOLUME ["/data"]
EXPOSE 8080 18765

# Default to root; can be overridden via docker-compose (user: "0:0" or non-root)

# Configurable server port (overrides JSON via entrypoint)
ENV THEMIS_PORT=18765
ENV THEMIS_CONFIG_PATH=/etc/vccdb/config.json

# Bootstrap entrypoint to adjust config and ensure directories
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
