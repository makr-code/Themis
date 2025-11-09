#pragma once

#include <atomic>
#include <array>
#include <string>
#include <cstdint>
#include <mutex>

// Lightweight singleton for tracking content blob compression statistics (ZSTD)
// Thread-safe via atomic counters. Designed for Prometheus exposition.
// We keep low cardinality by aggregating MIME types into coarse groups.
// Level distribution recorded per ZSTD level (1-22).

namespace themis { namespace utils {

class CompressionMetrics {
public:
    static CompressionMetrics& instance() {
        static CompressionMetrics inst; return inst;
    }

    void recordCompression(uint64_t original_bytes, uint64_t compressed_bytes, const std::string& mime, int level) {
        compressed_blobs_total_.fetch_add(1, std::memory_order_relaxed);
        original_bytes_total_.fetch_add(original_bytes, std::memory_order_relaxed);
        compressed_bytes_total_.fetch_add(compressed_bytes, std::memory_order_relaxed);
        if (level >= 1 && level <= 22) {
            level_original_bytes_[level].fetch_add(original_bytes, std::memory_order_relaxed);
        }
        mimeGroup(mime).fetch_add(1, std::memory_order_relaxed);
        // timing handled externally via start/stop, see recordCompressionTime
    }

    void recordSkipped(uint64_t original_bytes, const std::string& mime) {
        uncompressed_blobs_total_.fetch_add(1, std::memory_order_relaxed);
        original_bytes_total_.fetch_add(original_bytes, std::memory_order_relaxed);
        mimeGroup(mime).fetch_add(1, std::memory_order_relaxed);
    }

    void recordCompressionTime(uint64_t microseconds) {
        compression_time_sum_us_.fetch_add(microseconds, std::memory_order_relaxed);
        incrementBucket(compression_time_buckets_, microseconds);
    }
    void recordDecompressionTime(uint64_t microseconds) {
        decompression_time_sum_us_.fetch_add(microseconds, std::memory_order_relaxed);
        incrementBucket(decompression_time_buckets_, microseconds);
    }

    // Prometheus exposition fragment
    std::string toPrometheus() const {
        std::string out;
        out.reserve(1024);
        auto comp = compressed_blobs_total_.load(std::memory_order_relaxed);
        auto uncomp = uncompressed_blobs_total_.load(std::memory_order_relaxed);
        auto orig = original_bytes_total_.load(std::memory_order_relaxed);
        auto comp_bytes = compressed_bytes_total_.load(std::memory_order_relaxed);
        double avg_ratio = (comp_bytes > 0) ? static_cast<double>(orig) / static_cast<double>(comp_bytes) : 1.0;

        out += "# HELP themis_compressed_blobs_total Number of blobs stored compressed\n";
        out += "# TYPE themis_compressed_blobs_total counter\n";
        out += "themis_compressed_blobs_total " + std::to_string(comp) + "\n";

        out += "# HELP themis_uncompressed_blobs_total Number of blobs stored uncompressed\n";
        out += "# TYPE themis_uncompressed_blobs_total counter\n";
        out += "themis_uncompressed_blobs_total " + std::to_string(uncomp) + "\n";

        out += "# HELP themis_compression_original_bytes_total Total original bytes of processed blobs\n";
        out += "# TYPE themis_compression_original_bytes_total counter\n";
        out += "themis_compression_original_bytes_total " + std::to_string(orig) + "\n";

        out += "# HELP themis_compression_compressed_bytes_total Total bytes after compression (only compressed blobs)\n";
        out += "# TYPE themis_compression_compressed_bytes_total counter\n";
        out += "themis_compression_compressed_bytes_total " + std::to_string(comp_bytes) + "\n";

        out += "# HELP themis_compression_ratio_average Average compression ratio (original/compressed) across compressed blobs\n";
        out += "# TYPE themis_compression_ratio_average gauge\n";
        out += "themis_compression_ratio_average " + std::to_string(avg_ratio) + "\n";

        // Level distribution (original bytes compressed per level)
        out += "# HELP themis_compression_level_original_bytes_total Original bytes compressed per zstd level\n";
        out += "# TYPE themis_compression_level_original_bytes_total counter\n";
        for (int lvl = 1; lvl <= 22; ++lvl) {
            auto val = level_original_bytes_[lvl].load(std::memory_order_relaxed);
            if (val > 0) {
                out += "themis_compression_level_original_bytes_total{level=\"" + std::to_string(lvl) + "\"} " + std::to_string(val) + "\n";
            }
        }

        // MIME group counters
        out += "# HELP themis_compression_mime_groups_total Processed blobs per MIME group (compressed + uncompressed)\n";
        out += "# TYPE themis_compression_mime_groups_total counter\n";
        const char* groupNames[6] = {"text","image","video","application","audio","other"};
        for (int i=0;i<6;++i) {
            auto val = mime_groups_[i].load(std::memory_order_relaxed);
            if (val > 0) {
                out += "themis_compression_mime_groups_total{group=\"" + std::string(groupNames[i]) + "\"} " + std::to_string(val) + "\n";
            }
        }

        // Compression time histogram buckets
        out += "# HELP themis_compression_time_microseconds_bucket Compression time histogram (us)\n";
        out += "# TYPE themis_compression_time_microseconds_bucket histogram\n";
        exportHistogram(out, "themis_compression_time_microseconds", compression_time_buckets_, compression_time_sum_us_.load(std::memory_order_relaxed));
        out += "# HELP themis_decompression_time_microseconds_bucket Decompression time histogram (us)\n";
        out += "# TYPE themis_decompression_time_microseconds_bucket histogram\n";
        exportHistogram(out, "themis_decompression_time_microseconds", decompression_time_buckets_, decompression_time_sum_us_.load(std::memory_order_relaxed));
        return out;
    }

private:
    CompressionMetrics() {
        // Default-init atomics (zero-initialized by std::atomic default ctor)
        // No explicit fill needed – std::atomic<uint64_t> default-constructs to 0
    }
    CompressionMetrics(const CompressionMetrics&) = delete;
    CompressionMetrics& operator=(const CompressionMetrics&) = delete;

