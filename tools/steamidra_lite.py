#!/usr/bin/env python3
"""
steamidra_lite.py — replica el deploy de SteaMidra (Linux) con código mínimo.

Lee un .zip estilo Hubcap (nombre típico: {appid}.zip, contiene UN .lua y N
.manifest) y escribe en disco lo que necesita Steam para ofrecer el Install de
un juego no-owned cuando lumalinux + SLSsteam están cargados.

Acciones (orden = el flow de SteaMidra Linux en sff/ui.py:process_lua_full):
  1. Extrae los .manifest del ZIP a AMBOS ~/.local/share/Steam/depotcache/ y
     ~/.local/share/Steam/config/depotcache/ (Steam lee de cualquiera).
  2. Añade SOLO el AppID principal a AdditionalApps de
     ~/.config/SLSsteam/config.yaml (replica sff/app_injector/sls.py:add_ids;
     meter los depots ahí confunde a Steam).
  3. Escribe ~/.config/lumalinux/keys.txt:
       - Content depots → EXTENDED  (parent;gid;size;key)
       - Shared depots  → LEGACY    (solo depot;key, NO se inyectan)
       - AppID dummy    → LEGACY    (placeholder 000...0)
     Los shared se detectan del header "-- SHARED DEPOTS" del .lua de Hubcap.
     POR DEFECTO (no-pin) escribe gid=size=0 en los content depots → BuildDep
     hace passthrough → el juego se AUTO-ACTUALIZA siguiendo a Valve. Con --pin
     escribe el gid del zip → congela esa versión (juegos modeados, etc.). Ver
     docs/method.md §6.
  4. Inyecta las DecryptionKeys en ~/.local/share/Steam/config/config.vdf
     (POR DEFECTO; --no-vdf para saltar). Lo hace editando el VDF como texto
     (sin depender del módulo 'vdf' externo — ver update_config_vdf).
     Cierra Steam ANTES de correr el script (Steam reescribe config.vdf al
     salir).
  5. (opcional, --token APPID:HEX) Añade un AppToken al config.yaml de SLSsteam.
  6. Resetea el error-state del .acf SI YA EXISTE (sff/lua/writer.py:
     _patch_acf_error_state). UpdateResult, Bytes*, StagingSize → 0;
     StateFlags &= ~16. Sin este paso, Steam suele mostrar "NO INTERNET
     CONNECTION" al primer Install tras cualquier fallo previo (el bug está en
     cómo Steam interpreta UpdateResult stale, no en la red — verbatim del
     comment del propio SteaMidra). Si no hay .acf, NO escribimos ninguno:
     lo crea Steam al pulsar Install, en la biblioteca que elija el usuario.

Uso:
  python3 steamidra_lite.py 2379780.zip            # no-pin (auto-update, DEFAULT)
  python3 steamidra_lite.py 2379780.zip --pin      # congela la versión del zip
  python3 steamidra_lite.py 2379780.zip --no-vdf
  python3 steamidra_lite.py 2379780.zip --token 2379780:0x123abc...

  # también acepta un .lua + dir manifests si no tienes ZIP:
  python3 steamidra_lite.py game.lua --manifests-dir ./manifests/

  # modo post-instalación: registra un juego YA descargado para que la app
  # ACCELA standalone lo liste (lee el installdir real del .acf + el .lua de
  # stplug-in). No instala nada. Pensado para invocarlo tras terminar el Install.
  python3 steamidra_lite.py --accela-mark 2379780

  # modos sin-zip de pin/unpin para un juego YA desplegado (para LumaDeck).
  # Cortan al principio (no abren zip, solo editan keys.txt / leen el .acf):
  python3 steamidra_lite.py --pin-installed 2379780   # congela la versión instalada
  python3 steamidra_lite.py --unpin 2379780           # vuelve a auto-update
  python3 steamidra_lite.py --pin-status 2379780      # imprime {appid,pinned,depots}

NO escribe el .acf — Steam lo escribe solo cuando pulses Install.
"""

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

# ─── Verbatim de SteaMidra ────────────────────────────────────────────────
#
# Lo que sigue (DepotKeyPair, LuaParsedInfo + los 4 regex + parse_lua_contents)
# está copiado literal de la rama main de Midrags/SFF:
#   sff/structs.py (DepotKeyPair: líneas 319-326, LuaParsedInfo: 337-343)
#   sff/lua/manager.py (regexes: 42-54, parse_lua_contents: 57-72)
#
# Sin reescribir nada. Si SteaMidra cambia su parser, hay que re-copiar.

@dataclass
class DepotKeyPair:
    """A depot and its decryption key"""
    depot_id: str
    "Depot ID"
    decryption_key: str
    "Decryption Key of the Depot. Can be blank if it's not a depot"


@dataclass
class _RawLua:
    path: Path
    contents: str


@dataclass
class LuaParsedInfo(_RawLua):
    app_id: str
    "The base app ID"
    depots: list
    manifest_overrides: dict = field(default_factory=dict)
    "depot_id -> manifest_gid pins from setManifestid() Lua calls"


_DEPOT_NO_KEY_REGEX = re.compile(
    r"^\s*addappid\s*\(\s*(\d+)\s*\)", flags=re.MULTILINE
)
_DEPOT_DEC_KEY_REGEX = re.compile(
    r"^\s*addappid\s*\(\s*(\d+)\s*,\s*\d\s*,\s*(?:\"|\')(\S+)(?:\"|\')\s*\)",
    flags=re.MULTILINE,
)
_GENERAL_ADDAPPID_REGEX = re.compile(r"^\s*addappid\s*\(\s*(\d+)", flags=re.MULTILINE)
_SETMANIFESTID_REGEX = re.compile(
    # Accept both 2-arg and 3-arg forms. Hubcap currently writes
    #   setManifestid(depot, "gid", size)
    # (the size is the third argument). LumaCore's Lua interpreter accepts
    # both shapes and ignores the third arg (size is forced to 0 internally,
    # see LuaLoader.cpp:187). We mirror that: capture depot + gid, ignore size.
    # SteaMidra's upstream regex still requires `)` immediately after the gid
    # quote, so 3-arg .luas would silently match nothing there too; we diverge
    # from upstream deliberately on this one.
    r"^\s*setManifestid\s*\(\s*(\d+)\s*,\s*[\"'](\d+)[\"']\s*(?:,\s*\d+\s*)?\)",
    flags=re.MULTILINE,
)


def parse_lua_contents(contents, path):
    """
    Parse Lua contents into LuaParsedInfo without prompts.
    Returns None if parsing fails (no app ID or no decryption keys).
    """
    if not (any_addappid := _GENERAL_ADDAPPID_REGEX.search(contents)):
        return None
    app_id = any_addappid.group(1)
    ids_with_no_key = _DEPOT_NO_KEY_REGEX.findall(contents)
    depot_dec_key = _DEPOT_DEC_KEY_REGEX.findall(contents)
    if not depot_dec_key:
        return None
    depot_pairs = [DepotKeyPair(*x) for x in depot_dec_key]
    depot_pairs.extend([DepotKeyPair(x, "") for x in ids_with_no_key])
    manifest_overrides = dict(_SETMANIFESTID_REGEX.findall(contents))
    return LuaParsedInfo(path, contents, app_id, depot_pairs, manifest_overrides)


# ─── Fin verbatim de SteaMidra ────────────────────────────────────────────


# Shared depots are redists (VC++, DirectX, etc.) that belong to ANOTHER app
# (228980 = "Steamworks Common Redistributables") which EVERY Steam account
# owns. Hubcap marks them with a "-- SHARED DEPOTS" section header.
#
# These must NOT be served by lumalinux's DepotKey hook: Steam already owns
# them and has their keys, and intercepting LoadDepotDecryptionKey for an
# owned depot corrupts Steam's heap (verified empirically — the codespace
# reproduction crashed with `free(): invalid pointer` while serving 228989/
# 228990, and stopped crashing + downloaded+decrypted FL the moment those two
# were removed from keys.txt). This mirrors LumaCore, whose HasDepot() excludes
# owned apps (MarkOwned) so it never serves keys for them.
_SHARED_SECTION_RE = re.compile(r"^\s*--\s*SHARED\s+DEPOTS", re.IGNORECASE | re.MULTILINE)
_SECTION_HEADER_RE = re.compile(r"^\s*--\s*[A-Z]", re.MULTILINE)
_ADDAPPID_ANY_RE = re.compile(r"^\s*addappid\s*\(\s*(\d+)", re.MULTILINE)


def parse_shared_depots(lua_text):
    """Return the set of depot ids under the `-- SHARED DEPOTS` section.

    The section runs from the `-- SHARED DEPOTS` header to the next top-level
    `-- SOMETHING` comment header (e.g. `-- DLCS WITHOUT...`) or EOF. Every
    addappid() in that range is a shared depot. Empty set if no such section
    (older .luas, or games with no shared redists like Mina)."""
    m = _SHARED_SECTION_RE.search(lua_text)
    if not m:
        return set()
    region_start = m.end()
    # Find the next section header after the SHARED DEPOTS one.
    nxt = _SECTION_HEADER_RE.search(lua_text, region_start)
    region_end = nxt.start() if nxt else len(lua_text)
    region = lua_text[region_start:region_end]
    return {int(x) for x in _ADDAPPID_ANY_RE.findall(region)}


