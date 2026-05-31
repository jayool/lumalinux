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
  4. Inyecta las DecryptionKeys en ~/.local/share/Steam/config/config.vdf
     (POR DEFECTO; --no-vdf para saltar). Requiere 'pip install --user vdf'.
     Cierra Steam ANTES de correr el script (Steam reescribe config.vdf al
     salir).
  5. (opcional, --token APPID:HEX) Añade un AppToken al config.yaml de SLSsteam.
  6. Resetea el error-state del .acf (sff/lua/writer.py:_patch_acf_error_state).
     UpdateResult, Bytes*, StagingSize → 0; StateFlags &= ~16. Sin este paso,
     Steam suele mostrar "NO INTERNET CONNECTION" al primer Install tras
     cualquier fallo previo (el bug está en cómo Steam interpreta UpdateResult
     stale, no en la red — verbatim del comment del propio SteaMidra). Si no
     hay .acf todavía, escribimos uno limpio.

Uso:
  python3 steamidra_lite.py 2379780.zip
  python3 steamidra_lite.py 2379780.zip --no-vdf
  python3 steamidra_lite.py 2379780.zip --token 2379780:0x123abc...

  # también acepta un .lua + dir manifests si no tienes ZIP:
  python3 steamidra_lite.py game.lua --manifests-dir ./manifests/

