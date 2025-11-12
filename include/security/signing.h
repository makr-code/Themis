#pragma once

#include <string>
#include <vector>
#include <memory>

namespace themis {

struct SigningResult {
    std::vector<uint8_t> signature;
    std::string algorithm; // e.g., "RSASSA-PSS/SHA256"
};

class SigningService {
public:
    virtual ~SigningService() = default;

    // Sign the provided data and return signature bytes
    virtual SigningResult sign(const std::vector<uint8_t>& data, const std::string& key_id) = 0;

    // Verify signature for data; returns true if valid
    virtual bool verify(const std::vector<uint8_t>& data,
                        const std::vector<uint8_t>& signature,
                        const std::string& key_id) = 0;
};

// Factory helper: create a mock signing service for testing
std::shared_ptr<SigningService> createMockSigningService();

} // namespace themis