# Static fallback to the dynamic `-- SHARED DEPOTS` detection above: the depots of
# app 228980 "Steamworks Common Redistributables" (VC++, DirectX, .NET, XNA, PhysX,
# OpenAL, …) that every account owns and that must ALWAYS auto-update — pinning them
# to an old manifest is pointless and can churn/re-download. parse_shared_depots only
# catches these when the .lua carries a `-- SHARED DEPOTS` header (Hubcap does;
# LuaTools/FreeTP .luas often don't), so they'd otherwise slip through as content
# depots and get pinned. Mirrors LumaCore's kAutoUpdateDepots (228980 range only; its
# lone 220211 left out until identified). Belt-and-suspenders — never pins a known
# redist regardless of .lua format.
_KNOWN_REDIST_DEPOTS = {
    228981, 228982, 228983, 228984, 228985, 228986, 228987, 228988, 228989, 228990,
    229000, 229001, 229002, 229003, 229004, 229005, 229006, 229007,
    229010, 229011, 229012, 229020, 229030, 229031, 229032, 229033,
}


_MANIFEST_NAME_RE = re.compile(r"^(\d+)_(\d+)\.manifest$")

# Steam manifest binary format magic numbers (Source SDK / SteamKit)
_PROTOBUF_PAYLOAD_MAGIC   = 0x71F617D0
_PROTOBUF_METADATA_MAGIC  = 0x1F4812BE
_PROTOBUF_SIGNATURE_MAGIC = 0x1B81B817


def parse_manifest_gid_from_name(filename):
    """Devuelve (depot_id, manifest_gid) si el nombre matchea {depot}_{gid}.manifest, o None."""
    m = _MANIFEST_NAME_RE.match(filename)
    if m: return int(m.group(1)), int(m.group(2))
    return None


def _read_varint(buf, pos):
    """Read protobuf varint starting at buf[pos]. Returns (value, num_bytes)."""
    val = 0
    shift = 0
    n = 0
    while pos + n < len(buf) and n < 10:
        b = buf[pos + n]
        n += 1
        val |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            return val, n
        shift += 7
    return 0, n


def _find_pb_field(buf, target_field_num, target_wire_type):
    """Walk a protobuf-encoded buffer looking for a field. Returns the value or 0."""
    i = 0
    while i < len(buf):
        tag, n = _read_varint(buf, i)
        if n == 0: break
        i += n
        fn = tag >> 3
        wt = tag & 7
        if fn == target_field_num and wt == target_wire_type:
            val, _ = _read_varint(buf, i)
            return val
        # skip
        if wt == 0:  # varint
            _, n = _read_varint(buf, i); i += n
        elif wt == 1:  # 64-bit fixed
            i += 8
        elif wt == 2:  # length-delimited
            length, n = _read_varint(buf, i); i += n + length
        elif wt == 5:  # 32-bit fixed
            i += 4
        else:
            return 0
    return 0


def parse_manifest_size(manifest_path):
    """Parse Steam .manifest binary, return cb_disk_original (field 5 of
    ContentManifestMetadata). Returns 0 if parsing fails."""
    try:
        data = manifest_path.read_bytes()
    except Exception:
        return 0
    # Find the metadata magic
    marker = _PROTOBUF_METADATA_MAGIC.to_bytes(4, 'little')
    idx = data.find(marker)
    if idx < 0:
        return 0
    # Size of the metadata block (uint32 LE right after the magic)
    if idx + 8 > len(data):
        return 0
    meta_size = int.from_bytes(data[idx+4:idx+8], 'little')
    meta = data[idx+8:idx+8+meta_size]
    # cb_disk_original is field 5, wire type 0 (varint)
    return _find_pb_field(meta, 5, 0)


def _write_manifest_both(data, base, depotcache, config_depotcache):
    """Escribe un .manifest a depotcache/ Y a config/depotcache/.
    SteaMidra (sff/steam_tools_compat.py:sync_manifest_to_config_depotcache)
    coloca los manifests en AMBOS sitios — Steam lee de cualquiera de los dos
    según la fase, y sincronizarlos evita un 'Missing manifest' intermitente."""
    dest = depotcache / base
    dest.write_bytes(data)
    if config_depotcache is not None:
        try:
            (config_depotcache / base).write_bytes(data)
        except OSError:
            pass
    return dest


def extract_zip(zip_path, depotcache, config_depotcache):
    """Extrae el .lua y copia .manifest files a depotcache (+ config/depotcache).
    Devuelve (lua_text, manifests_copied, manifests_from_names, manifest_sizes)."""
    depotcache.mkdir(parents=True, exist_ok=True)
    if config_depotcache is not None:
        config_depotcache.mkdir(parents=True, exist_ok=True)
    lua_text = None
    copied = []
    manifests_from_names = {}
    manifest_sizes = {}  # {depot_id: cb_disk_original}
    with zipfile.ZipFile(zip_path) as zf:
        for member in zf.namelist():
            if member.endswith("/"):
                continue
            base = Path(member).name
            if base.endswith(".lua"):
                if lua_text is None:
                    lua_text = zf.read(member).decode("utf-8", errors="ignore")
            elif base.endswith(".manifest"):
                data = zf.read(member)
                dest = _write_manifest_both(data, base, depotcache, config_depotcache)
                copied.append(base)
                parsed = parse_manifest_gid_from_name(base)
                if parsed:
                    depot_id = parsed[0]
                    manifests_from_names[depot_id] = parsed[1]
                    size = parse_manifest_size(dest)
                    if size > 0:
                        manifest_sizes[depot_id] = size
    if lua_text is None:
        sys.exit(f"ERROR: el .zip {zip_path} no contiene ningún .lua")
    return lua_text, copied, manifests_from_names, manifest_sizes


def copy_manifests_from_dir(manifests, src_dir, depotcache, config_depotcache):
    """Modo legacy: copia desde dir suelto a depotcache (+ config/depotcache)."""
    depotcache.mkdir(parents=True, exist_ok=True)
    if config_depotcache is not None:
        config_depotcache.mkdir(parents=True, exist_ok=True)
    copied, missing = [], []
    for depot_id, gid in manifests.items():
        target_name = f"{depot_id}_{gid}.manifest"
        candidates = list(src_dir.glob(f"{depot_id}_*.manifest")) + \
                     list(src_dir.glob(f"*{gid}*.manifest"))
        if not candidates:
            missing.append((depot_id, gid))
            continue
        data = candidates[0].read_bytes()
        _write_manifest_both(data, target_name, depotcache, config_depotcache)
        copied.append(target_name)
    return copied, missing


def prune_stale_manifests(depotcache, config_depotcache, keep):
    """Modo NO-PIN: borra de depotcache (+ config/depotcache) los manifests
    VIEJOS de los content depots de este juego, dejando SOLO el gid que acabamos
    de seedear (`keep` = {depot_id: gid_del_zip}). Sin esto, un manifest pinneado
    viejo de una instalación previa puede reusarse y bloquear el auto-update — es
    el 'nuke your depotcache' del flujo de SteaMidra, pero scoped a este juego
    (no toca depots de otros juegos)."""
    removed = []
    for depot_id, keep_gid in keep.items():
        for base in (depotcache, config_depotcache):
            if base is None or not base.exists():
                continue
            for f in base.glob(f"{depot_id}_*.manifest"):
                parsed = parse_manifest_gid_from_name(f.name)
                if parsed and parsed[1] != int(keep_gid):
                    try:
                        f.unlink()
                        removed.append(f.name)
                    except OSError:
                        pass
    return removed


def update_sls_yaml(yaml_path, all_appids, app_tokens=None):
    """Añade appids a AdditionalApps (y opcional tokens a AppTokens). Idempotente, hace .bak."""
    yaml_path.parent.mkdir(parents=True, exist_ok=True)
    if not yaml_path.exists():
        yaml_path.write_text(
            "PlayNotOwnedGames: yes\n"
            "DisableFamilyShareLock: yes\n"
            "AdditionalApps:\n"
            "AppTokens:\n",
            encoding="utf-8")

    shutil.copy2(yaml_path, yaml_path.with_suffix(".yaml.bak"))
    lines = yaml_path.read_text(encoding="utf-8").splitlines()

    def insert_into_section(section_name, new_items, item_format):
        """Inserta items justo después de `section_name:`. new_items debe ser
        lista de tuplas o ints. item_format es callable -> str de la línea a insertar."""
        nonlocal lines
        try:
            idx = next(i for i, l in enumerate(lines) if l.strip().startswith(f"{section_name}:"))
        except StopIteration:
            lines.append(f"{section_name}:")
            idx = len(lines) - 1
        existing = set()
        scan = idx + 1
        while scan < len(lines) and (lines[scan].startswith(" ") or lines[scan].startswith("\t") or not lines[scan].strip()):
            m1 = re.match(r"^\s*-\s*(\d+)", lines[scan])
            m2 = re.match(r"^\s*(\d+)\s*:", lines[scan])
            if m1: existing.add(int(m1.group(1)))
            if m2: existing.add(int(m2.group(1)))
            scan += 1
        to_add = [x for x in new_items if (x if isinstance(x, int) else x[0]) not in existing]
        if to_add:
            insertion = [item_format(x) for x in to_add]
            lines = lines[:idx+1] + insertion + lines[idx+1:]
        return len(to_add)

    n_apps = insert_into_section("AdditionalApps", all_appids, lambda n: f"  - {n}")
    n_tokens = 0
    if app_tokens:
        n_tokens = insert_into_section("AppTokens", list(app_tokens.items()),
                                       lambda t: f"  {t[0]}: {t[1]}")

    yaml_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return n_apps, n_tokens


