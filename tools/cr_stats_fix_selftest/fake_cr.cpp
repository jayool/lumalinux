// Fake cloud_redirect.so for the CrStatsFix self-test: the exact exported
// signature/layout of StatsHandlers::HandleGetUserStats in CloudRedirect 2.6.5
// (RpcResult { PB::Writer { std::vector<uint8_t> }; int32_t eresult }), plus
// CR_GetVersion. Built stripped (-s) like the real release.
#include <cstdint>
#include <vector>
namespace PB {
struct Field { uint32_t fieldNum; uint32_t wireType; uint64_t varintVal; const uint8_t* data; size_t dataLen; };
class Writer { public: std::vector<uint8_t> buf_; };
}
namespace StatsHandlers {
struct RpcResult { PB::Writer body; int32_t eresult = 1; };
RpcResult HandleGetUserStats(uint32_t appId, const std::vector<PB::Field>& req) {
    (void)req;
    RpcResult r;
    switch (appId) {
        case 1: r.body.buf_ = {0x10, 0x00}; break;                         // empty store: crc 0 only
        case 2: r.body.buf_ = {0x10, 0x80, 0x01}; break;                   // "up to date", crc 128
        case 3: r.body.buf_ = {0x10, 0x00, 0x1a, 0x02, 0xaa, 0xbb}; break; // crc 0 but schema present
        default: break;                                                    // genuinely empty (0 bytes)
    }
    return r;
}
}
extern "C" const char* CR_GetVersion() { return "2.6.5-selftest"; }
