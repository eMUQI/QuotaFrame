#include <array>
#include <cstddef>
#include <cstdint>

#include "unity.h"
#include "usage_ota/backend.hpp"
#include "usage_ota/power_gate.hpp"
#include "usage_ota/session.hpp"

using namespace usage_panel;

namespace {

constexpr char kDigest[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

class FakeBackend final : public OtaBackend {
public:
    size_t capacity() const override { return slot_capacity; }

    bool begin(size_t size) override
    {
        begin_calls++;
        begun_size = size;
        return begin_result;
    }

    bool write(const uint8_t*, size_t size) override
    {
        write_calls++;
        written += size;
        return write_result;
    }

    bool finish(
        const std::array<uint8_t, 32>& expected_sha256,
        const char* expected_version) override
    {
        finish_calls++;
        finished_digest = expected_sha256;
        finished_version = expected_version ? expected_version : "";
        return finish_result;
    }

    void abort() override { abort_calls++; }

    size_t slot_capacity = 4096;
    bool begin_result = true;
    bool write_result = true;
    bool finish_result = true;
    unsigned begin_calls = 0;
    unsigned write_calls = 0;
    unsigned finish_calls = 0;
    unsigned abort_calls = 0;
    size_t begun_size = 0;
    size_t written = 0;
    std::array<uint8_t, 32> finished_digest{};
    std::string finished_version;
};

class FakePowerSource final : public OtaPowerSource {
public:
    bool ok = true;
    OtaPowerReading reading{};

    bool read(OtaPowerReading& out) override
    {
        if (!ok) return false;
        out = reading;
        return true;
    }
};

}  // namespace

TEST_CASE("OTA begin enters physical confirmation without touching flash", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);

    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 100));
    const OtaSnapshot snapshot = session.snapshot();

    TEST_ASSERT_EQUAL(int(OtaPhase::Confirming), int(snapshot.phase));
    TEST_ASSERT_EQUAL(int(OtaError::None), int(snapshot.error));
    TEST_ASSERT_EQUAL_UINT32(1024, snapshot.size);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.offset);
    TEST_ASSERT_EQUAL_STRING("0.5.0", snapshot.version.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, backend.begin_calls);
}

TEST_CASE("OTA begin rejects an image larger than the inactive slot", "[ota]")
{
    FakeBackend backend;
    backend.slot_capacity = 1000;
    OtaSession session(backend);

    TEST_ASSERT_FALSE(session.begin(1001, kDigest, "0.5.0", 0));

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::TooLarge), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(0, backend.begin_calls);
}

TEST_CASE("duplicate OTA begin preserves the active confirmation", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));

    TEST_ASSERT_FALSE(session.begin(2048, kDigest, "0.6.0", 1));

    const OtaSnapshot snapshot = session.snapshot();
    TEST_ASSERT_EQUAL(int(OtaPhase::Confirming), int(snapshot.phase));
    TEST_ASSERT_EQUAL(int(OtaError::None), int(snapshot.error));
    TEST_ASSERT_EQUAL_UINT32(1024, snapshot.size);
    TEST_ASSERT_EQUAL_STRING("0.5.0", snapshot.version.c_str());
}

TEST_CASE("physical confirmation begins the inactive OTA slot", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));

    TEST_ASSERT_TRUE(session.confirm(50));

    TEST_ASSERT_EQUAL(int(OtaPhase::Receiving), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL_UINT32(1, backend.begin_calls);
    TEST_ASSERT_EQUAL_UINT32(1024, backend.begun_size);
}

TEST_CASE("flash begin failure invalidates the requested image", "[ota]")
{
    FakeBackend backend;
    backend.begin_result = false;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));

    TEST_ASSERT_FALSE(session.confirm(50));

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::BadImage), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.begin_calls);
}

TEST_CASE("denying physical confirmation returns idle with denied error", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));

    TEST_ASSERT_TRUE(session.deny());

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::Denied), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(0, backend.abort_calls);
}

TEST_CASE("confirmation expires after sixty seconds", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 100));

    session.tick(60100);

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::Timeout), int(session.snapshot().error));
}

TEST_CASE("receiving expires after ten seconds without a block", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(100));

    session.tick(10100);

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::LinkLost), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
}