def write_lumalinux_keys(keys_path, depot_keys, manifests, manifest_sizes, app_id, pin):
    """Mezcla con lo existente (no rompe entries de otros juegos) y escribe:

    - Cada depot con key (distinto del AppID) → EXTENDED:
        depot_id;parent_app_id;manifest_gid;manifest_size;hex_key
      lumalinux LoadPackage inyecta estos en PackageId=0 si parent==app_id;
      BuildDep parchea los que Steam ya tenga en pDepotInfo.

      PIN vs NO-PIN (parámetro `pin`):
        - pin=True  → manifest_gid/size = los del zip → BuildDep fija esa versión
          (el juego NO se auto-actualiza; queda congelado en el manifest del zip).
        - pin=False (DEFAULT) → manifest_gid=size=0 → BuildDep hace passthrough →
          Steam sigue el manifest actual de Valve y se auto-actualiza. La key se
          escribe igual (es por depot y estable entre versiones; descifra cualquier
          versión). Ver docs/method.md §6.

    - El AppID principal → entry LEGACY con key (la del .lua si la trae, o
      000...000 como dummy). Steam pregunta por la 'key del AppID' durante
      el bootstrap del install; el DepotKey hook responde para que el flow
      no se quede esperando.

    TODOS los depots (content y shared) van a keys.txt. El hook DepotKey de
    lumalinux v1.0 hookea la KeyValues accessor interna (como LumaCore), así que
    servir la key de cualquier depot — owned o no — solo responde la query y NO
    corrompe el heap. Para un shared depot owned (228989/228990) servir nuestra
    key es redundante pero inofensivo; para uno que NO poseas, es necesario. Sin
    lista estática: el hook sirve lo que esté en keys.txt y hace passthrough del
    resto. Espejo de LumaCore (DepotKeys.cpp sirve todo el DepotKeySet)."""
    keys_path.parent.mkdir(parents=True, exist_ok=True)
    # Merge with existing entries (preserves entries from previous runs for other apps)
    existing = {}  # {depot_id: tuple — variants below}
    if keys_path.exists():
        for line in keys_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"): continue
            parts = line.split(";")
            try:
                if len(parts) == 5:
                    did = int(parts[0])
                    existing[did] = ("ext", int(parts[1]), int(parts[2]), int(parts[3]), parts[4])
                elif len(parts) == 2:
                    did = int(parts[0])
                    # `<did>;` with empty key field = presence-only (no-key) entry.
                    if parts[1] == "":
                        existing[did] = ("leg_no_key",)
                    else:
                        existing[did] = ("leg", parts[1])
            except ValueError:
                continue

    added = 0
    updated = 0
    app_id_key_from_lua = None
    for did, key in depot_keys.items():
        if did == app_id:
            # The .lua may include `addappid(APP_ID, 1, "real_key")` (Hubcap
            # does this for Formula Legends and similar). LumaCore keeps that
            # real key in its DepotKeySet — we mirror that here as a LEGACY
            # entry. We only filter the AppID out of `config.vdf` (where it
            # genuinely breaks Steam — confirmed by FL vs Mina); keeping the
            # real key in keys.txt is harmless because the AppID is never
            # queried as a depot key during download.
            app_id_key_from_lua = key
            continue
        # NO-PIN (default): gid/size = 0 → BuildDep passthrough → auto-update.
        # PIN: usa el gid/size del zip → BuildDep congela esa versión — EXCEPTO los
        # redistribuibles compartidos (228980), que nunca se pinean aunque pin=True
        # (siempre auto-update). Fallback estático a `-- SHARED DEPOTS`.
        pin_this = pin and did not in _KNOWN_REDIST_DEPOTS
        gid = manifests.get(did, 0) if pin_this else 0
        size = manifest_sizes.get(did, 0) if pin_this else 0
        new_entry = ("ext", app_id, gid, size, key)
        if did not in existing:
            existing[did] = new_entry
            added += 1
        elif existing[did] != new_entry:
            existing[did] = new_entry
            updated += 1

    # AppID entry. Two cases, both mirror LumaCore:
    #   - .lua has `addappid(APP_ID, 1, "real_key")` → LEGACY with the real key.
    #     DepotKey hook will serve it if Steam asks (same as LumaCore).
    #   - .lua has `addappid(APP_ID)` (no key) → presence-only entry written
    #     as `APP_ID;` with the empty key field. KeyStore loads it with
    #     has_key=false so Lookup returns nullopt and the DepotKey hook
    #     passthroughs to Steam's original — matches LumaCore's
    #     DepotKeySet[id] = "" semantics. The id is still returned by
    #     GetAllDepotIds() so LoadPackage injects it into AppIdVec.
    new_app_entry = ("leg", app_id_key_from_lua) if app_id_key_from_lua else ("leg_no_key",)
    if app_id not in existing:
        existing[app_id] = new_app_entry
        added += 1
    elif existing[app_id] != new_app_entry:
        existing[app_id] = new_app_entry
        updated += 1

    lines = [
        "# lumalinux keys.txt — managed by steamidra_lite.py",
        "# EXTENDED: depot_id;parent_app_id;manifest_gid;manifest_size;hex_key   (content depots — lumalinux inyecta en pDepotInfo)",
        "# LEGACY:   depot_id;hex_key                                            (AppID with key — lumalinux solo sirve la key, no inyecta)",
        "# NO-KEY:   depot_id;                                                   (AppID sin key en el .lua — lumalinux inyecta el id pero el hook hace passthrough; espejo del DepotKeySet[id]=\"\" de LumaCore)",
        "# Todos los depots (content y shared) van aquí: el hook DepotKey v1.0 hookea la KeyValues accessor interna y sirve cualquiera sin corromper (como LumaCore).",
    ]
    for did in sorted(existing):
        e = existing[did]
        if e[0] == "ext":
            _, parent, gid, size, key = e
            lines.append(f"{did};{parent};{gid};{size};{key}")
        elif e[0] == "leg_no_key":
            lines.append(f"{did};")
        else:
            _, key = e
            lines.append(f"{did};{key}")
    keys_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return added, updated


def update_config_vdf(vdf_path, depot_keys):
    """Añade DecryptionKey al config.vdf de Steam sin depender del módulo
    'vdf' externo (SteamOS no lo trae y no se puede pip-install fácilmente).

    En vez de parsear el VDF completo (formato delicado, riesgo de corromper
    config.vdf), encontramos el bloque depots literalmente como texto e
    insertamos los nuevos bloques justo antes del cierre del bloque. Es
    exactamente la técnica del Python inline que probamos a mano para Mina y
    funcionó (las 5 DecryptionKeys quedaron correctamente metidas)."""
    if not vdf_path.exists():
        print(f"  [SKIP] config.vdf no existe en {vdf_path}. ¿Steam nunca arrancó?")
        return 0
    shutil.copy2(vdf_path, vdf_path.with_suffix(".vdf.bak"))
    txt = vdf_path.read_text(encoding="utf-8")

    # Find the "depots" block inside InstallConfigStore > Software > Valve > Steam
    m = re.search(r'^(\s*)"depots"\s*\n\s*\{', txt, re.MULTILINE)
    if not m:
        print(f"  [SKIP] No se encontró el bloque 'depots' en {vdf_path}.")
        return 0
    depot_block_indent = m.group(1)
    child_indent = depot_block_indent + "\t"
    inner_indent = child_indent + "\t"

    # Find the matching closing brace of the depots block
    depth = 0
    i = m.end() - 1
    end = -1
    while i < len(txt):
        if txt[i] == '{':
            depth += 1
        elif txt[i] == '}':
            depth -= 1
            if depth == 0:
                end = i
                break
        i += 1
    if end < 0:
        print(f"  [SKIP] Cierre del bloque 'depots' no encontrado en {vdf_path}.")
        return 0

    existing_section = txt[m.end():end]
    new_blocks = ""
    added = 0
    for did, key in depot_keys.items():
        sid = str(did)
        if re.search(r'^\s*"' + re.escape(sid) + r'"\s*$', existing_section, re.MULTILINE):
            continue  # ya está
        new_blocks += (
            f'{child_indent}"{sid}"\n'
            f'{child_indent}{{\n'
            f'{inner_indent}"DecryptionKey"\t\t"{key}"\n'
            f'{child_indent}}}\n'
        )
        added += 1

    if new_blocks:
        txt = txt[:end] + new_blocks + txt[end:]
        vdf_path.write_text(txt, encoding="utf-8")
    return added


def parse_token_arg(s):
    """'APPID:HEX' -> (appid_int, hex_str)"""
    try:
        a, t = s.split(":", 1)
        return int(a), t.strip()
    except Exception:
        sys.exit(f"ERROR: --token mal formado: '{s}'. Usa APPID:HEX")


# ── .acf error-state handling (replica sff/lua/writer.py:_patch_acf_error_state) ──
#
# After a failed install, Steam writes UpdateResult=<errcode> + StateFlags|=16
# (Update Required) in the appmanifest_<appid>.acf. On the NEXT Steam launch
# the client reads that stale error and shows "NO INTERNET CONNECTION"
# (regardless of whether the network is actually up). The fix SteaMidra applies
# is to reset those fields to 0 before the user clicks Install again. See the
# verbatim comment in sff/lua/writer.py:113 ("this is what causes 'NO INTERNET
# CONNECTION'"), corroborated by our own end-to-end run (RESEARCH.md §12.6: after
# a completed install Steam queues an auto-update that returns UpdateResult=8).
#
# Two fields SFF resets are DELIBERATELY not here, because they are not error
# residue — they are work Steam scheduled for itself, and zeroing them cancels it:
#
#   ScheduledAutoUpdate          a future appointment. Measured on a real library:
#                                Halo: Campaign Evolved (2806050) carried
#                                "1788144179" with StateFlags=6.
#   FullValidateAfterNextUpdate  an instruction Steam left itself. Measured:
#                                Steamworks Common Redistributables (228980)
#                                carried "1".
#
# In SFF the list was safe because it applied to a manifest SFF had just written
# for a game DepotDownloaderMod had just downloaded — every counter was zero by
# construction and there was nothing of Steam's to overwrite. Applied to a live
# Steam manifest it is not the same operation.
_ACF_ERROR_FIELDS = (
    ("UpdateResult", "0"),
    ("BytesToDownload", "0"),
    ("BytesDownloaded", "0"),
    ("BytesToStage", "0"),
    ("BytesStaged", "0"),
    ("StagingSize", "0"),
)


