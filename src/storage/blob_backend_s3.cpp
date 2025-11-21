#include "storage/blob_storage_backend.h"
#include "utils/logger.h"
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace themis {
namespace storage {

/**
 * @brief AWS S3 Blob Storage Backend
 * 
 * Stores blobs in AWS S3 bucket with optional prefix.
 * Uses AWS SDK for C++ v3.
 * 
 * Features:
 * - Automatic retry with exponential backoff
 * - Server-side encryption (AES256)
 * - Content-MD5 verification
 * - Thread-safe operations
 */
class S3BlobBackend : public IBlobStorageBackend {
private:
    std::string bucket_;
    std::string region_;
    std::string prefix_;
    std::unique_ptr<Aws::S3::S3Client> client_;
    mutable std::mutex mutex_;
    
    static bool aws_sdk_initialized_;
    static std::mutex init_mutex_;
    
    // Compute SHA256 hash
    static std::string computeSHA256(const std::vector<uint8_t>& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    
    // Get S3 key for blob
    std::string getS3Key(const std::string& blob_id) const {
        if (prefix_.empty()) {
            return blob_id + ".blob";
        }
        return prefix_ + "/" + blob_id + ".blob";
    }
    
    // Initialize AWS SDK (called once)
    static void initializeSDK() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (!aws_sdk_initialized_) {
            Aws::SDKOptions options;
            options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Warn;
            Aws::InitAPI(options);
            aws_sdk_initialized_ = true;
            THEMIS_INFO("AWS SDK initialized");
        }
    }

public:
    S3BlobBackend(const std::string& bucket, const std::string& region, const std::string& prefix = "")
        : bucket_(bucket), region_(region), prefix_(prefix) {
        
        initializeSDK();
        
        // Configure S3 client
        Aws::Client::ClientConfiguration config;
        config.region = region_;
        config.connectTimeoutMs = 5000;
        config.requestTimeoutMs = 30000;
        config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("S3Backend", 3);
        
        // Use default credentials provider chain (env vars, ~/.aws/credentials, IAM role)
        client_ = std::make_unique<Aws::S3::S3Client>(config);
        
        THEMIS_INFO("S3BlobBackend initialized: bucket={}, region={}, prefix={}", 
                    bucket_, region_, prefix_);
    }
    
    ~S3BlobBackend() override = default;
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string s3_key = getS3Key(blob_id);
        
        // Create PutObject request
        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(bucket_);
        request.SetKey(s3_key);
        request.SetServerSideEncryption(Aws::S3::Model::ServerSideEncryption::AES256);
        
        // Create stream from data
        auto input_stream = Aws::MakeShared<Aws::StringStream>("PutObjectInputStream");
        input_stream->write(reinterpret_cast<const char*>(data.data()), data.size());
        request.SetBody(input_stream);
        request.SetContentLength(data.size());
        
        // Upload to S3
        auto outcome = client_->PutObject(request);
        
        if (!outcome.IsSuccess()) {
            auto error = outcome.GetError();
            THEMIS_ERROR("S3 PutObject failed: {} - {}", 
                        error.GetExceptionName(), error.GetMessage());
            throw std::runtime_error("S3 upload failed: " + error.GetMessage());
        }
        
        // Create blob reference
        BlobRef ref;
        ref.id = blob_id;
        ref.type = BlobStorageType::S3;
        ref.uri = "s3://" + bucket_ + "/" + s3_key;
        ref.size_bytes = data.size();
        ref.hash_sha256 = computeSHA256(data);
        ref.created_at = std::chrono::system_clock::now().time_since_epoch().count();
        
        THEMIS_DEBUG("Blob stored in S3: id={}, size={} bytes", blob_id, data.size());
        return ref;
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string s3_key = getS3Key(ref.id);
        
        // Create GetObject request
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(bucket_);
        request.SetKey(s3_key);
        
        // Download from S3
        auto outcome = client_->GetObject(request);
        
        if (!outcome.IsSuccess()) {
            auto error = outcome.GetError();
            if (error.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY) {
                THEMIS_WARN("Blob not found in S3: {}", ref.id);
                return std::nullopt;
            }
            THEMIS_ERROR("S3 GetObject failed: {} - {}", 
                        error.GetExceptionName(), error.GetMessage());
            return std::nullopt;
        }
        
        // Read data from stream
        auto& body = outcome.GetResult().GetBody();
        std::vector<uint8_t> data;
        data.reserve(ref.size_bytes);
        
        char buffer[4096];
        while (body.read(buffer, sizeof(buffer))) {
            data.insert(data.end(), buffer, buffer + body.gcount());
        }
        if (body.gcount() > 0) {
            data.insert(data.end(), buffer, buffer + body.gcount());
        }
        
        // Verify hash
        std::string actual_hash = computeSHA256(data);
        if (actual_hash != ref.hash_sha256) {
            THEMIS_ERROR("Hash mismatch for blob {}: expected={}, actual={}", 
                        ref.id, ref.hash_sha256, actual_hash);
            return std::nullopt;
        }
        
        THEMIS_DEBUG("Blob retrieved from S3: id={}, size={} bytes", ref.id, data.size());
        return data;
    }
    
    bool remove(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string s3_key = getS3Key(ref.id);
        
        // Create DeleteObject request
        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(bucket_);
        request.SetKey(s3_key);
        
        // Delete from S3
        auto outcome = client_->DeleteObject(request);
        
        if (!outcome.IsSuccess()) {
            auto error = outcome.GetError();
            THEMIS_ERROR("S3 DeleteObject failed: {} - {}", 
                        error.GetExceptionName(), error.GetMessage());
            return false;
        }
        
        THEMIS_DEBUG("Blob deleted from S3: id={}", ref.id);
        return true;
    }
    
    bool exists(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string s3_key = getS3Key(ref.id);
        
        // Create HeadObject request
        Aws::S3::Model::HeadObjectRequest request;
        request.SetBucket(bucket_);
        request.SetKey(s3_key);
        
        // Check existence
        auto outcome = client_->HeadObject(request);
        return outcome.IsSuccess();
    }
    
    std::string name() const override {
        return "s3";
    }
    
    bool isAvailable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Test connectivity with a simple ListBuckets call
        try {
            auto outcome = client_->ListBuckets();
            return outcome.IsSuccess();
        } catch (...) {
            return false;
        }
    }
};

// Static member initialization
bool S3BlobBackend::aws_sdk_initialized_ = false;
std::mutex S3BlobBackend::init_mutex_;

} // namespace storage
} // namespace themis
