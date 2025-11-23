#!/usr/bin/env bash
# Build ARM packages locally using Docker
# Supports Debian, RPM, and Arch package formats

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
VERSION="${VERSION:-1.0.0}"
ARCH="${ARCH:-arm64}"  # arm64 or armv7
FORMAT="${FORMAT:-deb}"  # deb, rpm, or arch
OUTPUT_DIR="${OUTPUT_DIR:-packages}"

echo -e "${BLUE}=== ThemisDB ARM Package Builder ===${NC}"
echo ""
echo "Configuration:"
echo "  Version: $VERSION"
echo "  Architecture: $ARCH"
echo "  Format: $FORMAT"
echo "  Output: $OUTPUT_DIR"
echo ""

# Validate architecture
case "$ARCH" in
    arm64|armv7)
        ;;
    *)
        echo -e "${RED}Error: Invalid architecture: $ARCH${NC}"
        echo "Supported: arm64, armv7"
        exit 1
        ;;
esac

# Validate format
case "$FORMAT" in
    deb|rpm|arch)
        ;;
    *)
        echo -e "${RED}Error: Invalid format: $FORMAT${NC}"
        echo "Supported: deb, rpm, arch"
        exit 1
        ;;
esac

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Determine Docker platform
case "$ARCH" in
    arm64)
        DOCKER_PLATFORM="linux/arm64"
        ;;
    armv7)
        DOCKER_PLATFORM="linux/arm/v7"
        ;;
esac

echo -e "${GREEN}Building $FORMAT package for $ARCH...${NC}"
echo ""

# Build based on format
case "$FORMAT" in
    deb)
        echo "Building Debian/Ubuntu package..."
        docker run --rm --platform "$DOCKER_PLATFORM" \
            -v "$(pwd):/workspace" \
            -w /workspace \
            -e VERSION="$VERSION" \
            debian:bookworm \
            bash -c '
                set -e
                echo "Installing build dependencies..."
                apt-get update -qq
                apt-get install -y -qq build-essential cmake ninja-build git \
                    dpkg-dev debhelper devscripts \
                    libssl-dev libcurl4-openssl-dev libyaml-cpp-dev libzstd-dev \
                    > /dev/null
                
                echo "Building package..."
                dpkg-buildpackage -b -uc -us -j$(nproc)
                
                echo "Moving packages to output directory..."
                mkdir -p /workspace/packages
                mv ../*.deb /workspace/packages/ 2>/dev/null || true
                mv ../*.changes /workspace/packages/ 2>/dev/null || true
                mv ../*.buildinfo /workspace/packages/ 2>/dev/null || true
                
                echo "Cleaning up..."
                rm -f ../*.tar.* ../*.dsc
            '
        ;;
        
    rpm)
        echo "Building RPM package..."
        docker run --rm --platform "$DOCKER_PLATFORM" \
            -v "$(pwd):/workspace" \
            -w /workspace \
            -e VERSION="$VERSION" \
            fedora:39 \
            bash -c '
                set -e
                echo "Installing build dependencies..."
                dnf install -y -q rpm-build rpmdevtools gcc-c++ cmake ninja-build git \
                    openssl-devel libcurl-devel yaml-cpp-devel libzstd-devel \
                    > /dev/null
                
                echo "Setting up RPM build tree..."
                rpmdev-setuptree
                
                echo "Creating source tarball..."
                mkdir -p ~/rpmbuild/SOURCES
                tar czf ~/rpmbuild/SOURCES/themisdb-${VERSION}.tar.gz \
                    --transform "s,^,ThemisDB-${VERSION}/," \
                    --exclude=.git --exclude=build* \
                    --exclude=packages --exclude=data .
                
                echo "Building RPM..."
                rpmbuild -bb /workspace/themisdb.spec \
                    --define "version ${VERSION}" \
                    --define "_topdir ${HOME}/rpmbuild"
                
                echo "Moving packages to output directory..."
                mkdir -p /workspace/packages
                cp ~/rpmbuild/RPMS/*/*.rpm /workspace/packages/
            '
        ;;
        
    arch)
        echo "Building Arch Linux package..."
        docker run --rm --platform "$DOCKER_PLATFORM" \
            -v "$(pwd):/workspace" \
            -w /workspace \
            -e VERSION="$VERSION" \
            archlinux:latest \
            bash -c '
                set -e
                echo "Installing build dependencies..."
                pacman -Syu --noconfirm > /dev/null 2>&1
                pacman -S --noconfirm base-devel cmake ninja git \
                    openssl rocksdb intel-tbb arrow boost spdlog curl yaml-cpp zstd \
                    > /dev/null 2>&1
                
                echo "Creating build user..."
                useradd -m builder
                chown -R builder:builder /workspace
                
                echo "Building package..."
                su - builder -c "
                    cd /workspace
                    makepkg -s --noconfirm
                    mkdir -p packages
                    mv *.pkg.tar.zst packages/ 2>/dev/null || true
                "
            '
        ;;
esac

# Check if packages were created
if [ -z "$(ls -A "$OUTPUT_DIR" 2>/dev/null)" ]; then
    echo -e "${RED}Error: No packages were created${NC}"
    exit 1
fi

# List created packages
echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "Created packages:"
ls -lh "$OUTPUT_DIR"
echo ""

# Show package info
echo -e "${BLUE}Package Information:${NC}"
for pkg in "$OUTPUT_DIR"/*; do
    if [ -f "$pkg" ]; then
        case "${pkg##*.}" in
            deb)
                echo "DEB Package: $(basename "$pkg")"
                dpkg-deb -I "$pkg" 2>/dev/null | grep -E "Package:|Version:|Architecture:" || true
                ;;
            rpm)
                echo "RPM Package: $(basename "$pkg")"
                # Note: rpm -qip won't work in cross-arch scenario
                ;;
            zst)
                echo "Arch Package: $(basename "$pkg")"
                ;;
        esac
        echo ""
    fi
done

# Verify package
echo -e "${BLUE}Package Contents:${NC}"
case "$FORMAT" in
    deb)
        for pkg in "$OUTPUT_DIR"/*.deb; do
            if [ -f "$pkg" ]; then
                echo "Files in $(basename "$pkg"):"
                dpkg-deb -c "$pkg" | grep -E "themis_server|config.yaml|themisdb.service" || true
            fi
        done
        ;;
    rpm)
        echo "Use 'rpm -qlp <package>' to list contents"
        ;;
    arch)
        echo "Use 'tar -tzf <package>' to list contents"
        ;;
esac

echo ""
echo -e "${GREEN}Done! Packages are in: $OUTPUT_DIR${NC}"
echo ""
echo "To test installation:"
case "$FORMAT" in
    deb)
        echo "  sudo dpkg -i $OUTPUT_DIR/themisdb_*.deb"
        ;;
    rpm)
        echo "  sudo rpm -i $OUTPUT_DIR/themisdb-*.rpm"
        ;;
    arch)
        echo "  sudo pacman -U $OUTPUT_DIR/themisdb-*.pkg.tar.zst"
        ;;
esac
echo ""

exit 0
