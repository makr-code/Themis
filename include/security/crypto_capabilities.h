#include <iostream>
#include <openssl/evp.h>
#include <openssl/engine.h>

#ifdef _WIN32
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace themis {

/**
 * @brief Check if CPU supports AES-NI hardware acceleration
 * 
 * @return true if AES-NI is available
 */
bool hasAESNI() {
    unsigned int cpuInfo[4];
    
#ifdef _WIN32
    __cpuid((int*)cpuInfo, 1);
#else
    __cpuid(1, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
#endif
    
    // Check ECX bit 25 (AES-NI)
    return (cpuInfo[2] & (1 << 25)) != 0;
}

/**
 * @brief Get information about OpenSSL hardware acceleration
 * 
 * @return String describing active acceleration
 */
std::string getEncryptionCapabilities() {
    std::ostringstream oss;
    
    oss << "OpenSSL Version: " << OpenSSL_version(OPENSSL_VERSION) << "\n";
    
    // Check AES-NI support
    if (hasAESNI()) {
        oss << "AES-NI: Available (Hardware Acceleration Enabled)\n";
    } else {
        oss << "AES-NI: Not Available (Software Fallback)\n";
    }
    
    // Check engine
    ENGINE* engine = ENGINE_get_default_cipher();
    if (engine) {
        oss << "Active Engine: " << ENGINE_get_name(engine) << "\n";
    } else {
        oss << "Active Engine: Default (Software)\n";
    }
    
    return oss.str();
}

/**
 * @brief Benchmark encryption performance
 * 
 * @return Operations per second
 */
double benchmarkEncryption() {
    // ... implementation ...
    return 0.0;
}

}  // namespace themis
