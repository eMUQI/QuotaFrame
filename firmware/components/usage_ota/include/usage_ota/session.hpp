#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "usage_ota/backend.hpp"
#include "usage_ota/power_gate.hpp"

namespace usage_panel {

/** Stable OTA phases exposed to the UI and Bridge status protocol. */
enum class OtaPhase : uint8_t {
    Idle,
    Confirming,
    Receiving,
    Verifying,
    Rebooting,
};

/** Stable OTA failure categories exposed to the UI and Bridge. */
enum class OtaError : uint8_t {
    None,
    Denied,
    Timeout,
    TooLarge,
    BadImage,
    LinkLost,
    SeqGap,
    LowPower,
    ProjectMismatch,
    TargetMismatch,
};

/** Thread-safe public snapshot of OTA state. */
struct OtaSnapshot {
    OtaPhase phase = OtaPhase::Idle;
    OtaError error = OtaError::None;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint64_t phase_started_ms = 0;  // Monotonic milliseconds.
    std::string version;
    // Counts failures since boot. A rejected retry can carry the same
    // OtaError as the attempt before it without the session ever reporting
    // OtaError::None in between, so the error alone cannot tell two failures
    // apart; this can.
    uint32_t failure_count = 0;
};

/**
 * Serializes one user-confirmed firmware update around an OtaBackend.
 *
 * Normal flow is Idle -> Confirming -> Receiving -> Verifying -> Rebooting.
 * Any validation, timeout, sequence, link, backend, or power-gate failure
 * resets the session to Idle while retaining a stable OtaError for
 * presentation.
 * `now_ms` values must all come from the same monotonic millisecond clock.
 */
class OtaSession {
public:
    explicit OtaSession(OtaBackend& backend) : backend_(backend) {}

    /**
     * Starts confirmation for a new image. Valid only while Idle.
     * The size, 64-character lowercase SHA-256 text, version, and backend
     * capacity are validated before the session enters Confirming.
     */
    bool begin(
        uint32_t size, const char* sha256_hex,
        const char* version, uint64_t now_ms);

    /** Authorizes the pending image and opens backend storage. */
    bool confirm(uint64_t now_ms);

    /** Rejects a pending confirmation and records Denied. */
    bool deny();

    /** Records a rejected manifest identity while the session is idle. */
    void reject_identity(bool project_matches);

    /**
     * Appends the next image chunk while Receiving.
     * `offset` must equal the current offset exactly; gaps or replays reset the
     * session with SeqGap instead of attempting to recover in place.
     */
    bool write(
        uint32_t offset, const uint8_t* data,
        size_t size, uint64_t now_ms);

    /**
     * Finalizes a fully received image and asks the backend to verify the
     * expected digest/version before entering Rebooting. Backend finalization
     * runs without holding the session mutex so snapshot readers do not block.
     */
    bool end();

    /** Cancels any active update and returns to Idle without an error. */
    void abort();

    /** Records LinkLost when a link drops during Confirming or Receiving. */
    void link_lost();

    /** Enforces confirmation and receive-inactivity timeouts. */
    void tick(uint64_t now_ms);

    OtaSnapshot snapshot() const;
    bool busy() const;

    /**
     * Wires the power gate consulted by begin() and confirm().
     * Assembly-time only: call during bring-up, before any BLE peer can
     * reach the manifest path. nullptr disables gating for targets whose
     * power state is not readable.
     */
    void set_power_source(OtaPowerSource* source);

private:
    static bool parse_sha256(
        const char* text, std::array<uint8_t, 32>& output);
    void reset_locked(OtaError error);
    bool power_allows_locked();

    OtaBackend& backend_;
    mutable std::mutex mutex_;
    OtaSnapshot state_{};
    std::array<uint8_t, 32> expected_sha256_{};
    uint64_t phase_started_ms_ = 0;
    uint64_t last_activity_ms_ = 0;
    bool backend_active_ = false;
    OtaPowerSource* power_source_ = nullptr;
};

}  // namespace usage_panel
