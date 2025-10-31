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

# Allow overriding target triplet (x64-linux default for QNAP x86_64)
ARG VCPKG_TRIPLET=x64-linux
ENV VCPKG_DEFAULT_TRIPLET=${VCPKG_TRIPLET}

# Build
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=${VCPKG_TRIPLET} \
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

# Triplet must match the build stage ARG; default to x64-linux
ARG VCPKG_TRIPLET=x64-linux
ENV LD_LIBRARY_PATH=/opt/vcpkg/installed/${VCPKG_TRIPLET}/lib:$LD_LIBRARY_PATH

# Default config and data
COPY config/config.json /etc/vccdb/config.json
VOLUME ["/data"]
EXPOSE 8080

# Default to root; can be overridden via docker-compose (user: "0:0" or non-root)

ENTRYPOINT ["/usr/local/bin/themis_server", "--config", "/etc/vccdb/config.json"]