def _vdf_dump_acf(path, data):
    """Minimal VDF text writer for .acf — no external dep. The format is
    "<key>"\t\t"<value>" with nested {} blocks. Steam ignores leading/trailing
    whitespace, so we just emit something it accepts."""
    def emit(node, indent):
        out = []
        pad = "\t" * indent
        for k, v in node.items():
            if isinstance(v, dict):
                out.append(f'{pad}"{k}"\n{pad}{{\n')
                out.append(emit(v, indent + 1))
                out.append(f'{pad}}}\n')
            else:
                out.append(f'{pad}"{k}"\t\t"{v}"\n')
        return "".join(out)
    with path.open("w", encoding="utf-8") as f:
        f.write(emit(data, 0))


_VDF_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\{)|(\})', re.DOTALL)


def _vdf_load_acf(path):
    """Minimal VDF text reader for .acf. Returns nested dict."""
    text = path.read_text(encoding="utf-8", errors="ignore")
    tokens = []
    for m in _VDF_TOKEN.finditer(text):
        s, lb, rb = m.group(1), m.group(2), m.group(3)
        if s is not None: tokens.append(("S", s.replace('\\"', '"')))
        elif lb:           tokens.append(("L", None))
        elif rb:           tokens.append(("R", None))
    i = 0
    def parse_block():
        nonlocal i
        node = {}
        while i < len(tokens):
            tt, tv = tokens[i]
            if tt == "R":
                i += 1
                return node
            if tt != "S":
                i += 1; continue
            key = tv; i += 1
            if i >= len(tokens): break
            tt2, tv2 = tokens[i]
            if tt2 == "L":
                i += 1
                node[key] = parse_block()
            else:
                node[key] = tv2
                i += 1
        return node
    # top-level: { "AppState" { ... } }
    return parse_block()


def _all_library_paths(steam_root):
    """Every Steam library folder, `steam_root` first.

    Games do not all live under the Steam install: a second drive (SD card,
    external partition) is a library of its own, with its own steamapps/ and its
    own appmanifest_<appid>.acf. `steam_root` is the INSTALL root — config.vdf,
    stplug-in, depotcache all hang off it and there is exactly one — so it must
    not be repurposed as "the library". This is the separate list.

    Steam keeps two copies of libraryfolders.vdf in sync and LOADS the one under
    steamapps/ (its own content_log says so). We read both and union them: a
    stale or missing copy then cannot HIDE a library from us. The reverse — a
    dead entry surviving in one copy — is harmless, since all we do with a path
    is look for a manifest under it. Order matters: the root is checked first,
    which is where a single-library user's manifest lives."""
    roots, seen = [], set()

    def add(path):
        try:
            key = os.path.realpath(str(path))
        except Exception:
            return
        if key not in seen:
            seen.add(key)
            roots.append(Path(path))

    add(steam_root)
    for vdf in (Path(steam_root) / "steamapps" / "libraryfolders.vdf",
                Path(steam_root) / "config" / "libraryfolders.vdf"):
        if not vdf.exists():
            continue
        try:
            folders = _vdf_load_acf(vdf).get("libraryfolders", {})
        except Exception:
            continue
        if not isinstance(folders, dict):
            continue
        for entry in folders.values():
            if isinstance(entry, dict) and entry.get("path"):
                add(entry["path"])
    return roots


def _find_acf(steam_root, app_id):
    """The appmanifest for `app_id`, in whichever library holds it, or None."""
    for lib in _all_library_paths(steam_root):
        acf = lib / "steamapps" / f"appmanifest_{app_id}.acf"
        if acf.exists():
            return acf
    return None


def patch_acf_error_state(steam_root, app_id, manifest_gids=None, name_override=None):
    """Step 6 of the install flow (replicates sff/lua/writer.py:_patch_acf_error_state).

    Clears stale error residue from an appmanifest Steam already wrote, so a
    failure from a previous attempt doesn't surface as 'NO INTERNET CONNECTION'
    on the next Install. Never creates a manifest: if there is no .acf, Steam
    writes its own on Install and we stay out of the way.

    Returns "patched", "clean", "none (...)" or "error".

    Looks in EVERY library, not just the install root: a game on an SD card or a
    second partition has its manifest there, and only there. Missing that meant
    the one job this function has silently not happening for those games.

    manifest_gids / name_override: kept for call compatibility, unused. They fed
    the stub this function used to seed; that behaviour is gone (see the tail of
    the function, and LumaDeck/docs/dev-multi-library.md)."""
    acf_path = _find_acf(steam_root, app_id)
    if acf_path is not None:
        try:
            data = _vdf_load_acf(acf_path)
            app_state = data.get("AppState", {})
            patched = False
            # Only correct keys that are PRESENT and wrong. A missing key is not
            # an error to clean: adding it would rewrite Steam's manifest on
            # every add of an already-installed game (measured: 12 of 13 real
            # manifests across two machines carry no FullValidateAfterNextUpdate,
            # so the old `.get(k) != clean` test never returned "clean").
            for k, clean in _ACF_ERROR_FIELDS:
                if k in app_state and app_state[k] != clean:
                    app_state[k] = clean
                    patched = True
            try:
                flags = int(app_state.get("StateFlags", "0"))
                if flags & 16:
                    app_state["StateFlags"] = str(flags & ~16)
                    patched = True
            except (ValueError, TypeError):
                pass
            if patched:
                # Back up only when we are actually going to write. Doing it
                # unconditionally left an .acf.bak per game on every add.
                shutil.copy2(acf_path, acf_path.with_suffix(".acf.bak"))
                _vdf_dump_acf(acf_path, data)
                return "patched"
            return "clean"
        except Exception as e:
            print(f"  [WARN] No pude patchar {acf_path}: {e}")
            return "error"

    # No .acf: nothing to do. Steam writes its own manifest when the user clicks
    # Install, in whichever library they pick, with the canonical installdir from
    # appinfo. We used to seed a stub here; it was measured to buy nothing (the
    # button reads "Install" either way) and to cost a real bug: a game installed
    # to a second library ends up with our orphan in the default one, and after
    # the next Steam restart Steam honours the orphan and reports the game as not
    # installed — issue #41. See LumaDeck/docs/dev-multi-library.md.
    return "none (no .acf yet — Steam writes it on Install)"


# ── Ecosystem interop (stplug-in / ACCELA) ────────────────────────────────────
#
# These helpers write breadcrumbs that other tools in the ecosystem look for to
# decide a game is "managed". Functionally redundant with our keys.txt flow —
# SLSsteam and lumalinux don't need any of this — but they make the
# game visible to:
#   - SteaMidra-style tools that scan <steam>/config/stplug-in/*.lua
#   - DeckTools / LumaDeck (their has_lua_for_app check)
#   - ACCELA / ASSella when used in Desktop Mode (markers inside the game
#     folder + ~/.local/share/ACCELA/depots/<appid>.depot for update detection)
#
# Each step is best-effort: it logs what it did and never aborts the run.


# Comments out every `setManifestid(...)` line in a .lua (prefixing `--`).
# In NO-PIN mode we write the stplug-in copy unpinned for parity with the
# Windows/LumaCore method (where commenting setManifestid is what enables
# auto-update). On Linux lumalinux reads keys.txt, not this .lua, so this is
# interop/cosmetic — but we keep the stplug-in copy consistent with keys.txt.
_SETMANIFESTID_LINE_RE = re.compile(r"^([ \t]*)setManifestid", re.MULTILINE)


def comment_setmanifestid(lua_contents):
    return _SETMANIFESTID_LINE_RE.sub(r"\1--setManifestid", lua_contents)


def install_lua_to_stplugin(steam_root, app_id, lua_contents, pin):
    """Copy the parsed .lua into <steam>/config/stplug-in/<appid>.lua. SLSsteam
    accepts both AdditionalApps (config.yaml) and the legacy .lua path; we
    already write AdditionalApps, so this is interop only — it lights up
    has_lua_for_app() in DeckTools / LumaDeck and makes SteaMidra-style
    manifest updaters find the game.

    When pin=False (default) the `setManifestid` lines are commented out in the
    written copy, mirroring the Windows/LumaCore auto-update convention.

    If the destination already exists, save a .lua.bak next to it before
    overwriting (the existing .lua may be an older version of the same game,
    or a hand-edited variant we shouldn't silently lose)."""
    stplug = steam_root / "config" / "stplug-in"
    stplug.mkdir(parents=True, exist_ok=True)
    dest = stplug / f"{app_id}.lua"
    if dest.exists():
        shutil.copy2(dest, dest.with_suffix(".lua.bak"))
    out = lua_contents if pin else comment_setmanifestid(lua_contents)
    with open(dest, "w", encoding="utf-8") as f:
        f.write(out)
    return dest


def _read_installdir_from_acf(steam_root, app_id):
    """Read the REAL `installdir` Steam will use from appmanifest_<appid>.acf.

    This is the single source of truth: Steam writes/normalises this field, and
    it's the exact folder name under steamapps/common/ where the game lives.
    Returns None if the .acf doesn't exist yet or has no installdir — which is
    the normal case BEFORE the user has installed the game, since we no longer
    seed a manifest of our own."""
    acf_path = steam_root / "steamapps" / f"appmanifest_{app_id}.acf"
    if not acf_path.exists():
        return None
    try:
        data = _vdf_load_acf(acf_path)
    except Exception:
        return None
    installdir = data.get("AppState", {}).get("installdir")
    return installdir or None