TEST_CASE("a tick sampled before the latest block keeps receiving", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(8, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(100));
    TEST_ASSERT_TRUE(session.write(0, bytes, sizeof(bytes), 5000));

    session.tick(4999);

    TEST_ASSERT_EQUAL(int(OtaPhase::Receiving), int(session.snapshot().phase));
    TEST_ASSERT_TRUE(session.write(4, bytes, sizeof(bytes), 5001));
}

TEST_CASE("a tick sampled before confirmation began keeps confirming", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 5000));

    session.tick(4999);

    TEST_ASSERT_EQUAL(int(OtaPhase::Confirming), int(session.snapshot().phase));
}

TEST_CASE("OTA data accepts only the exact next offset", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(8, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));
    TEST_ASSERT_TRUE(session.write(0, bytes, sizeof(bytes), 2));

    TEST_ASSERT_FALSE(session.write(3, bytes, sizeof(bytes), 3));

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::SeqGap), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
    TEST_ASSERT_EQUAL_UINT32(4, backend.written);
}

TEST_CASE("flash write failure invalidates the receiving session", "[ota]")
{
    FakeBackend backend;
    backend.write_result = false;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(4, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));

    TEST_ASSERT_FALSE(session.write(0, bytes, sizeof(bytes), 2));

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::BadImage), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
}

TEST_CASE("explicit abort invalidates a receiving session without an error", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(8, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));

    session.abort();

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::None), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
}

TEST_CASE("link loss immediately invalidates an active session", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    TEST_ASSERT_TRUE(session.begin(8, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));

    session.link_lost();

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::LinkLost), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
}

TEST_CASE("valid complete image selects reboot after digest verification", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(4, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));
    TEST_ASSERT_TRUE(session.write(0, bytes, sizeof(bytes), 2));

    TEST_ASSERT_TRUE(session.end());

    TEST_ASSERT_EQUAL(int(OtaPhase::Rebooting), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL_UINT32(1, backend.finish_calls);
    TEST_ASSERT_EQUAL_STRING("0.5.0", backend.finished_version.c_str());
    for (uint8_t byte : backend.finished_digest) {
        TEST_ASSERT_EQUAL_HEX8(0xAA, byte);
    }
}

TEST_CASE("OTA end rejects an incomplete declared image", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(8, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));
    TEST_ASSERT_TRUE(session.write(0, bytes, sizeof(bytes), 2));

    TEST_ASSERT_FALSE(session.end());

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::BadImage), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(1, backend.abort_calls);
    TEST_ASSERT_EQUAL_UINT32(0, backend.finish_calls);
}

TEST_CASE("failed image verification remains on the running image", "[ota]")
{
    FakeBackend backend;
    backend.finish_result = false;
    OtaSession session(backend);
    const uint8_t bytes[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(session.begin(4, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));
    TEST_ASSERT_TRUE(session.write(0, bytes, sizeof(bytes), 2));

    TEST_ASSERT_FALSE(session.end());

    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::BadImage), int(session.snapshot().error));
}

