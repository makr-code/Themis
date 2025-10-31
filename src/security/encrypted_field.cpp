#include "security/encryption.h"
#include <sstream>
#include <iomanip>

namespace themis {

// ===== Template Method Implementations =====

template<typename T>
void EncryptedField<T>::setFieldEncryption(std::shared_ptr<FieldEncryption> encryption) {
    field_encryption_ = encryption;
}

template<typename T>
EncryptedField<T>::EncryptedField() {}

template<typename T>
EncryptedField<T>::EncryptedField(const T& value, const std::string& key_id) {
    encrypt(value, key_id);
}

template<typename T>
EncryptedField<T>::EncryptedField(const EncryptedBlob& blob)
    : blob_(blob) {}

template<typename T>
void EncryptedField<T>::encrypt(const T& value, const std::string& key_id) {
    if (!field_encryption_) {
        throw std::runtime_error("FieldEncryption not set. Call setFieldEncryption() first.");
    }
    
    std::string serialized = serialize(value);
    blob_ = field_encryption_->encrypt(serialized, key_id);
}

template<typename T>
T EncryptedField<T>::decrypt() const {
    if (!field_encryption_) {
        throw std::runtime_error("FieldEncryption not set. Call setFieldEncryption() first.");
    }
    
    if (!hasValue()) {
        throw std::runtime_error("No encrypted value to decrypt");
    }
    
    std::string decrypted = field_encryption_->decryptToString(blob_);
    return deserialize(decrypted);
}

template<typename T>
bool EncryptedField<T>::hasValue() const {
    return !blob_.ciphertext.empty();
}

template<typename T>
bool EncryptedField<T>::isEncrypted() const {
    return !blob_.ciphertext.empty();
}

template<typename T>
std::string EncryptedField<T>::toBase64() const {
    return blob_.toBase64();
}

template<typename T>
EncryptedField<T> EncryptedField<T>::fromBase64(const std::string& b64) {
    return EncryptedField<T>(EncryptedBlob::fromBase64(b64));
}

template<typename T>
nlohmann::json EncryptedField<T>::toJson() const {
    return blob_.toJson();
}

template<typename T>
EncryptedField<T> EncryptedField<T>::fromJson(const nlohmann::json& j) {
    return EncryptedField<T>(EncryptedBlob::fromJson(j));
}

// ===== Type-Specific Serialization =====

// std::string specialization
template<>
std::string EncryptedField<std::string>::serialize(const std::string& value) {
    return value;
}

template<>
std::string EncryptedField<std::string>::deserialize(const std::string& str) {
    return str;
}

// int64_t specialization
template<>
std::string EncryptedField<int64_t>::serialize(const int64_t& value) {
    return std::to_string(value);
}

template<>
int64_t EncryptedField<int64_t>::deserialize(const std::string& str) {
    return std::stoll(str);
}

// double specialization
template<>
std::string EncryptedField<double>::serialize(const double& value) {
    std::ostringstream oss;
    oss << std::setprecision(17) << value;
    return oss.str();
}

template<>
double EncryptedField<double>::deserialize(const std::string& str) {
    return std::stod(str);
}

// Explicit template instantiations
template class EncryptedField<std::string>;
template class EncryptedField<int64_t>;
template class EncryptedField<double>;

}  // namespace themis