    std::atomic<uint64_t>& mimeGroup(const std::string& mime) const {
        if (mime.rfind("text/", 0) == 0) return mime_groups_[0];
        if (mime.rfind("image/", 0) == 0) return mime_groups_[1];
        if (mime.rfind("video/", 0) == 0) return mime_groups_[2];
        if (mime.rfind("application/", 0) == 0) return mime_groups_[3];
        if (mime.rfind("audio/", 0) == 0) return mime_groups_[4];
        return mime_groups_[5];
    }

    // Histogram helper: fixed buckets
    void incrementBucket(std::array<std::atomic<uint64_t>, 11>& buckets, uint64_t us) {
        // Buckets: 100, 500, 1000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000, +Inf
        static const uint64_t limits[10] = {100,500,1000,5000,10000,50000,100000,500000,1000000,5000000};
        for (size_t i=0;i<10;++i) {
            if (us <= limits[i]) { buckets[i].fetch_add(1, std::memory_order_relaxed); return; }
        }
        buckets[10].fetch_add(1, std::memory_order_relaxed); // +Inf
    }

    void exportHistogram(std::string& out, const std::string& name, const std::array<std::atomic<uint64_t>,11>& buckets, uint64_t sum) const {
        static const char* le[11] = {"100","500","1000","5000","10000","50000","100000","500000","1000000","5000000","+Inf"};
        uint64_t cumulative = 0;
        for (size_t i=0;i<11;++i) {
            cumulative += buckets[i].load(std::memory_order_relaxed);
            out += name + "_bucket{le=\"" + le[i] + "\"} " + std::to_string(cumulative) + "\n";
        }
        out += name + "_sum " + std::to_string(sum) + "\n";
        out += name + "_count " + std::to_string(cumulative) + "\n";
    }

    // Counters
    std::atomic<uint64_t> compressed_blobs_total_{0};
    std::atomic<uint64_t> uncompressed_blobs_total_{0};
    std::atomic<uint64_t> original_bytes_total_{0};
    std::atomic<uint64_t> compressed_bytes_total_{0};
    mutable std::array<std::atomic<uint64_t>, 23> level_original_bytes_{}; // 1..22 used
    mutable std::array<std::atomic<uint64_t>, 6> mime_groups_{}; // text,image,video,application,audio,other
    // Timing histograms (microseconds)
    std::array<std::atomic<uint64_t>,11> compression_time_buckets_{};
    std::array<std::atomic<uint64_t>,11> decompression_time_buckets_{};
    std::atomic<uint64_t> compression_time_sum_us_{0};
    std::atomic<uint64_t> decompression_time_sum_us_{0};
};

}} // namespace themis::utils
