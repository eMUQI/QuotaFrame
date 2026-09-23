#include "usage_ota/backend.hpp"

#include <cstring>

namespace usage_panel {

EspOtaBackend::EspOtaBackend(const char* expected_project_name)
    : expected_project_name_(expected_project_name)
{
    // A zeroed operation object is an inactive one, so the member's value
    // initialisation is all the setup PSA needs here.
}

EspOtaBackend::~EspOtaBackend() { abort(); }

size_t EspOtaBackend::capacity() const
{
    const esp_partition_t* partition =
        esp_ota_get_next_update_partition(nullptr);
    return partition ? partition->size : 0;
}

bool EspOtaBackend::begin(size_t size)
{
    abort();
    partition_ = esp_ota_get_next_update_partition(nullptr);
    if (!partition_ || size == 0 || size > partition_->size) return false;
    if (esp_ota_begin(partition_, size, &handle_) != ESP_OK) {
        partition_ = nullptr;
        handle_ = 0;
        return false;
    }
    // psa_crypto_init is idempotent, and nothing else in this firmware is
    // guaranteed to have called it before the first update arrives.
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&sha256_operation_, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        esp_ota_abort(handle_);
        partition_ = nullptr;
        handle_ = 0;
        return false;
    }
    active_ = true;
    sha256_active_ = true;
    return true;
}

bool EspOtaBackend::write(const uint8_t* data, size_t size)
{
    if (!active_ || !sha256_active_ || !data || size == 0) return false;
    if (esp_ota_write(handle_, data, size) != ESP_OK) return false;
    return psa_hash_update(&sha256_operation_, data, size) == PSA_SUCCESS;
}

bool EspOtaBackend::finish(
    const std::array<uint8_t, 32>& expected_sha256,
    const char* expected_version)
{
    if (!active_ || !partition_) return false;
    const esp_partition_t* completed_partition = partition_;
    std::array<uint8_t, 32> actual_sha256{};
    size_t actual_length = 0;
    const bool digest_valid = sha256_active_ &&
        psa_hash_finish(
            &sha256_operation_, actual_sha256.data(),
            actual_sha256.size(), &actual_length) == PSA_SUCCESS &&
        actual_length == actual_sha256.size();
    sha256_active_ = false;
    const esp_err_t end_result = esp_ota_end(handle_);
    active_ = false;
    handle_ = 0;
    partition_ = nullptr;
    if (end_result != ESP_OK || !digest_valid ||
        actual_sha256 != expected_sha256) {
        return false;
    }
    esp_app_desc_t app_description{};
    if (!expected_project_name_ || !expected_version ||
        esp_ota_get_partition_description(
            completed_partition, &app_description) != ESP_OK ||
        std::strncmp(
            app_description.project_name, expected_project_name_,
            sizeof(app_description.project_name)) != 0 ||
        std::strncmp(
            app_description.version, expected_version,
            sizeof(app_description.version)) != 0) {
        return false;
    }
    return esp_ota_set_boot_partition(completed_partition) == ESP_OK;
}

void EspOtaBackend::abort()
{
    if (active_) esp_ota_abort(handle_);
    active_ = false;
    handle_ = 0;
    partition_ = nullptr;
    sha256_active_ = false;
    // Safe on an already-terminated operation, and leaves it inactive so the
    // next begin() can set it up again.
    psa_hash_abort(&sha256_operation_);
}

}  // namespace usage_panel
