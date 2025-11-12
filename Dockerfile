# Multi-stage Docker build for VCCDB (Linux x86_64)

FROM ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip pkg-config ca-certificates \
    python3 perl nasm libssl-dev libcurl4-openssl-dev librocksdb-dev \
    && rm -rf /var/lib/apt/lists/*

# Ensure compilers are discoverable by CMake
ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++

# Allow overriding target triplet (x64-linux default for QNAP x86_64)
ARG VCPKG_TRIPLET=x64-linux
ENV VCPKG_DEFAULT_TRIPLET=${VCPKG_TRIPLET}

# Build
WORKDIR /src
COPY . .
# Install development packages from apt to satisfy dependencies without vcpkg.
RUN apt-get update && apt-get install -y --no-install-recommends \
    librocksdb-dev libsimdjson-dev libtbb-dev libarrow-dev libfmt-dev libspdlog-dev \
    nlohmann-json3-dev libboost-system-dev libyaml-cpp-dev libzstd-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    && cmake --build build -j

# Runtime image
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl jq \
    && rm -rf /var/lib/apt/lists/*

# Copy binary to runtime image
COPY --from=build /src/build/themis_server /usr/local/bin/themis_server

# Triplet must match the build stage ARG; default to x64-linux
ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/lib

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
