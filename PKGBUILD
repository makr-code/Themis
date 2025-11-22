# Maintainer: ThemisDB Team <info@themisdb.org>
pkgname=themisdb
pkgver=1.0.0
pkgrel=1
pkgdesc='Multi-model database system with ACID transactions'
arch=('x86_64' 'aarch64')
url='https://github.com/makr-code/ThemisDB'
license=('MIT')
depends=(
    'openssl'
    'rocksdb'
    'intel-tbb'
    'arrow'
    'boost-libs'
    'spdlog'
    'curl'
    'yaml-cpp'
    'zstd'
)
makedepends=(
    'cmake>=3.20'
    'ninja'
    'gcc>=11'
    'git'
    'boost'
    'nlohmann-json'
)
backup=('etc/themisdb/config.yaml')
source=("$pkgname-$pkgver.tar.gz::https://github.com/makr-code/ThemisDB/archive/v$pkgver.tar.gz"
        'themisdb.service')
sha256sums=('SKIP'
            'SKIP')

build() {
    cd "ThemisDB-$pkgver"
    
    cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DTHEMIS_BUILD_TESTS=OFF \
        -DTHEMIS_BUILD_BENCHMARKS=OFF \
        -DTHEMIS_ENABLE_GPU=OFF \
        -DTHEMIS_STRICT_BUILD=OFF \
        -DBUILD_SHARED_LIBS=OFF
    
    cmake --build build
}

check() {
    cd "ThemisDB-$pkgver"
    # Optional: run tests if enabled
    # cmake --build build --target test
}

package() {
    cd "ThemisDB-$pkgver"
    
    DESTDIR="$pkgdir" cmake --install build
    
    # Install systemd service
    install -Dm644 "$srcdir/themisdb.service" \
        "$pkgdir/usr/lib/systemd/system/themisdb.service"
    
    # Install default configuration
    install -Dm644 config/config.yaml \
        "$pkgdir/etc/themisdb/config.yaml"
    
    # Create data directory
    install -dm750 "$pkgdir/var/lib/themisdb"
    
    # Install documentation
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 CHANGELOG.md "$pkgdir/usr/share/doc/$pkgname/CHANGELOG.md"
}

# Development package
package_themisdb-dev() {
    pkgdesc='Development files for ThemisDB'
    depends=("themisdb=$pkgver")
    
    cd "ThemisDB-$pkgver"
    
    # Headers are already installed by main package
    # Just ensure static library is in the dev package
}