NO escribe el .acf — Steam lo escribe solo cuando pulses Install.
"""

import argparse
import os
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

RE_APPID         = re.compile(r"^\s*addappid\s*\(\s*(\d+)\s*\)", re.MULTILINE)
RE_DEPOT_KEY     = re.compile(r"^\s*addappid\s*\(\s*(\d+)\s*,\s*\d\s*,\s*[\"'](\S+?)[\"']\s*\)", re.MULTILINE)
# Hubcap .lua emits setManifestid with 3 args (depot, "gid", size). Older formats
# had only 2 (depot, "gid"). Accept both — the size is captured but optional.
RE_MANIFEST_GID  = re.compile(r"^\s*setManifestid\s*\(\s*(\d+)\s*,\s*[\"'](\d+)[\"']\s*(?:,\s*(\d+))?\s*\)", re.MULTILINE)
# Hubcap .lua marks shared depots with this comment block. Everything declared
# after this header until the next blank-line / EOF is treated as shared (Steam
# has these in pSharedDepotInfo already; we serve their key but DO NOT inject).
RE_SHARED_HEADER = re.compile(r"--\s*SHARED DEPOTS\b", re.IGNORECASE)


def parse_lua_text(text):
    appids = [int(m) for m in RE_APPID.findall(text)]
    if not appids:
        sys.exit("ERROR: no addappid(N) en el .lua. Revisa el archivo.")
    app_id = appids[0]
    extra_appids = appids[1:]
    depot_keys = {int(d): k for d, k in RE_DEPOT_KEY.findall(text)}
    manifests  = {int(d): g for d, g, _size in RE_MANIFEST_GID.findall(text)}
    manifest_sizes_from_lua = {int(d): int(s) for d, _g, s in RE_MANIFEST_GID.findall(text) if s}

    # Detect which depots are SHARED (Steam already has them in pSharedDepotInfo).
    # We split the .lua at the "-- SHARED DEPOTS" header and treat every depot
    # declared after that as shared.
    shared_depots = set()
    m = RE_SHARED_HEADER.search(text)
    if m:
        for did, _key in RE_DEPOT_KEY.findall(text[m.end():]):
            shared_depots.add(int(did))

    return app_id, extra_appids, depot_keys, manifests, manifest_sizes_from_lua, shared_depots


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


def write_lumalinux_keys(keys_path, depot_keys, manifests, manifest_sizes, app_id, shared_depots):
    """Mezcla con lo existente (no rompe entries de otros juegos) y escribe:

    - Content depots (no shared, no es el AppID) → EXTENDED:
        depot_id;parent_app_id;manifest_gid;manifest_size;hex_key
      lumalinux inyecta estos en pDepotInfo cuando Steam construye las deps de
      parent_app_id (BuildDepotDependency hook).

    - Shared depots (e.g. VC 2019 Redist 228988 desde el comment "-- SHARED
      DEPOTS" del .lua de Hubcap) → LEGACY:
        depot_id;hex_key
      Steam ya los tiene en pSharedDepotInfo (cualquier juego owned que los use
      como dependencia los registra). Si los pusiéramos en EXTENDED, lumalinux
      intentaría inyectarlos en pDepotInfo → duplicación, Steam los rechaza con
      "configuración de contenido no válida".

    - El AppID principal → entry dummy con key de ceros (LEGACY-ish):
        app_id;0000...0000
      Steam pregunta por la "key del AppID" durante el bootstrap del install;
      el DepotKey hook responde con el placeholder para que el flow no se quede
      esperando."""
    keys_path.parent.mkdir(parents=True, exist_ok=True)
    # Merge with existing entries (preserves entries from previous runs for other apps)
    existing = {}  # {depot_id: tuple — 5-tuple (ext) or 2-tuple (legacy)}
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
                    existing[did] = ("leg", parts[1])
            except ValueError:
                continue

    added = 0
    updated = 0
    for did, key in depot_keys.items():
        if did == app_id:
            new_entry = ("leg", "0" * 64)
        elif did in shared_depots:
            new_entry = ("leg", key)
        else:
            gid = manifests.get(did, 0)
            size = manifest_sizes.get(did, 0)
            new_entry = ("ext", app_id, gid, size, key)
        if did not in existing:
            existing[did] = new_entry
            added += 1
        elif existing[did] != new_entry:
            existing[did] = new_entry
            updated += 1

    # Ensure AppID dummy line exists even if the .lua didn't list it as a depot_key
    if app_id not in existing:
        existing[app_id] = ("leg", "0" * 64)
        added += 1

    lines = [
        "# lumalinux keys.txt — managed by steamidra_lite.py",
        "# EXTENDED: depot_id;parent_app_id;manifest_gid;manifest_size;hex_key   (content depots — lumalinux inyecta en pDepotInfo)",
        "# LEGACY:   depot_id;hex_key                                            (shared depots + AppID dummy — lumalinux solo sirve la key)",
    ]
    for did in sorted(existing):
        e = existing[did]
        if e[0] == "ext":
            _, parent, gid, size, key = e
            lines.append(f"{did};{parent};{gid};{size};{key}")
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
# CONNECTION'").
_ACF_ERROR_FIELDS = (
    ("UpdateResult", "0"),
    ("FullValidateAfterNextUpdate", "0"),
    ("ScheduledAutoUpdate", "0"),
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


def write_or_patch_acf(steam_root, app_id, manifest_gids):
    """Step 6 of the install flow (replicates sff/lua/writer.py:write_acf +
    _patch_acf_error_state). Without this step Steam often shows 'NO INTERNET
    CONNECTION' on the next install attempt after any failure, even with the
    network actually up.

    - If the .acf already exists → patch the error-state fields (UpdateResult,
      Bytes*, etc.) back to 0 and clear the Update-Required bit (StateFlags
      AND 0xFFEF). This matches _patch_acf_error_state verbatim.
    - If the .acf doesn't exist yet → write a fresh one with StateFlags=4
      (Fully Installed placeholder) so Steam has a clean slate. write_acf in
      SteaMidra Windows does this; in Linux native we want the same.

    manifest_gids: {depot_id: gid} for InstalledDepots stub (optional, only
    written when creating a new .acf — Steam fills the real entries during
    install)."""
    acf_path = steam_root / "steamapps" / f"appmanifest_{app_id}.acf"
    if acf_path.exists():
        try:
            shutil.copy2(acf_path, acf_path.with_suffix(".acf.bak"))
            data = _vdf_load_acf(acf_path)
            app_state = data.get("AppState", {})
            patched = False
            for k, clean in _ACF_ERROR_FIELDS:
                if app_state.get(k) != clean:
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
                _vdf_dump_acf(acf_path, data)
                return "patched"
            return "clean"
        except Exception as e:
            print(f"  [WARN] No pude patchar {acf_path}: {e}")
            return "error"

    # No .acf yet — write a fresh stub. Steam will overwrite it with real
    # depot info during install; we just want to ensure no stale state.
    acf_path.parent.mkdir(parents=True, exist_ok=True)
    app_state = {
        "appid": str(app_id),
        "Universe": "1",
        "StateFlags": "4",
        "installdir": str(app_id),
        "LastUpdated": "0",
        "UpdateResult": "0",
        "SizeOnDisk": "0",
        "BytesToDownload": "0",
        "BytesDownloaded": "0",
    }
    if manifest_gids:
        app_state["InstalledDepots"] = {
            str(d): {"manifest": str(g), "size": "0"}
            for d, g in manifest_gids.items() if g
        }
    _vdf_dump_acf(acf_path, {"AppState": app_state})
    return "created"


def main():
    ap = argparse.ArgumentParser(
        description="Replica el deploy de SteaMidra (Linux) sin instalarlo.",
        epilog="Por defecto acepta un .zip estilo Hubcap; con --manifests-dir acepta .lua suelto."
    )
    ap.add_argument("input", type=Path,
                    help="ruta al archivo de entrada: .zip Hubcap (default) o .lua si pasas --manifests-dir")
    ap.add_argument("--manifests-dir", type=Path, default=None,
                    help="(modo legacy) directorio con .manifest si el input es un .lua suelto")
    ap.add_argument("--steam-root", type=Path, default=Path.home()/".local/share/Steam",
                    help="raíz de Steam (default: ~/.local/share/Steam)")
    ap.add_argument("--sls-config", type=Path, default=Path.home()/".config/SLSsteam/config.yaml",
                    help="config.yaml de SLSsteam")
    ap.add_argument("--luma-keys", type=Path, default=Path.home()/".config/lumalinux/keys.txt",
                    help="keys.txt de lumalinux")
    ap.add_argument("--no-vdf", action="store_true",
                    help="NO escribir las DecryptionKeys en Steam config.vdf. "
                         "Por defecto SÍ se escriben (SteaMidra Linux también lo hace; "
                         "el hook DepotKey de lumalinux es backup runtime).")
    ap.add_argument("--token", action="append", default=[], metavar="APPID:HEX",
                    help="añadir un AppToken al config.yaml de SLSsteam. Puedes pasarlo varias veces.")
    args = ap.parse_args()

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

    print(f"== Parseando .lua ==")
    app_id, extra_appids, depot_keys, manifests_from_lua, sizes_from_lua, shared_depots = parse_lua_text(lua_text)
    # Merge: los del .lua tienen prioridad, los de los filenames del zip rellenan huecos
    manifests = dict(manifests_from_names)
    manifests.update(manifests_from_lua)
    # Sizes: el .lua manda (Hubcap los incluye en setManifestid 3-arg); si no, el
    # binary parse hecho en extract_zip.
    for d, s in sizes_from_lua.items():
        manifest_sizes.setdefault(d, s)
    print(f"  appid principal:    {app_id}")
    if extra_appids: print(f"  appids extra:       {extra_appids}")
    print(f"  depots con key:     {sorted(depot_keys.keys())}")
    if shared_depots:
        print(f"  shared depots:      {sorted(shared_depots)}  (LEGACY format, no injection)")
    print(f"  manifests GID:      {sorted(manifests.keys())}")
    print(f"  manifest sizes (cb_disk_original):")
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
    # Content depots → EXTENDED (lumalinux inyecta en pDepotInfo).
    # Shared depots → LEGACY (Steam ya los tiene en pSharedDepotInfo).
    # AppID principal → entry dummy con key de ceros (DepotKey hook responde
    # cuando Steam pregunta por la "key del app" durante el flow de install).
    print(f"== Escribiendo {args.luma_keys} ==")
    n_new, n_upd = write_lumalinux_keys(
        args.luma_keys, depot_keys, manifests, manifest_sizes, app_id, shared_depots)
    print(f"  [+] {n_new} keys nuevas, {n_upd} actualizadas")
    patchable = [d for d in depot_keys if d != app_id and d not in shared_depots
                 and manifests.get(d, 0) != 0]
    print(f"  [*] depots que BuildDep parcheará (parent={app_id}, gid≠0, NO shared): {patchable}")
    if not patchable:
        print(f"  [!] AVISO: ningún content depot con gid≠0. BuildDep no parcheará nada.")
    print()

    # ── config.vdf — siempre por defecto (--no-vdf para skipear) ──────────
    # SteaMidra Linux (sff/ui.py:process_lua_full) lo hace siempre. Steam consulta
    # config.vdf primero para depot keys; aunque lumalinux las sirve runtime via
    # el hook DepotKey, tenerlas también ahí cubre paths donde Steam no llama al
    # hook (p.ej. validación pre-descarga del manifest signature).
    if not args.no_vdf:
        print(f"== Añadiendo keys a {args.steam_root/'config/config.vdf'} ==")
        n_vdf = update_config_vdf(args.steam_root/"config/config.vdf", depot_keys)
        print(f"  [+] {n_vdf} keys nuevas en config.vdf")
        print()

    # ── .acf error-state reset ────────────────────────────────────────────
    # Without this Steam often shows "NO INTERNET CONNECTION" on the first
    # install attempt — it's not a network issue, it's stale UpdateResult /
    # Bytes* fields in the .acf from a previous failure. Verbatim from
    # sff/lua/writer.py:113: "this is what causes 'NO INTERNET CONNECTION'".
    print(f"== Reseteando error-state del .acf (paso que evita 'no internet') ==")
    acf_result = write_or_patch_acf(args.steam_root, app_id, manifests)
    print(f"  [+] appmanifest_{app_id}.acf: {acf_result}")
    print()

    print("== Hecho ==")
    print("Backups creados con sufijo .bak.")
    print("Reinicia Steam con lumalinux + SLSsteam cargados y prueba el Install.")


if __name__ == "__main__":
    main()