// EspOtaBackend hashes the incoming image with PSA rather than the mbedTLS
// SHA-256 API, which ESP-IDF 6 removed. A wrong call sequence would still
// compile and would reject every genuine update, so pin the digest of the
// published SHA-256 test vector against the exact sequence the backend uses.
TEST_CASE("the OTA hashing sequence matches the SHA-256 vector", "[ota]")
{
    static constexpr uint8_t kInput[] = {'a', 'b', 'c'};
    static constexpr std::array<uint8_t, 32> kExpected{
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

    TEST_ASSERT_EQUAL(PSA_SUCCESS, psa_crypto_init());

    psa_hash_operation_t operation{};
    TEST_ASSERT_EQUAL(
        PSA_SUCCESS, psa_hash_setup(&operation, PSA_ALG_SHA_256));
    TEST_ASSERT_EQUAL(
        PSA_SUCCESS, psa_hash_update(&operation, kInput, sizeof(kInput)));

    std::array<uint8_t, 32> actual{};
    size_t actual_length = 0;
    TEST_ASSERT_EQUAL(
        PSA_SUCCESS,
        psa_hash_finish(
            &operation, actual.data(), actual.size(), &actual_length));
    TEST_ASSERT_EQUAL_UINT32(actual.size(), actual_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kExpected.data(), actual.data(), actual.size());

    // Aborting a finished operation must stay safe: abort() runs on every
    // failure path and after every completed transfer.
    TEST_ASSERT_EQUAL(PSA_SUCCESS, psa_hash_abort(&operation));
}

TEST_CASE("power word packs and unpacks without loss", "[ota_power]")
{
    const OtaPowerReading decoded =
        unpack_power_word(pack_power_word(true, true, 37));

    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_TRUE(decoded.external_power);
    TEST_ASSERT_EQUAL_UINT8(37, decoded.percent);

    const OtaPowerReading minimal =
        unpack_power_word(pack_power_word(false, false, 0));
    TEST_ASSERT_FALSE(minimal.valid);
    TEST_ASSERT_FALSE(minimal.external_power);
    TEST_ASSERT_EQUAL_UINT8(0, minimal.percent);
}

TEST_CASE("atomic power source starts invalid and round-trips publishes", "[ota_power]")
{
    AtomicPowerSource source;
    OtaPowerReading reading{};

    TEST_ASSERT_TRUE(source.read(reading));
    TEST_ASSERT_FALSE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);

    source.publish(true, false, 41);
    TEST_ASSERT_TRUE(source.read(reading));
    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
    TEST_ASSERT_EQUAL_UINT8(41, reading.percent);

    source.publish(false, true, 0);
    TEST_ASSERT_TRUE(source.read(reading));
    TEST_ASSERT_FALSE(reading.valid);
    TEST_ASSERT_TRUE(reading.external_power);
}

TEST_CASE("OTA begin allows updates when no power source is assembled", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);

    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 100));
}

TEST_CASE("OTA begin rejects when the power source cannot be read", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    FakePowerSource source;
    source.ok = false;
    session.set_power_source(&source);

    TEST_ASSERT_FALSE(session.begin(1024, kDigest, "0.5.0", 100));
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(0, backend.begin_calls);
}

TEST_CASE("OTA begin fails closed on an invalid reading", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    FakePowerSource source;
    source.reading = OtaPowerReading{false, false, 39};
    session.set_power_source(&source);

    TEST_ASSERT_FALSE(session.begin(1024, kDigest, "0.5.0", 100));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(session.snapshot().error));
}

TEST_CASE("external power allows OTA regardless of percent", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    FakePowerSource source;
    source.reading = OtaPowerReading{true, true, 0};
    session.set_power_source(&source);

    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 100));
    TEST_ASSERT_EQUAL(int(OtaPhase::Confirming), int(session.snapshot().phase));
}

TEST_CASE("battery percent enforces the inclusive minimum", "[ota]")
{
    {
        FakeBackend backend;
        OtaSession session(backend);
        FakePowerSource source;
        source.reading = OtaPowerReading{true, false, 39};
        session.set_power_source(&source);

        TEST_ASSERT_FALSE(session.begin(1024, kDigest, "0.5.0", 100));
        TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(session.snapshot().error));
    }
    {
        FakeBackend backend;
        OtaSession session(backend);
        FakePowerSource source;
        source.reading = OtaPowerReading{true, false, 40};
        session.set_power_source(&source);

        TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 100));
    }
}

TEST_CASE("OTA confirm rechecks the power gate", "[ota]")
{
    FakeBackend backend;
    OtaSession session(backend);
    FakePowerSource source;
    source.reading = OtaPowerReading{true, false, 80};
    session.set_power_source(&source);

    TEST_ASSERT_TRUE(session.begin(1024, kDigest, "0.5.0", 0));
    source.reading = OtaPowerReading{false, false, 0};

    TEST_ASSERT_FALSE(session.confirm(2000));
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(session.snapshot().error));
    TEST_ASSERT_EQUAL_UINT32(0, backend.begin_calls);
}

TEST_CASE("content validation keeps precedence over the power gate", "[ota]")
{
    FakeBackend backend;
    backend.slot_capacity = 1000;
    OtaSession session(backend);
    FakePowerSource source;
    source.reading = OtaPowerReading{true, false, 10};
    session.set_power_source(&source);

    TEST_ASSERT_FALSE(session.begin(1001, kDigest, "0.5.0", 0));
    TEST_ASSERT_EQUAL(int(OtaError::TooLarge), int(session.snapshot().error));
}
