#include "usage_ota/session.hpp"
#include "usage_json.h"

#include "sdkconfig.h"

#include <cstring>
#include <utility>

namespace usage_panel {
namespace {

// Confirmation allows time for a person to act. Once bytes start flowing, a
// much shorter inactivity timeout releases the OTA partition after a dead link.
constexpr uint64_t kConfirmationTimeoutMs = 60000;
constexpr uint64_t kReceiveTimeoutMs = 10000;

int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

}  // namespace

bool OtaSession::begin(
    uint32_t size, const char* sha256_hex,
    const char* version, uint64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase != OtaPhase::Idle) return false;

    std::array<uint8_t, 32> digest{};
    if (size == 0 || !parse_sha256(sha256_hex, digest) ||
        !usage_version_valid(version)) {
        reset_locked(OtaError::BadImage);
        return false;
    }
    if (size > backend_.capacity()) {
        reset_locked(OtaError::TooLarge);
        return false;
    }

    if (!power_allows_locked()) {
        reset_locked(OtaError::LowPower);
        return false;
    }

    state_.phase = OtaPhase::Confirming;
    state_.error = OtaError::None;
    state_.offset = 0;
    state_.size = size;
    state_.version = version;
    expected_sha256_ = digest;
    phase_started_ms_ = now_ms;
    last_activity_ms_ = now_ms;
    return true;
}

bool OtaSession::confirm(uint64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase != OtaPhase::Confirming) return false;
    if (!power_allows_locked()) {
        reset_locked(OtaError::LowPower);
        return false;
    }
    if (!backend_.begin(state_.size)) {
        reset_locked(OtaError::BadImage);
        return false;
    }
    backend_active_ = true;
    state_.phase = OtaPhase::Receiving;
    phase_started_ms_ = now_ms;
    last_activity_ms_ = now_ms;
    return true;
}

bool OtaSession::deny()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase != OtaPhase::Confirming) return false;
    reset_locked(OtaError::Denied);
    return true;
}

bool OtaSession::write(
    uint32_t offset, const uint8_t* data,
    size_t size, uint64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase != OtaPhase::Receiving || data == nullptr || size == 0) {
        return false;
    }
    if (offset != state_.offset || size > state_.size - state_.offset) {
        // Folder Push is ordered; accepting a replay/gap would make the final
        // digest the only indication that transport state had diverged.
        reset_locked(OtaError::SeqGap);
        return false;
    }
    if (!backend_.write(data, size)) {
        reset_locked(OtaError::BadImage);
        return false;
    }
    state_.offset += static_cast<uint32_t>(size);
    last_activity_ms_ = now_ms;
    return true;
}

bool OtaSession::end()
{
    std::array<uint8_t, 32> digest{};
    std::string version;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.phase != OtaPhase::Receiving) return false;
        if (state_.offset != state_.size) {
            reset_locked(OtaError::BadImage);
            return false;
        }
        state_.phase = OtaPhase::Verifying;
        digest = expected_sha256_;
        version = state_.version;
    }

    // Verification may touch flash and image metadata. Do it outside the
    // mutex so UI/status readers can observe Verifying instead of blocking.
    const bool valid = backend_.finish(digest, version.c_str());

    std::lock_guard<std::mutex> lock(mutex_);
    backend_active_ = false;
    if (!valid) {
        reset_locked(OtaError::BadImage);
        return false;
    }
    state_.phase = OtaPhase::Rebooting;
    state_.error = OtaError::None;
    return true;
}

void OtaSession::abort()
{
    std::lock_guard<std::mutex> lock(mutex_);
    reset_locked(OtaError::None);
}

void OtaSession::link_lost()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase == OtaPhase::Confirming ||
        state_.phase == OtaPhase::Receiving) {
        reset_locked(OtaError::LinkLost);
    }
}

void OtaSession::tick(uint64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Callers sample now_ms before tick() while the BLE task may record a newer
    // time in begin() or write(); an unsigned difference would wrap to a timeout.
    if (state_.phase == OtaPhase::Confirming && now_ms > phase_started_ms_ &&
        now_ms - phase_started_ms_ >= kConfirmationTimeoutMs) {
        reset_locked(OtaError::Timeout);
    } else if (state_.phase == OtaPhase::Receiving && now_ms > last_activity_ms_ &&
               now_ms - last_activity_ms_ >= kReceiveTimeoutMs) {
        reset_locked(OtaError::LinkLost);
    }
}

OtaSnapshot OtaSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    OtaSnapshot snapshot = state_;
    snapshot.phase_started_ms = phase_started_ms_;
    return snapshot;
}

bool OtaSession::busy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.phase != OtaPhase::Idle;
}

void OtaSession::set_power_source(OtaPowerSource* source)
{
    // Intentionally unlocked: assembly happens during single-threaded
    // bring-up, before BLE starts accepting peers.
    power_source_ = source;
}

bool OtaSession::power_allows_locked()
{
    if (power_source_ == nullptr) return true;
    OtaPowerReading reading{};
    if (!power_source_->read(reading)) return false;
    if (!reading.valid) return false;
    if (reading.external_power) return true;
    return reading.percent >= CONFIG_USAGE_OTA_MIN_BATTERY_PERCENT;
}

bool OtaSession::parse_sha256(
    const char* text, std::array<uint8_t, 32>& output)
{
    if (!text || strlen(text) != 64) return false;
    for (size_t index = 0; index < output.size(); ++index) {
        const int high = hex_nibble(text[index * 2]);
        const int low = hex_nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

void OtaSession::reject_identity(bool project_matches)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.phase == OtaPhase::Idle)
        reset_locked(project_matches ? OtaError::TargetMismatch : OtaError::ProjectMismatch);
}

void OtaSession::reset_locked(OtaError error)
{
    if (backend_active_) {
        backend_.abort();
        backend_active_ = false;
    }
    state_.phase = OtaPhase::Idle;
    state_.error = error;
    if (error != OtaError::None) {
        ++state_.failure_count;
    }
    state_.offset = 0;
    state_.size = 0;
    state_.version.clear();
    expected_sha256_.fill(0);
    phase_started_ms_ = 0;
    last_activity_ms_ = 0;
}

}  // namespace usage_panel
