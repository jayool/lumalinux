-- experiment_pkg_vectors.lua — sonda de SOLO LECTURA para el Accionable A.
--
-- Pregunta que responde
-- ---------------------
-- lumalinux inyecta los ids de depot en el vector de +0x38 (que nuestro
-- load_package_hook.hpp llama "AppIdVec"); el plugin download.lua los inyecta en
-- el de +0x48 ("DepotIdVec"). Las dos etiquetas vienen de Structs.h de LumaCore,
-- NO de simbolos de Valve, asi que "cual es el correcto" se apoya hoy en una
-- convencion heredada y no en evidencia.
--
-- Esta sonda no inyecta nada: engancha CPackageInfoCache::GetPackage y VUELCA
-- ambos vectores tal y como Steam los deja, para paquetes que el usuario POSEE
-- de verdad. Si en un paquete legitimo +0x38 contiene appIds y +0x48 depotIds,
-- queda demostrado (a) que las etiquetas son correctas y (b) que hoy estamos
-- metiendo depots en la lista de apps -- una confusion de tipos que funciona
-- porque el filtro de licencias solo compara numeros.
--
-- Ver docs/slssteam-plugins-analysis.md §3.4 y §7.
--
-- COMO USARLA
-- -----------
--   1. Entorno LIMPIO: SLSsteam SI, lumalinux NO. Si lumalinux ya esta
--      desplegado, su finder inyecta en +0x38 y contamina la muestra.
--      (Alternativa: LUMA_NO_PKG0_FINDER=1 + LUMA_NO_DEPOTKEY=1 antes de Steam.)
--   2. En ~/.config/SLSsteam/config.yaml:   Plugins: yes
--   3. cp este fichero a ~/.config/SLSsteam/plugins/
--   4. Reiniciar Steam y dejarlo asentarse ~30 s en la biblioteca.
--   5. Leer  ~/.SLSsteam.log   y buscar "PKGPROBE".
--
-- ES DESECHABLE: entorno de pruebas, nunca una Deck de usuario. Activar
-- `Plugins: yes` habilita ejecucion de codigo arbitrario dentro de Steam.

PkgProbe = PkgProbe or {
    setup   = false,
    hook    = nil,
    tramp   = nil,
    seen    = {},
    dumped  = 0,

    MAX_PACKAGES    = 12,     -- paquetes distintos a volcar (log legible)
    MAX_ENTRIES     = 24,     -- entradas por vector a imprimir
    IMPLAUSIBLE     = 4096,   -- size mayor que esto = offset mal, no leer
}

-- Guarda de inclusion. En release SLSsteam NO recrea el lua_State al recargar,
-- asi que sin esto cada evento de fichero volveria a enganchar. Es estructural.
if PkgProbe.setup then return end
PkgProbe.setup = true

local ffi = require("ffi")

ffi.cdef[[
    void* place_lua_hook(const int, const void*);

    typedef struct {
        uint32_t* mem;
        uint32_t  alloc;
        uint32_t  grow;
        uint32_t  size;
    } ProbeCUtlVector;

    typedef struct {
        uint32_t        packageId;    /* +0x00 */
        uint8_t         __pad[0x34];  /* +0x04 .. +0x37 */
        ProbeCUtlVector appIds;       /* +0x38  lumalinux: AppIdVec   (inyectamos AQUI) */
        ProbeCUtlVector depotIds;     /* +0x48  lumalinux: DepotIdVec (el plugin inyecta AQUI) */
    } ProbePackageInfo_t;

    typedef ProbePackageInfo_t*(*GetPackage_t)(void*, uint32_t, uint32_t, uint32_t);
]]

-- Autocomprobacion del layout: si mi aritmetica de padding esta mal, prefiero
-- saberlo aqui que leer basura de la memoria de Steam.
do
    local offA = ffi.offsetof("ProbePackageInfo_t", "appIds")
    local offD = ffi.offsetof("ProbePackageInfo_t", "depotIds")
    if offA ~= 0x38 or offD ~= 0x48 then
        log.notifyError(string.format(
            "PKGPROBE: layout mal (appIds=0x%x depotIds=0x%x, esperaba 0x38/0x48). Abortando.",
            offA, offD))
        return
    end
    log.info(string.format("PKGPROBE: layout ok (appIds=+0x%x depotIds=+0x%x, sizeof=%d)",
        offA, offD, ffi.sizeof("ProbePackageInfo_t")))
end

local mod = memhlp.getModule("steamclient.so")
if mod == nil then
    log.notifyError("PKGPROBE: no encuentro steamclient.so")
    return
end