def _game_dir_has_content(game_dir):
    """Mirror ASSella game_manager._has_game_content: True if the folder has at
    least one entry that is NOT an ACCELA marker / OS-metadata file / dotfile.

    ACCELA only lists games whose folder has real content, so this tells us
    whether Steam has actually finished downloading the game yet (pre-download
    the folder is empty except for our own marker, which is ignored)."""
    ignore = {".accela", ".depotdownloader", "desktop.ini", "thumbs.db"}
    try:
        for entry in os.scandir(game_dir):
            name = entry.name
            if name.lower() in ignore or name.startswith("."):
                continue
            return True
    except OSError:
        return False
    return False


def mark_game_for_accela(steam_root, app_id, installdir):
    """Create <steam>/steamapps/common/<installdir>/.DepotDownloader/ so
    ACCELA / ASSella sees the game as one of theirs. ASSella's library
    scanner looks for either '.ACCELA' or '.DepotDownloader' inside the
    install dir to flag a game as 'is_accela_install' (see ASSella
    game_manager.py:_get_accela_marker_path); we use .DepotDownloader since
    that's what the modern ASSella creates itself.

    Also drops the wrapper metadata json ASSella uses for selected-DLC
    tracking (empty list — we don't preselect DLCs).

    'installdir' comes from Steam's own manifest (we no longer write one), so
    it is by definition the directory Steam uses. Returns the marker dir for
    logging."""
    game_dir = steam_root / "steamapps" / "common" / installdir
    marker_dir = game_dir / ".DepotDownloader"
    marker_dir.mkdir(parents=True, exist_ok=True)
    metadata_file = marker_dir / "accela_wrapper_metadata.json"
    if not metadata_file.exists():
        with open(metadata_file, "w", encoding="ascii") as f:
            json.dump({"selected_dlcs": []}, f, indent=2)
    return marker_dir


def write_accela_depot_marker(app_id, main_depot_id, manifest_id, app_token=""):
    """Write ~/.local/share/ACCELA/depots/<appid>.depot — the file ASSella's
    ManifestCheckTask reads to compare the saved manifest_id with the current
    public manifest (Steam Web API) and surface 'update_available' badges
    in its library UI.

    Format (ACCELA task_manager._save_main_depot_info):
        <main_depot_id>: <manifest_id>[: <app_token>]
    The token field is optional and only needed for apps whose PICS appinfo
    Valve gates behind a token. We pass it through if --token was supplied
    for this appid, otherwise empty (still a valid 3-field line).

    Creates ~/.local/share/ACCELA/depots/ if it doesn't exist yet, so the
    file is ready the moment the user installs ACCELA / ASSella on this
    deck. If ACCELA dir gets cleaned later by other means, the next run of
    this script re-creates it."""
    base_env = os.environ.get("XDG_DATA_HOME")
    base_root = Path(base_env) if base_env else (Path.home() / ".local" / "share")
    accela_base = base_root / "ACCELA"
    depots_dir = accela_base / "depots"
    depots_dir.mkdir(parents=True, exist_ok=True)
    depot_file = depots_dir / f"{app_id}.depot"
    if depot_file.exists():
        shutil.copy2(depot_file, depot_file.with_suffix(".depot.bak"))
    # Match ACCELA's exact on-disk format (task_manager._save_main_depot_info):
    #   "<depot>: <manifest>"            when no token
    #   "<depot>: <manifest>: <token>"   when token-gated
    # ASSella's parser strips each field so spacing is cosmetic, but we mirror it
    # byte-for-byte so a real ACCELA install and ours look identical on disk.
    if app_token:
        line = f"{main_depot_id}: {manifest_id}: {app_token}"
    else:
        line = f"{main_depot_id}: {manifest_id}"
    with open(depot_file, "w", encoding="utf-8") as f:
        f.write(line + "\n")
    return depot_file


def _pick_main_depot_for_accela(depot_keys, manifests, app_id):
    """Pick a 'main depot' to write into the .depot tracker. ASSella checks
    just one depot per app to decide if an update is available — typically
    the game's primary content depot.

    Heuristic: first depot_id with parent_app == app_id (= a depot whose key
    we wrote with parent=app_id), gid != 0 (= the .lua actually pinned a
    manifest for it), and that isn't the appid itself (= not the dummy
    line). Returns (depot_id, manifest_id) or None if no candidate fits."""
    for depot_id in sorted(depot_keys):
        if depot_id == app_id:
            continue
        gid = manifests.get(depot_id, 0)
        if gid:
            return (depot_id, gid)
    return None


def _get_app_token_for(args, app_id):
    """Extract the app token for `app_id` from --token arguments, if the
    user passed one for this app. Empty string if not — write_accela_depot_marker
    handles that cleanly."""
    if not args.token:
        return ""
    for entry in args.token:
        try:
            tid, thex = parse_token_arg(entry)
            if int(tid) == app_id:
                return str(thex).strip()
        except Exception:
            continue
    return ""


def run_accela_mark(args):
    """Post-install registration mode (no install). (Re)creates the ACCELA /
    ASSella markers for a game Steam has ALREADY downloaded, so the standalone
    ACCELA app lists it as one of its own.

    Why a separate mode: in the LumaDeck flow steamidra_lite runs BEFORE the
    download (it only sets up the Install button); Steam downloads later, in
    Game Mode. At that earlier point the game folder is empty, so the in-game
    `.DepotDownloader` marker can't take effect (ACCELA only lists folders with
    real content). This mode is meant to run AFTER the install completes — e.g.
    LumaDeck invoking `steamidra_lite --accela-mark <appid>` once Steam is done.

    It reads the real installdir from appmanifest_<appid>.acf (single source of
    truth) and recovers depot/manifest info by re-parsing the stplug-in .lua we
    wrote during the install. Idempotent — safe to run repeatedly."""
    app_id = int(args.accela_mark)
    print(f"== Modo --accela-mark: registrando appid {app_id} para ACCELA/ASSella ==")

    installdir = _read_installdir_from_acf(args.steam_root, app_id)
    if not installdir:
        sys.exit(
            f"ERROR: no encuentro 'installdir' en "
            f"{args.steam_root}/steamapps/appmanifest_{app_id}.acf. "
            f"Sin .acf no sé en qué carpeta vive el juego — ¿se configuró/instaló "
            f"vía Steam primero?")

    game_dir = args.steam_root / "steamapps" / "common" / installdir
    has_content = _game_dir_has_content(game_dir)
    if not has_content:
        print(f"  [!] {game_dir} aún sin contenido — ACCELA no lo listará hasta que "
              f"Steam termine de descargar. Creo el marker igualmente (idempotente).")

    try:
        marker_dir = mark_game_for_accela(args.steam_root, app_id, installdir)
        print(f"  [+] {marker_dir} (in-game marker, installdir='{installdir}', "
              f"contenido={'sí' if has_content else 'todavía no'})")
    except Exception as exc:
        print(f"  [!] no pude crear el in-game marker: {exc}")

    # .depot tracker: recover depot+manifest by re-parsing the stplug-in .lua
    # we wrote during install. That file is the record of what this game is.
    lua_path = args.steam_root / "config" / "stplug-in" / f"{app_id}.lua"
    if not lua_path.exists():
        print(f"  [-] no existe {lua_path} → sin depot/manifest, .depot tracker no escrito")
        print("== Hecho (accela-mark) ==")
        return

    lua_text = lua_path.read_text(encoding="utf-8", errors="ignore")
    parsed = parse_lua_contents(lua_text, lua_path)
    if parsed is None:
        print(f"  [-] no pude parsear {lua_path} → .depot tracker no escrito")
        print("== Hecho (accela-mark) ==")
        return

    depot_keys = {int(p.depot_id): p.decryption_key for p in parsed.depots if p.decryption_key}
    manifests = {int(d): g for d, g in parsed.manifest_overrides.items()}
    main_depot = _pick_main_depot_for_accela(depot_keys, manifests, app_id)
    if main_depot:
        depot_id, manifest_id = main_depot
        app_token = _get_app_token_for(args, app_id)
        try:
            depot_file = write_accela_depot_marker(app_id, depot_id, manifest_id, app_token)
            print(f"  [+] {depot_file} (update tracking: depot {depot_id} → manifest {manifest_id})")
        except Exception as exc:
            print(f"  [!] no pude escribir el .depot de ACCELA: {exc}")
    else:
        print(f"  [-] el .lua no fija manifest (setManifestid) para ningún depot → "
              f".depot tracker no escrito")
    print("== Hecho (accela-mark) ==")


# ── Modos sin-zip: pin/unpin de un juego YA desplegado (para LumaDeck) ─────────
#
# Cortan al principio de main() (como --accela-mark): NO abren zip, NO extraen,
# NO tocan config.vdf. Editan la sección ManifestIds del config.yaml de SLSsteam
# (y leen el .acf/keys.txt para saber los depots en --pin-installed).

_KEYS_HEADER = [
    "# lumalinux keys.txt — managed by steamidra_lite.py",
    "# EXTENDED: depot_id;parent_app_id;manifest_gid;manifest_size;hex_key   (content depots — lumalinux inyecta en pDepotInfo)",
    "# LEGACY:   depot_id;hex_key                                            (AppID with key — lumalinux solo sirve la key, no inyecta)",
    "# NO-KEY:   depot_id;                                                   (AppID sin key en el .lua — lumalinux inyecta el id pero el hook hace passthrough; espejo del DepotKeySet[id]=\"\" de LumaCore)",
    "# Todos los depots (content y shared) van aquí: el hook DepotKey v1.0 hookea la KeyValues accessor interna y sirve cualquiera sin corromper (como LumaCore).",
]


