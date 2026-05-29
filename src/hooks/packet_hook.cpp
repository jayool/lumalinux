#include "packet_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>
#include <chrono>
#include <thread>

namespace {

// ── CProtoBufMsgBase layout (reversed from steamclient.so) ──────────────────
//   +0x14  uint16  type   (eMsg)
//   +0x20  void*   pBody  (parsed protobuf message)
constexpr uint32_t kOffMsgType = 0x14;
constexpr uint32_t kOffMsgBody = 0x20;

// ── CMsgClientGetAppOwnershipTicketResponse body offsets ────────────────────
// (protobuf-lite, old GCC ABI)
//   +0x14  uint32  app_id
//   +0x18  int32   eresult
constexpr uint32_t kOffBodyAppId   = 0x14;
constexpr uint32_t kOffBodyEResult = 0x18;

constexpr uint16_t EMSG_APPOWNERSHIPTICKET_RESPONSE = 858;
constexpr int32_t  ERESULT_OK = 1;

using InitFromPacketFn = void (*)(void* /*pMsg*/, void* /*pSrc*/);
InitFromPacketFn g_origFn = nullptr;

inline uint16_t MsgType(void* pMsg) {
    return *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(pMsg) + kOffMsgType);
}
inline void* MsgBody(void* pMsg) {
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pMsg) + kOffMsgBody);
}

void HandleTicket858(void* body) {
    uint32_t appId =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(body) + kOffBodyAppId);
    int32_t eresult =
        *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(body) + kOffBodyEResult);
    if (eresult == ERESULT_OK) return;

    // Only flip for apps that have at least one injectable depot in our
    // KeyStore — i.e. apps we're explicitly forcing. Avoids interfering with
    // tickets for apps we don't care about.
    if (KeyStore::GetDepotsForApp(appId).empty()) return;

    *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(body) + kOffBodyEResult) =
        ERESULT_OK;
    Log::Info("Packet: faked OK AppOwnershipTicket for app %u (was eresult=%d)",
              appId, eresult);
}

// Chained outer hook over SLSsteam's existing InitFromPacket hook.
// Flow: Steam → our jmp → HookFn → g_origFn (= our trampoline → SLSsteam jmp
// relocated by RelocateChainedJmp → SLSsteam HookFn → SLSsteam's trampoline →
// original InitFromPacket). Returns up the chain to us, where we post-process
// the parsed body for the 858 fake.
void HookFn(void* pMsg, void* pSrc) {
    if (g_origFn) g_origFn(pMsg, pSrc);
    if (!pMsg) return;
    if (MsgType(pMsg) != EMSG_APPOWNERSHIPTICKET_RESPONSE) return;
    void* body = MsgBody(pMsg);
    if (!body) return;
    HandleTicket858(body);
}

} // namespace

namespace Hooks::Packet {

bool Install() {
    uintptr_t target = Patterns::FindInitFromPacketFunction();
    if (!target) {
        Log::Error("Packet hook: cannot install — InitFromPacket not found");
        return false;
    }
    Log::Info("Packet hook: InitFromPacket resolved to 0x%lx, waiting for "
              "SLSsteam's detour...", (unsigned long)target);

    // Poll the prologue until SLSsteam has placed its E9 (chained jmp). Up to
    // 30 seconds — generous because SLSsteam can hook lazily on some flows.
    using namespace std::chrono_literals;
    volatile uint8_t* prologue = reinterpret_cast<volatile uint8_t*>(target);
    constexpr int kMaxWaitMs = 30000;
    constexpr int kPollMs    = 100;
    int waited = 0;
    while (*prologue != 0xE9 && waited < kMaxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        waited += kPollMs;
    }
    if (*prologue != 0xE9) {
        Log::Warn("Packet hook: SLSsteam detour on InitFromPacket did not appear "
                  "after %dms — SKIPPING packet hook. Without it, install will "
                  "fail with Access Denied, but Steam will not crash.", waited);
        return false;
    }
    Log::Info("Packet hook: SLSsteam detour detected on InitFromPacket after "
              "%dms — installing as outer chain", waited);

    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("Packet hook: LmHook::Install failed");
        return false;
    }
    g_origFn = reinterpret_cast<InitFromPacketFn>(tramp);
    Log::Info("Packet hook: INSTALLED (InitFromPacket target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::Packet
