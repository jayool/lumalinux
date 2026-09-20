// Separate TU so the call to HandleGetUserStats goes through the PLT exactly
// like CloudRedirect's TryHandleGetUserStats does.
#include <cstdint>
#include <vector>
namespace PB { struct Field { uint32_t fieldNum; uint32_t wireType; uint64_t varintVal; const uint8_t* data; size_t dataLen; }; class Writer { public: std::vector<uint8_t> buf_; }; }
namespace StatsHandlers {
struct RpcResult { PB::Writer body; int32_t eresult = 1; };
RpcResult HandleGetUserStats(uint32_t appId, const std::vector<PB::Field>& req);
}
extern "C" int TryHandle_test(uint32_t appId) {
    std::vector<PB::Field> f;
    auto res = StatsHandlers::HandleGetUserStats(appId, f);
    return static_cast<int>(res.body.buf_.size());
}
