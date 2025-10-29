# Multi-stage Docker build for VCCDB (Linux x86_64)

FROM ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip pkg-config ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg

# Build
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build -j

# Runtime image
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

# Copy binary and vcpkg-installed libs to satisfy shared deps
COPY --from=build /src/build/themis_server /usr/local/bin/themis_server
COPY --from=build /opt/vcpkg/installed /opt/vcpkg/installed
ENV LD_LIBRARY_PATH=/opt/vcpkg/installed/x64-linux/lib:$LD_LIBRARY_PATH

# Default config and data
COPY config/config.json /etc/vccdb/config.json
VOLUME ["/data"]
EXPOSE 8080

# Non-root user
RUN useradd -m -u 10001 vccdb || adduser -u 10001 -D vccdb 2>/dev/null || true
USER 10001:10001

ENTRYPOINT ["/usr/local/bin/themis_server", "--config", "/etc/vccdb/config.json"]