-- Mismo sitio de llamada que usa download.lua para GetPackage. Anclar en la
-- LLAMADA (y no en el prologo del destino) es lo que hace que siga resolviendo
-- aunque otro haya enganchado la funcion -- ver §7.1.
local callSite = memhlp.patternScan("E8 ? ? ? ? 83 C4 ? 83 78 ? ? 0F 84", mod)
if callSite == nil then
    log.notifyError("PKGPROBE: patron de GetPackage no encontrado en este build")
    return
end

local getPackagePtr = memhlp.getJmpTarget(callSite)
if getPackagePtr == nil then
    log.notifyError("PKGPROBE: getJmpTarget fallo sobre el sitio de llamada")
    return
end
log.info("PKGPROBE: GetPackage @ " .. tostring(getPackagePtr))

PkgProbe.mutexReturn = function(value, mutex)
    mutex:unlock()
    return value
end

PkgProbe.dumpVec = function(label, vec)
    local mem, alloc, grow, size = vec.mem, vec.alloc, vec.grow, vec.size

    log.info(string.format("  %s  mem=%s alloc=%u grow=%u size=%u",
        label, tostring(mem), alloc, grow, size))

    if mem == nil or size == 0 then
        return
    end
    if size > PkgProbe.IMPLAUSIBLE then
        log.warn(string.format("  %s  size=%u inverosimil -> NO leo el contenido", label, size))
        return
    end

    local n = size
    if n > PkgProbe.MAX_ENTRIES then n = PkgProbe.MAX_ENTRIES end

    local parts = {}
    for i = 0, n - 1 do
        parts[#parts + 1] = tostring(mem[i])
    end

    local extra = ""
    if size > n then extra = string.format("  ... (+%u mas)", size - n) end
    log.info(string.format("  %s  [%s]%s", label, table.concat(parts, ", "), extra))
end

-- Volcado hexadecimal propio. NO usa memhlp.hexdump: ese binding espera un
-- `const void*` de LuaBridge y un cdata de FFI no se convierte (falla con
-- "The lua object can't be cast to desired type"). Leyendo los bytes nosotros
-- no dependemos de la capa de bindings.
PkgProbe.hexdump = function(ptr, size)
    local p = ffi.cast("const uint8_t*", ptr)
    local lines = {}
    for base = 0, size - 1, 16 do
        local hex, asc = {}, {}
        for i = 0, 15 do
            if base + i < size then
                local b = p[base + i]
                hex[#hex + 1] = string.format("%02x", b)
                asc[#asc + 1] = (b >= 0x20 and b < 0x7f) and string.char(b) or "."
            else
                hex[#hex + 1] = "  "
                asc[#asc + 1] = " "
            end
        end
        lines[#lines + 1] = string.format("    +%04x  %s  |%s|",
            base, table.concat(hex, " "), table.concat(asc))
    end
    return table.concat(lines, "\n")
end

PkgProbe.dump = function(pkgId, pPkg)
    log.info(string.format("=== PKGPROBE package %u @ %s ===", pkgId, tostring(pPkg)))

    -- El hexdump es la EVIDENCIA; los campos parseados son la interpretacion.
    -- Si el layout hubiera derivado, los campos daran basura y el hexdump lo dira.
    log.info("  raw +0x00..+0x5F:\n" .. PkgProbe.hexdump(pPkg, 0x60))

    PkgProbe.dumpVec("appIds   (+0x38)", pPkg.appIds)
    PkgProbe.dumpVec("depotIds (+0x48)", pPkg.depotIds)
end

PkgProbe.hkGetPackage = ffi.cast("GetPackage_t", function(pCache, pkgId, a2, a3)
    local mutex = LuaMutex()
    local pPkg = PkgProbe.tramp(pCache, pkgId, a2, a3)

    if pPkg ~= nil
       and not PkgProbe.seen[pkgId]
       and PkgProbe.dumped < PkgProbe.MAX_PACKAGES
    then
        PkgProbe.seen[pkgId] = true
        PkgProbe.dumped = PkgProbe.dumped + 1
        -- pcall solo cubre errores de Lua; las guardas de arriba son las que
        -- protegen de leer memoria mala.
        local ok, err = pcall(PkgProbe.dump, pkgId, pPkg)
        if not ok then log.error("PKGPROBE: dump fallo: " .. tostring(err)) end
    end

    return PkgProbe.mutexReturn(pPkg, mutex)
end)

PkgProbe.hook  = LuaHook("PkgProbe_GetPackage", getPackagePtr)
PkgProbe.tramp = ffi.cast("GetPackage_t",
    ffi.C.place_lua_hook(PkgProbe.hook.index, PkgProbe.hkGetPackage))

if PkgProbe.tramp == nil then
    log.notifyError("PKGPROBE: place_lua_hook fallo")
    return
end

log.notify("PKGPROBE cargada — abre la biblioteca y mira ~/.SLSsteam.log")
