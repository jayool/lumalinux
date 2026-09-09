#pragma once
// EXPERIMENT probe: is ContentServerDirectory.GetCDNAuthToken ownership-gated?
// Gated OFF unless LUMA_CDNAUTH_PROBE=1. Build-pinned to steamclient 1788652215.
// See cdnauth_probe.cpp for the full rationale.
namespace Hooks::CdnAuthProbe {
    bool Install();
}