def _load_keys_file(keys_path):
    """Parsea keys.txt → {depot_id: entry}. entry es una de:
       ("ext", parent, gid, size, key) | ("leg", key) | ("leg_no_key",).
    Mismo formato que el parser inline de write_lumalinux_keys."""
    existing = {}
    if keys_path.exists():
        for line in keys_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(";")
            try:
                if len(parts) == 5:
                    existing[int(parts[0])] = ("ext", int(parts[1]), int(parts[2]),
                                               int(parts[3]), parts[4])
                elif len(parts) == 2:
                    did = int(parts[0])
                    existing[did] = ("leg_no_key",) if parts[1] == "" else ("leg", parts[1])
            except ValueError:
                continue
    return existing


def _write_keys_file(keys_path, existing):
    """Escribe keys.txt desde el dict de _load_keys_file (hace .bak primero)."""
    keys_path.parent.mkdir(parents=True, exist_ok=True)
    if keys_path.exists():
        shutil.copy2(keys_path, keys_path.with_suffix(".txt.bak"))
    lines = list(_KEYS_HEADER)
    for did in sorted(existing):
        e = existing[did]
        if e[0] == "ext":
            _, parent, gid, size, key = e
            lines.append(f"{did};{parent};{gid};{size};{key}")
        elif e[0] == "leg_no_key":
            lines.append(f"{did};")
        else:
            _, key = e
            lines.append(f"{did};{key}")
    keys_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_installed_depots(steam_root, app_id):
    """Lee appmanifest_<appid>.acf → InstalledDepots → {depot_id: (gid, size)}.
    {} si no hay .acf o no hay InstalledDepots (juego no instalado)."""
    acf_path = steam_root / "steamapps" / f"appmanifest_{app_id}.acf"
    if not acf_path.exists():
        return {}
    try:
        data = _vdf_load_acf(acf_path)
    except Exception:
        return {}
    installed = data.get("AppState", {}).get("InstalledDepots", {})
    out = {}
    if isinstance(installed, dict):
        for did, info in installed.items():
            if not isinstance(info, dict):
                continue
            try:
                gid = int(info.get("manifest", 0))
                did_i = int(did)
            except (ValueError, TypeError):
                continue
            try:
                size = int(info.get("size", 0))
            except (ValueError, TypeError):
                size = 0
            out[did_i] = (gid, size)
    return out


_COMMENTED_SETMANIFESTID_RE = re.compile(r"^([ \t]*)--setManifestid", re.MULTILINE)


def uncomment_setmanifestid(lua_contents):
    return _COMMENTED_SETMANIFESTID_RE.sub(r"\1setManifestid", lua_contents)


def _toggle_stplugin_pin(steam_root, app_id, pin):
    """Comenta (no-pin) o descomenta (pin) los setManifestid del .lua de stplug-in.
    Interop/cosmético en Linux (lumalinux lee keys.txt), pero mantiene el stplug-in
    coherente con el estado de pin. Best-effort: si no existe el .lua, no-op."""
    lua_path = steam_root / "config" / "stplug-in" / f"{app_id}.lua"
    if not lua_path.exists():
        return None
    txt = lua_path.read_text(encoding="utf-8", errors="ignore")
    new = uncomment_setmanifestid(txt) if pin else comment_setmanifestid(txt)
    if new != txt:
        shutil.copy2(lua_path, lua_path.with_suffix(".lua.bak"))
        lua_path.write_text(new, encoding="utf-8")
    return lua_path


def purge_depot_manifests(depotcache, config_depotcache, depot_ids):
    """Borra TODOS los manifests de los depots dados (ambos depotcache). Para
    --unpin: así Steam re-pide el manifest ACTUAL a Valve en el próximo arranque."""
    removed = []
    for did in depot_ids:
        for base in (depotcache, config_depotcache):
            if base is None or not base.exists():
                continue
            for f in base.glob(f"{did}_*.manifest"):
                try:
                    f.unlink()
                    removed.append(f.name)
                except OSError:
                    pass
    return removed


# ── SLSsteam ManifestIds (el mecanismo de pin desde 20260714) ─────────────────
#
# lumalinux ya NO hookea BuildDepotDependency (SLSsteam lo hookea primero y gana
# el orden de carga). El pin de versión se re-homea a `ManifestIds` en el
# config.yaml de SLSsteam: un mapa depot_id -> manifest_gid que SLSsteam aplica
# en SU buildDepotDependency (depot->manifestId = override). SLSsteam vigila el
# config con inotify y lo recarga en caliente, así que no hace falta reiniciar
# Steam; el freeze aplica en la siguiente evaluación del plan de descarga.

def _app_content_depots(keys_path, app_id):
    """Content depot ids de app_id según keys.txt (entradas EXTENDED, parent==app_id)."""
    existing = _load_keys_file(keys_path)
    return [did for did, e in existing.items() if e[0] == "ext" and e[1] == app_id]


def _read_manifest_ids(config_path):
    """Parsea la sección ManifestIds del config.yaml de SLSsteam → {depot_id: gid}
    (ints). Editor de líneas a mano (SteamOS python no trae pyyaml). {} si el
    fichero o la sección no existen."""
    if not config_path.exists():
        return {}
    out = {}
    in_section = False
    for line in config_path.read_text(encoding="utf-8").splitlines():
        if not in_section:
            if line.strip().startswith("ManifestIds:"):
                in_section = True
            continue
        # dentro de la sección: hijos indentados "depot: gid"; la primera línea
        # NO indentada (o en blanco) cierra la sección.
        if line[:1] not in (" ", "\t"):
            break
        m = re.match(r"^\s*(\d+)\s*:\s*(\d+)", line)
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def _write_manifest_ids(config_path, manifest_ids):
    """Reescribe la sección ManifestIds del config.yaml con `manifest_ids`
    ({depot: gid}). Crea la sección si falta (o el fichero, mínimo). Hace .bak.
    Editor de líneas a mano; NO toca el resto del config."""
    body = ["ManifestIds:"] + [f"  {did}: {gid}" for did, gid in sorted(manifest_ids.items())]
    if not config_path.exists():
        config_path.parent.mkdir(parents=True, exist_ok=True)
        config_path.write_text("\n".join(body) + "\n", encoding="utf-8")
        return
    shutil.copy2(config_path, config_path.with_suffix(".yaml.bak"))
    lines = config_path.read_text(encoding="utf-8").splitlines()
    out, i, n, replaced = [], 0, len(lines), False
    while i < n:
        if lines[i].strip().startswith("ManifestIds:"):
            i += 1  # saltar cabecera vieja + sus hijos indentados
            while i < n and lines[i][:1] in (" ", "\t"):
                i += 1
            out.extend(body)
            replaced = True
            continue
        out.append(lines[i])
        i += 1
    if not replaced:
        if out and out[-1].strip() != "":
            out.append("")
        out.extend(body)
    config_path.write_text("\n".join(out) + "\n", encoding="utf-8")


def run_pin_installed(args):
    """--pin-installed APPID: congela el juego en la versión INSTALADA escribiendo
    ManifestIds (depot->gid) en el config.yaml de SLSsteam. Lee los gids de
    appmanifest_<APPID>.acf:InstalledDepots, acotado a los content depots de este
    app (keys.txt). No necesita zip; SLSsteam recarga el config solo."""
    app_id = int(args.pin_installed)
    print(f"== Pin a la versión instalada (SLSsteam ManifestIds): appid {app_id} ==")
    installed = read_installed_depots(args.steam_root, app_id)
    if not installed:
        sys.exit(f"ERROR: sin InstalledDepots en appmanifest_{app_id}.acf "
                 f"(¿el juego está instalado?). Sin versión instalada no hay nada que pinear.")
    content = _app_content_depots(args.luma_keys, app_id)
    # Never pin the shared redistributables (228980) — they must always auto-update.
    # They can land in keys.txt as content depots when the .lua lacks a
    # `-- SHARED DEPOTS` header, so exclude them here too (mirrors the zip-based pin).
    to_pin = {did: installed[did][0] for did in content
              if did in installed and installed[did][0]
              and did not in _KNOWN_REDIST_DEPOTS}
    if not to_pin:
        sys.exit(f"ERROR: ningún content depot de {app_id} (keys.txt) coincide con "
                 f"InstalledDepots {sorted(installed)}. ¿Se desplegó con steamidra_lite?")
    manifest_ids = _read_manifest_ids(args.sls_config)
    manifest_ids.update(to_pin)
    _write_manifest_ids(args.sls_config, manifest_ids)
    for did, gid in sorted(to_pin.items()):
        print(f"  [+] depot {did} → gid {gid} (ManifestIds)")
    print("== Pinneado. SLSsteam recarga el config solo; el juego se queda en esta versión. ==")


def run_unpin(args):
    """--unpin APPID: vuelve el juego a auto-update quitando sus content depots de
    ManifestIds en el config.yaml de SLSsteam. Steam vuelve a planear el manifest
    más nuevo (GMRC lo pide) en el próximo update-check."""
    app_id = int(args.unpin)
    print(f"== Unpin (auto-update): appid {app_id} ==")
    content = set(_app_content_depots(args.luma_keys, app_id))
    if not content:
        sys.exit(f"ERROR: ningún content depot de {app_id} en keys.txt. ¿appid correcto?")
    manifest_ids = _read_manifest_ids(args.sls_config)
    removed = [d for d in content if d in manifest_ids]
    for d in removed:
        del manifest_ids[d]
    _write_manifest_ids(args.sls_config, manifest_ids)
    if removed:
        print(f"  [+] {len(removed)} depot(s) fuera de ManifestIds (auto-update): {sorted(removed)}")
    else:
        print("  [i] no estaba pinneado; nada que cambiar.")
    print("== Despinneado. SLSsteam recarga el config solo; volverá a seguir a Valve. ==")


