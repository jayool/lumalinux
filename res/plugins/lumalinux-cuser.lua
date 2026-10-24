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
-- what lumalinux needs. The hand-over is a call to lumalinux's luma_set_cuser
-- at the ADDRESS lumalinux publishes in its status.json ("cuser_handover"):
-- SLSsteam is an LD_AUDIT module and lives in the dynamic loader's audit
-- namespace, where ffi.C / dlsym cannot see the application's libraries
-- (measured 2026-10-24: "undefined symbol: luma_set_cuser" with lumalinux
-- loaded); a raw address call works, same process memory. status.json is
-- matched by pid so a stale file of an earlier Steam never gives an address.
-- Fail-safe: no engine yet, no user yet (not logged in), no status.json of
-- this process (lumalinux not loaded, or not yet) -> nothing happens and the
-- next event tries again. Handed over once per pointer, and again on every
-- SLSsteam config reload (lumalinux re-broadcasts after an add).
LumaCUser = LumaCUser or { setup = false, sent = nil }

local ffi = require("ffi")
pcall(ffi.cdef, "int getpid(void);")   -- pcall: a re-run of this file must not fail on the redefinition

-- lumalinux's status.json of THIS process: /run/user/<uid>/lumalinux/ (32-bit
-- client) or ~/.cache/lumalinux/ (64-bit client, inside the container), the
-- same two places LumaDeck looks. Returns the hand-over address or nil.
LumaCUser.handover = function()
    local pid = tonumber(ffi.C.getpid())
    local dirs = {}
    local rt = os.getenv("XDG_RUNTIME_DIR")
    if rt then dirs[#dirs + 1] = rt .. "/lumalinux" end
    local home = os.getenv("HOME")
    if home then dirs[#dirs + 1] = home .. "/.cache/lumalinux" end
    for _, d in ipairs(dirs) do
        local f = io.open(d .. "/status.json", "r")
        if f then
            local text = f:read("*a"); f:close()
            if tonumber(text:match('"pid":%s*(%d+)')) == pid then
                return text:match('"cuser_handover":%s*"(0x%x+)"')
            end
        end
    end
    return nil
end

LumaCUser.send = function(why)
    local engine = SLS.steamEngine
    if not engine then return end
    local user = engine:getUser(0)
    if not user then return end
    local cuser = ffi.cast("void**", user)[1]
    if cuser == nil then return end
    -- Every config reload is handed over, same pointer or not: lumalinux uses
    -- the reload that follows an add to repeat its broadcast (the 64-bit
    -- client ignores the one fired at the keys.txt write; see
    -- license_reconcile.cpp RebroadcastOnReload). Other events only once.
    if why ~= "configLoaded" and cuser == LumaCUser.sent then return end
    local addr = LumaCUser.handover()
    if not addr then
        log.debug("lumalinux-cuser: no lumalinux status.json for this process yet (" .. why .. ")")
        return
    end
    ffi.cast("void(*)(void*)", ffi.cast("uintptr_t", tonumber(addr)))(cuser)
    if cuser ~= LumaCUser.sent then
        log.info(string.format("lumalinux-cuser: CUser %s handed to lumalinux at %s (%s)", tostring(cuser), addr, why))
    else
        log.debug("lumalinux-cuser: config reload reported to lumalinux (" .. why .. ")")
    end
    LumaCUser.sent = cuser
end

if not LumaCUser.setup then
    LumaCUser.setup = true
    SLS.registerCallback("SLSsteam::initialized", function() LumaCUser.send("initialized") end)
    SLS.registerCallback("SLSsteam::configLoaded", function() LumaCUser.send("configLoaded") end)
end
LumaCUser.send("load")
