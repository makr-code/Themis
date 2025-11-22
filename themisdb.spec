Name:           themisdb
Version:        1.0.0
Release:        1%{?dist}
Summary:        Multi-model database system with ACID transactions

License:        MIT
URL:            https://github.com/makr-code/ThemisDB
Source0:        https://github.com/makr-code/ThemisDB/archive/v%{version}.tar.gz

BuildRequires:  gcc-c++ >= 11
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  git
BuildRequires:  pkgconfig
BuildRequires:  openssl-devel
BuildRequires:  rocksdb-devel
BuildRequires:  tbb-devel
BuildRequires:  arrow-devel
BuildRequires:  boost-devel
BuildRequires:  spdlog-devel
BuildRequires:  json-devel
BuildRequires:  libcurl-devel
BuildRequires:  yaml-cpp-devel
BuildRequires:  libzstd-devel
BuildRequires:  systemd-rpm-macros

Requires:       openssl-libs
Requires:       rocksdb
Requires:       tbb
Requires:       arrow-libs
Requires:       boost-system
Requires:       spdlog
Requires:       libcurl
Requires:       yaml-cpp
Requires:       libzstd

%description
ThemisDB is a high-performance multi-model database system that supports:
 - Relational data with secondary indexes
 - Graph traversals (BFS, Dijkstra, A*)
 - Vector search with HNSW index
 - Time-series data with Gorilla compression
 - Document storage
 - Full ACID transactions with MVCC
 - Advanced Query Language (AQL)
 - OpenTelemetry tracing and Prometheus metrics

%package devel
Summary:        Development files for ThemisDB
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Development headers and libraries for building applications that
link against ThemisDB.

%prep
%autosetup -n ThemisDB-%{version}

%build
%cmake \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHEMIS_BUILD_TESTS=OFF \
    -DTHEMIS_BUILD_BENCHMARKS=OFF \
    -DTHEMIS_ENABLE_GPU=OFF \
    -DTHEMIS_STRICT_BUILD=OFF \
    -DBUILD_SHARED_LIBS=OFF
%cmake_build

%install
%cmake_install

# Install systemd service file
install -D -m 644 %{_builddir}/ThemisDB-%{version}/debian/themisdb.service \
    %{buildroot}%{_unitdir}/themisdb.service

# Install configuration file
install -D -m 644 %{_builddir}/ThemisDB-%{version}/config/config.yaml \
    %{buildroot}%{_sysconfdir}/themisdb/config.yaml

# Create data directory
install -d -m 755 %{buildroot}%{_sharedstatedir}/themisdb

%pre
getent group themisdb >/dev/null || groupadd -r themisdb
getent passwd themisdb >/dev/null || \
    useradd -r -g themisdb -d %{_sharedstatedir}/themisdb -s /sbin/nologin \
    -c "ThemisDB Database Server" themisdb
exit 0

%post
%systemd_post themisdb.service
# Set ownership of data directory
chown -R themisdb:themisdb %{_sharedstatedir}/themisdb
chmod 750 %{_sharedstatedir}/themisdb
# Set ownership of config directory
chown -R root:themisdb %{_sysconfdir}/themisdb
chmod 750 %{_sysconfdir}/themisdb
chmod 640 %{_sysconfdir}/themisdb/config.yaml

%preun
%systemd_preun themisdb.service

%postun
%systemd_postun_with_restart themisdb.service
if [ $1 -eq 0 ] ; then
    # Package removal, not upgrade
    getent passwd themisdb >/dev/null && userdel themisdb
    getent group themisdb >/dev/null && groupdel themisdb
    rm -rf %{_sharedstatedir}/themisdb
fi

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/themis_server
%{_unitdir}/themisdb.service
%dir %attr(750,root,themisdb) %{_sysconfdir}/themisdb
%config(noreplace) %attr(640,root,themisdb) %{_sysconfdir}/themisdb/config.yaml
%dir %attr(750,themisdb,themisdb) %{_sharedstatedir}/themisdb

%files devel
%{_includedir}/*
%{_libdir}/libthemis_core.a

%changelog
* Fri Nov 22 2024 ThemisDB Team <info@themisdb.org> - 1.0.0-1
- Initial RPM release
- Multi-model database with relational, graph, vector, and time-series support
- Full ACID transactions with MVCC
- Advanced Query Language (AQL) with graph traversals
- HNSW vector index with persistence
- Enterprise security features (TLS 1.3, RBAC, audit logging)
- OpenTelemetry tracing and Prometheus metrics
- Change Data Capture (CDC) support