def run_pin_status(args):
    """--pin-status APPID: imprime JSON {appid, pinned, depots}. pinned=True si
    algún content depot de este app está en ManifestIds del config de SLSsteam."""
    app_id = int(args.pin_status)
    content = _app_content_depots(args.luma_keys, app_id)
    manifest_ids = _read_manifest_ids(args.sls_config)
    depots = {did: manifest_ids[did] for did in content if did in manifest_ids}
    pinned = bool(depots)
    print(json.dumps({"appid": app_id, "pinned": pinned,
                      "depots": {str(d): str(g) for d, g in sorted(depots.items())}}))


def main():
    ap = argparse.ArgumentParser(
        description="Replica el deploy de SteaMidra (Linux) sin instalarlo.",
        epilog="Por defecto acepta un .zip estilo Hubcap; con --manifests-dir acepta .lua suelto."
    )
    ap.add_argument("input", type=Path, nargs="?", default=None,
                    help="ruta al archivo de entrada: .zip Hubcap (default) o .lua si pasas "
                         "--manifests-dir. Se omite si usas --accela-mark.")
    ap.add_argument("--manifests-dir", type=Path, default=None,
                    help="(modo legacy) directorio con .manifest si el input es un .lua suelto")
    ap.add_argument("--steam-root", type=Path, default=Path.home()/".local/share/Steam",
                    help="raíz de Steam (default: ~/.local/share/Steam)")
    ap.add_argument("--sls-config", type=Path, default=Path.home()/".config/SLSsteam/config.yaml",
                    help="config.yaml de SLSsteam")
    ap.add_argument("--luma-keys", type=Path, default=Path.home()/".config/lumalinux/keys.txt",
                    help="keys.txt de lumalinux")
    ap.add_argument("--name", type=str, default=None, metavar="GAME_NAME",
                    help="IGNORADO. Alimentaba el `installdir` del .acf stub, que ya no "
                         "escribimos (lo escribe Steam al instalar). Se sigue aceptando "
                         "porque LumaDeck cachea por sesión si este script soporta la flag: "
                         "quitarla rompería un add si se actualiza lumalinux sin reiniciar "
                         "el plugin. Eliminar cuando nadie la mande.")
    ap.add_argument("--no-vdf", action="store_true",
                    help="NO escribir las DecryptionKeys en Steam config.vdf. "
                         "Por defecto SÍ se escriben (SteaMidra Linux también lo hace; "
                         "el hook DepotKey de lumalinux es backup runtime).")
    ap.add_argument("--pin", action="store_true",
                    help="PINEAR el juego al manifest del zip (gid del setManifestid). "
                         "Por DEFECTO NO se pinea (gid=0): el juego sigue a Valve y se "
                         "auto-actualiza vía el cliente nativo. Usa --pin para congelarlo "
                         "en la versión del zip (p.ej. juegos modeados a una versión concreta). "
                         "Ver docs/method.md §6.")
    ap.add_argument("--token", action="append", default=[], metavar="APPID:HEX",
                    help="añadir un AppToken al config.yaml de SLSsteam. Puedes pasarlo varias veces.")
    ap.add_argument("--accela-mark", type=int, default=None, metavar="APPID",
                    help="modo post-instalación: NO instala nada. Solo (re)crea los markers de "
                         "ACCELA/ASSella para un juego YA descargado por Steam, leyendo el "
                         "installdir real de appmanifest_<APPID>.acf y los depots del .lua de "
                         "stplug-in. Idempotente — pensado para que LumaDeck lo invoque cuando "
                         "Steam termine de instalar el juego.")
    ap.add_argument("--pin-installed", type=int, default=None, metavar="APPID",
                    help="(modo sin-zip) Congela el juego en la versión INSTALADA: lee los gids "
                         "de appmanifest_<APPID>.acf:InstalledDepots y los escribe en ManifestIds "
                         "del config.yaml de SLSsteam. Para el toggle 'Pin' de LumaDeck.")
    ap.add_argument("--unpin", type=int, default=None, metavar="APPID",
                    help="(modo sin-zip) Vuelve el juego a auto-update: quita sus content depots "
                         "de ManifestIds en el config.yaml de SLSsteam.")
    ap.add_argument("--pin-status", type=int, default=None, metavar="APPID",
                    help="(modo sin-zip) Imprime JSON {appid,pinned,depots} (para LumaDeck).")
    args = ap.parse_args()

    if args.accela_mark is not None:
        run_accela_mark(args)
        return
    if args.pin_installed is not None:
        run_pin_installed(args)
        return
    if args.unpin is not None:
        run_unpin(args)
        return
    if args.pin_status is not None:
        run_pin_status(args)
        return

    if args.input is None:
        sys.exit("ERROR: falta el archivo de entrada (.zip/.lua), o usa --accela-mark APPID")
    if not args.input.exists():
        sys.exit(f"ERROR: no existe: {args.input}")

    depotcache = args.steam_root / "depotcache"
    config_depotcache = args.steam_root / "config" / "depotcache"

    # ── Cargar lua (desde zip o suelto) y copiar manifests ────────────────
    manifests_from_names = {}
    manifest_sizes = {}
    if args.input.suffix.lower() == ".zip":
        print(f"== Extrayendo ZIP {args.input} ==")
        lua_text, copied, manifests_from_names, manifest_sizes = extract_zip(
            args.input, depotcache, config_depotcache)
        print(f"  [+] .lua encontrado y leído")
        for n in copied: print(f"  [+] {n} -> depotcache/")
        if not copied:
            print("  [!] El ZIP no contiene ningún .manifest. ¿Lo correcto?")
    else:
        if args.manifests_dir is None:
            sys.exit("ERROR: con un .lua suelto necesitas --manifests-dir <dir>")
        if not args.manifests_dir.exists():
            sys.exit(f"ERROR: no existe el dir: {args.manifests_dir}")
        lua_text = args.input.read_text(encoding="utf-8", errors="ignore")
        # Parseamos GIDs y sizes de los nombres+contenido de los .manifest del dir
        for f in args.manifests_dir.glob("*.manifest"):
            p = parse_manifest_gid_from_name(f.name)
            if p:
                manifests_from_names[p[0]] = p[1]
                size = parse_manifest_size(f)
                if size > 0:
                    manifest_sizes[p[0]] = size
    print()

    print(f"== Parseando .lua (parse_lua_contents verbatim de SteaMidra) ==")
    parsed = parse_lua_contents(lua_text, args.input)
    if parsed is None:
        sys.exit("ERROR: parse_lua_contents devolvió None (sin addappid o sin depot keys).")
    # Adapt LuaParsedInfo to the existing pipeline (script existente usa dicts).
    app_id = int(parsed.app_id)
    depot_keys = {int(p.depot_id): p.decryption_key for p in parsed.depots if p.decryption_key}
    dlcs_no_key = [int(p.depot_id) for p in parsed.depots if not p.decryption_key and int(p.depot_id) != app_id]
    manifests_from_lua = {int(d): g for d, g in parsed.manifest_overrides.items()}
    # Merge: los del .lua tienen prioridad, los de los filenames del zip rellenan huecos.
    # NOTA: SteaMidra verbatim NO captura el tercer arg (size) del setManifestid
    # — lo capturamos del .manifest binario en extract_zip via parse_manifest_size.
    manifests = dict(manifests_from_names)
    manifests.update(manifests_from_lua)
    print(f"  appid principal:    {app_id}")
    if dlcs_no_key: print(f"  DLCs sin depot:     {dlcs_no_key}  (no key, no manifest)")
    print(f"  depots con key:     {sorted(depot_keys.keys())}")
    print(f"  manifests GID:      {sorted(manifests.keys())}")
    print(f"  manifest sizes (cb_disk_original, del binario):")
    for d in sorted(manifest_sizes):
        print(f"    depot {d}: {manifest_sizes[d]:>14} bytes  ({manifest_sizes[d]/1024/1024:.1f} MB)")
    if not manifest_sizes:
        print(f"    [!] ninguno parseado — Steam crashea con size=0, revisa los .manifest")
    print()

    # En modo .lua suelto, copiar manifests ahora que sabemos los GID
    if args.input.suffix.lower() != ".zip":
        print(f"== Copiando manifests desde {args.manifests_dir} a {depotcache} (+ config/depotcache) ==")
        copied, missing = copy_manifests_from_dir(manifests, args.manifests_dir, depotcache, config_depotcache)
        for n in copied: print(f"  [+] {n}")
        for d, g in missing:
            print(f"  [!] Falta .manifest para depot {d} gid {g}")
        print()

    # ── SLSsteam config.yaml ──────────────────────────────────────────────
    # SteaMidra (sff/app_injector/sls.py:add_ids) mete SOLO el AppID principal,
    # no los depots. Replicamos ese comportamiento — meter depots como AppIDs
    # confunde a Steam (los ve como "este depot es un app", contradicción).
    print(f"== Actualizando {args.sls_config} ==")
    tokens_dict = dict(parse_token_arg(t) for t in args.token) if args.token else None
    n_apps, n_tokens = update_sls_yaml(args.sls_config, [app_id], tokens_dict)
    print(f"  [+] {n_apps} appids nuevos en AdditionalApps (solo {app_id} — replicamos SteaMidra)")
    if tokens_dict:
        print(f"  [+] {n_tokens} tokens nuevos en AppTokens")
    print()

    # ── lumalinux keys.txt ────────────────────────────────────────────────
    # TODOS los depots (content y shared) van a keys.txt. El hook DepotKey v1.0
    # hookea la KeyValues accessor interna (como LumaCore), así que servir la key
    # de cualquier depot solo responde la query y no corrompe — sin lista
    # estática. shared_depots se reporta solo a título informativo.
    shared_depots = parse_shared_depots(lua_text)
    if shared_depots:
        print(f"  [i] shared depots: {sorted(shared_depots)} (se sirven igual; el hook v1.0 lo maneja)")

    mode = "PIN (congela la versión del zip)" if args.pin else "NO-PIN (auto-update; sigue a Valve)"
    print(f"== Escribiendo {args.luma_keys}  [modo: {mode}] ==")

    # NO-PIN: borrar manifests viejos de los content depots de este juego para
    # que Steam no reuse un manifest pinneado previo y pueda seguir el de Valve.
    if not args.pin:
        content_keep = {d: manifests[d] for d in depot_keys
                        if d != app_id and d not in shared_depots and manifests.get(d)}
        removed = prune_stale_manifests(depotcache, config_depotcache, content_keep)
        if removed:
            print(f"  [+] depotcache: borrados {len(removed)} manifest(s) viejos de este juego "
                  f"(se conserva el del zip)")

    n_new, n_upd = write_lumalinux_keys(
        args.luma_keys, depot_keys, manifests, manifest_sizes, app_id, args.pin)
    print(f"  [+] {n_new} keys nuevas, {n_upd} actualizadas")
    if args.pin:
        patchable = [d for d in depot_keys if d != app_id and manifests.get(d, 0) != 0]
        print(f"  [*] depots con gid≠0 que BuildDep fijará: {patchable}")
        if not patchable:
            print(f"  [!] AVISO: ningún content depot con gid≠0. BuildDep no fijará nada.")
    else:
        print(f"  [*] gid=0 en todos los content depots → BuildDep passthrough → "
              f"Steam sigue el manifest actual de Valve (auto-update)")
    print()

    # ── SLSsteam ManifestIds — el pin de versión VIVO ─────────────────────
    # BuildDep (keys.txt, arriba) está desactivado desde SLSsteam 20260714, que
    # se quedó con BuildDepotDependency y lee la versión de la sección
    # ManifestIds del config.yaml. Así que re-homeamos el pin aquí:
    #   --pin  → escribir los gids del build instalado en ManifestIds (fija esa
    #            versión por el mecanismo que SLSsteam lee de verdad).
    #   no-pin → quitar los content depots de este app de ManifestIds, para que
    #            un re-install sin pin no deje un pin viejo colgado (vuelve a
    #            seguir a Valve). Simétrico con --pin-installed / --unpin.
    # Siempre read-merge-write: NUNCA reescribir la sección con solo este app,
    # o borraríamos los pins de otros juegos.
    try:
        mids = _read_manifest_ids(args.sls_config)
        if args.pin:
            # Shared redistributables (228980) never get pinned — they must always
            # auto-update. Excluded here too (this ManifestIds write is the LIVE pin;
            # keys.txt/BuildDep above is disabled since SLSsteam 20260714).
            to_pin = {d: manifests[d] for d in depot_keys
                      if d != app_id and manifests.get(d, 0)
                      and d not in _KNOWN_REDIST_DEPOTS}
            if to_pin:
                mids.update(to_pin)
                _write_manifest_ids(args.sls_config, mids)
                print(f"  [+] ManifestIds (SLSsteam): pinneados {len(to_pin)} "
                      f"depot(s) → {sorted(to_pin.items())}")
            else:
                print("  [!] --pin pero ningún content depot con gid≠0 → nada que "
                      "pinear en ManifestIds.")
        else:
            content = set(_app_content_depots(args.luma_keys, app_id))
            removed = [d for d in content if d in mids]
            if removed:
                for d in removed:
                    del mids[d]
                _write_manifest_ids(args.sls_config, mids)
                print(f"  [+] ManifestIds (SLSsteam): quitados {len(removed)} "
                      f"depot(s) (auto-update): {sorted(removed)}")
    except Exception as exc:
        print(f"  [!] no pude actualizar ManifestIds de SLSsteam: {exc}")
    print()

    # ── config.vdf — siempre por defecto (--no-vdf para skipear) ──────────
    # SteaMidra Linux (sff/ui.py:process_lua_full) lo hace siempre. Steam consulta
    # config.vdf primero para depot keys; aunque lumalinux las sirve runtime via
    # el hook DepotKey, tenerlas también ahí cubre paths donde Steam no llama al
    # hook (p.ej. validación pre-descarga del manifest signature).
    if not args.no_vdf:
        print(f"== Añadiendo keys a {args.steam_root/'config/config.vdf'} ==")
        # Filter out the main AppID: Hubcap .luas sometimes include
        # `addappid(APP_ID, 1, "key")` (Formula Legends does), but Steam's
        # depots table is for *depot* keys only. Writing APP_ID there made
        # Steam crash on install. Confirmed by comparing FL (broken, APP_ID
        # was in depots) vs Mina (working, APP_ID was not).
        vdf_keys = {d: k for d, k in depot_keys.items() if d != app_id}
        n_vdf = update_config_vdf(args.steam_root/"config/config.vdf", vdf_keys)
        print(f"  [+] {n_vdf} keys nuevas en config.vdf  "
              f"(AppID {app_id} filtrado, no es un depot)")
        # NOTE: we deliberately DO NOT touch config.vdf's "DisableShaderCache".
        # It is the user's own Steam "Shader Pre-Caching" setting; the per-game
        # ShaderDepot hook (v0.14, RESEARCH §13.9) handles keyless games inside the
        # client without any global flag, so steamidra has no business writing it.
        # (Until v0.16.x we reverted a legacy 1->0 here, but that blindly stomped a
        # deliberate user-off — a real bug — so the revert was removed. A build
        # whose ShaderDepot hook is broken is kept off users by LumaDeck's update
        # gate, not by co-opting a global user setting.)
        print()

    # ── .acf error-state reset ────────────────────────────────────────────
    # Without this Steam often shows "NO INTERNET CONNECTION" on the first
    # install attempt — it's not a network issue, it's stale UpdateResult /
    # Bytes* fields in the .acf from a previous failure. Verbatim from
    # sff/lua/writer.py:113: "this is what causes 'NO INTERNET CONNECTION'".
    print(f"== Reseteando error-state del .acf (paso que evita 'no internet') ==")
    acf_result = patch_acf_error_state(args.steam_root, app_id)
    print(f"  [+] appmanifest_{app_id}.acf: {acf_result}")
    print()

    # ── Ecosystem interop (stplug-in .lua + ACCELA markers) ────────────────
    # None of this is needed for our flow to work (SLSsteam reads
    # AdditionalApps, lumalinux serves keys from keys.txt). It's solely so
    # other tools find the game: SteaMidra-style scanners that look at
    # stplug-in, DeckTools / LumaDeck's has_lua_for_app check, and ACCELA /
    # ASSella in Desktop Mode (in-game marker + ~/.local/share/ACCELA/depots/
    # tracker for their update-detection UI). Each step is best-effort with
    # its own log line so the user can see what landed.
    print(f"== Copiando .lua a stplug-in (interop SteaMidra / DeckTools) ==")
    try:
        lua_dest = install_lua_to_stplugin(args.steam_root, app_id, lua_text, args.pin)
        print(f"  [+] {lua_dest}")
    except Exception as exc:
        print(f"  [!] no pude escribir el .lua a stplug-in: {exc}")
    print()

    # In-game marker. The installdir MUST match the folder Steam actually
    # downloads into, so we read it from the .acf — the single source of truth.
    #
    # Since we stopped seeding a manifest, on a fresh add there is no .acf yet
    # and no folder to mark: we skip instead of guessing. Guessing str(app_id)
    # would only create an empty steamapps/common/<appid>/ nobody ever reads.
    # `--accela-mark <appid>` after the install places it correctly.
    installdir_for_marker = _read_installdir_from_acf(args.steam_root, app_id)

    print(f"== Creando markers para ACCELA/ASSella (interop Desktop Mode) ==")
    if not installdir_for_marker:
        print("  [-] sin .acf todavía (el juego aún no está instalado) — "
              "marker omitido; usa --accela-mark tras instalar")
    else:
        try:
            marker_dir = mark_game_for_accela(args.steam_root, app_id, installdir_for_marker)
            has_content = _game_dir_has_content(
                args.steam_root / "steamapps" / "common" / installdir_for_marker)
            ready = "sí" if has_content else "todavía no — re-ejecuta --accela-mark tras instalar"
            print(f"  [+] {marker_dir} (in-game marker, installdir='{installdir_for_marker}', contenido={ready})")
        except Exception as exc:
            print(f"  [!] no pude crear el in-game marker: {exc}")

    main_depot = _pick_main_depot_for_accela(depot_keys, manifests, app_id)
    if main_depot:
        depot_id, manifest_id = main_depot
        app_token = _get_app_token_for(args, app_id)
        try:
            depot_file = write_accela_depot_marker(app_id, depot_id, manifest_id, app_token)
            print(f"  [+] {depot_file} (update tracking: depot {depot_id} → manifest {manifest_id})")
        except Exception as exc:
            print(f"  [!] no pude escribir el .depot de ACCELA: {exc}")
    else:
        print(f"  [-] sin depot principal con manifest_gid≠0 → .depot no escrito")
    print()

    print("== Hecho ==")
    print("Backups creados con sufijo .bak.")
    print("Reinicia Steam con lumalinux + SLSsteam cargados y prueba el Install.")


if __name__ == "__main__":
    main()
