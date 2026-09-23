#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_ota_ops.h"
// PSA is the hashing API both mbedTLS 3.x (ESP-IDF 5.x) and TF-PSA-Crypto
// (ESP-IDF 6.x) provide. mbedtls/sha256.h exists only in the former.
#include "psa/crypto.h"

namespace usage_panel {

/** Storage/verification boundary used by the OTA session state machine. */
class OtaBackend {
public:
    virtual ~OtaBackend() = default;

    /** Returns the maximum image size currently writable by this backend. */
    virtual size_t capacity() const = 0;

    /** Starts a fresh image write of exactly `size` bytes. */
    virtual bool begin(size_t size) = 0;

    /** Appends one non-empty image chunk to the active write. */
    virtual bool write(const uint8_t* data, size_t size) = 0;

    /**
     * Finalizes the image, verifies its digest and embedded firmware identity,
     * and makes it the next boot partition only if every check succeeds.
     */
    virtual bool finish(
        const std::array<uint8_t, 32>& expected_sha256,
        const char* expected_version) = 0;

    /** Cancels any active write; safe to call when already inactive. */
    virtual void abort() = 0;
};

/** ESP-IDF OTA backend with streaming SHA-256 and image identity checks. */
class EspOtaBackend final : public OtaBackend {
public:
    /** `expected_project_name` must remain valid for this backend's lifetime. */
    explicit EspOtaBackend(const char* expected_project_name);
    ~EspOtaBackend() override;

    size_t capacity() const override;
    bool begin(size_t size) override;
    bool write(const uint8_t* data, size_t size) override;
    bool finish(
        const std::array<uint8_t, 32>& expected_sha256,
        const char* expected_version) override;
    void abort() override;

private:
    const esp_partition_t* partition_ = nullptr;
    esp_ota_handle_t handle_ = 0;
    bool active_ = false;
    psa_hash_operation_t sha256_operation_{};
    bool sha256_active_ = false;
    const char* expected_project_name_ = nullptr;
};

}  // namespace usage_panel
