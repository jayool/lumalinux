-- lumalinux-cuser.lua — SLSsteam plugin, installed by lumalinux's setup.sh into
-- ~/.config/SLSsteam/plugins/ (SLSsteam runs every .lua there; Plugins: yes).
--
-- Hands Steam's CUser object to lumalinux. lumalinux's license reconcile calls
-- Steam's CUser::NotifyLicensesUpdated right after it puts a freshly added game
-- into package 0: that is what makes Steam process the new licence at once
-- (depots mapped, the install works, no restart). The function takes the CUser
-- as its argument, and lumalinux has no way of its own to get it. On the 32-bit
-- client it also comes from lumalinux's patch of SLSsteam's achievement guard;
-- on the 64-bit client that patch does not apply (SLSsteam's build is stripped),
-- so without this plugin Reconcile never fires there. Measured 2026-10-24 on
-- the Deck beta (steamrt64): a game with no DLC, no shared depot and no app
-- token (Into the Breach) installed with "0 target depots" 2 of 2 times; with
-- the pointer handed over, it downloaded at the first try.
--
-- SLSsteam exposes the object as SLS.steamEngine:getUser(0). LuaBridge 3 keeps
-- the raw pointer right after the userdata's vtable (Userdata::m_p), which is
-- what lumalinux needs; LuaJIT's ffi reads it and calls lumalinux's exported
-- luma_set_cuser (lumalinux is in LD_PRELOAD, so ffi.C finds it). Fail-safe:
-- no engine yet, no user yet (not logged in), no lumalinux in this process ->
-- nothing happens and the next event tries again. Handed over once per pointer.
LumaCUser = LumaCUser or { setup = false, sent = nil }

local ffi = require("ffi")
pcall(ffi.cdef, "void luma_set_cuser(void*);")   -- pcall: a re-run of this file must not fail on the redefinition

LumaCUser.send = function(why)
    local engine = SLS.steamEngine
    if not engine then return end
    local user = engine:getUser(0)
    if not user then return end
    local cuser = ffi.cast("void**", user)[1]
    if cuser == nil or cuser == LumaCUser.sent then return end
    local ok, err = pcall(function() ffi.C.luma_set_cuser(cuser) end)
    if not ok then
        log.warn("lumalinux-cuser: lumalinux is not loaded in this process (" .. tostring(err) .. ")")
        LumaCUser.sent = cuser   -- do not retry on every event; a restart reloads this file
        return
    end
    LumaCUser.sent = cuser
    log.info(string.format("lumalinux-cuser: CUser %s handed to lumalinux (%s)", tostring(cuser), why))
end

if not LumaCUser.setup then
    LumaCUser.setup = true
    SLS.registerCallback("SLSsteam::initialized", function() LumaCUser.send("initialized") end)
    SLS.registerCallback("SLSsteam::configLoaded", function() LumaCUser.send("configLoaded") end)
end
LumaCUser.send("load")
