class Themisdb < Formula
  desc "Multi-model database system with ACID transactions"
  homepage "https://github.com/makr-code/ThemisDB"
  url "https://github.com/makr-code/ThemisDB/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "HASH_PLACEHOLDER"
  license "MIT"
  head "https://github.com/makr-code/ThemisDB.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkg-config" => :build

  depends_on "openssl@3"
  depends_on "rocksdb"
  depends_on "tbb"
  depends_on "apache-arrow"
  depends_on "boost"
  depends_on "spdlog"
  depends_on "nlohmann-json"
  depends_on "curl"
  depends_on "yaml-cpp"
  depends_on "zstd"

  def install
    # Set environment for vcpkg-free build on macOS
    ENV["OPENSSL_ROOT_DIR"] = Formula["openssl@3"].opt_prefix
    
    system "cmake", "-S", ".", "-B", "build",
                    "-GNinja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DTHEMIS_BUILD_TESTS=OFF",
                    "-DTHEMIS_BUILD_BENCHMARKS=OFF",
                    "-DTHEMIS_ENABLE_GPU=OFF",
                    "-DTHEMIS_STRICT_BUILD=OFF",
                    "-DBUILD_SHARED_LIBS=OFF",
                    *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"

    # Install configuration file
    (etc/"themisdb").install "config/config.yaml" => "config.yaml.example"

    # Create data directory
    (var/"lib/themisdb").mkpath
  end

  def post_install
    # Create default config if it doesn't exist
    config_file = etc/"themisdb/config.yaml"
    unless config_file.exist?
      cp etc/"themisdb/config.yaml.example", config_file
      
      # Update paths for macOS
      inreplace config_file do |s|
        s.gsub! "./data/rocksdb", "#{var}/lib/themisdb"
      end
    end
  end

  service do
    run [opt_bin/"themis_server", "--config", etc/"themisdb/config.yaml"]
    keep_alive true
    working_dir var/"lib/themisdb"
    log_path var/"log/themisdb.log"
    error_log_path var/"log/themisdb.error.log"
    environment_variables PATH: std_service_path_env
  end

  test do
    # Test that the binary runs and shows version/help
    assert_match "ThemisDB", shell_output("#{bin}/themis_server --help 2>&1", 1)
    
    # Test configuration file parsing
    config = etc/"themisdb/config.yaml"
    assert_predicate config, :exist?, "Config file should exist"
  end
end
