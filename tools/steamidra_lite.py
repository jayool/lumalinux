#!/usr/bin/env python3
"""
steamidra_lite.py — replica el deploy de SteaMidra (Linux) con código mínimo.

Lee un .zip estilo Hubcap (nombre típico: {appid}.zip, contiene UN .lua y N
.manifest) y escribe en disco lo que necesita Steam para ofrecer el Install de
un juego no-owned cuando lumalinux + SLSsteam están cargados.

Acciones:
  1. Extrae los .manifest del ZIP a ~/.local/share/Steam/depotcache/
     (los .manifest del Hubcap ya vienen con nombre {depot}_{gid}.manifest)
  2. Añade el appid + depot ids al AdditionalApps de
     ~/.config/SLSsteam/config.yaml (edición line-based segura, backup .bak)
  3. Escribe las depot keys en ~/.config/lumalinux/keys.txt (formato simple)
  4. (opcional, --vdf) Mete las keys también en
     ~/.local/share/Steam/config/config.vdf (requiere 'pip install --user vdf';
     si no, se salta sin error — lumalinux ya sirve las keys vía hook)
  5. (opcional, --token APPID:HEX) Añade un AppToken al config.yaml de SLSsteam

Uso:
  python3 steamidra_lite.py 2379780.zip
  python3 steamidra_lite.py 2379780.zip --vdf
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

RE_APPID        = re.compile(r"^\s*addappid\s*\(\s*(\d+)\s*\)", re.MULTILINE)
RE_DEPOT_KEY    = re.compile(r"^\s*addappid\s*\(\s*(\d+)\s*,\s*\d\s*,\s*[\"'](\S+?)[\"']\s*\)", re.MULTILINE)
RE_MANIFEST_GID = re.compile(r"^\s*setManifestid\s*\(\s*(\d+)\s*,\s*[\"'](\d+)[\"']\s*\)", re.MULTILINE)


def parse_lua_text(text):
    appids = [int(m) for m in RE_APPID.findall(text)]
    if not appids:
        sys.exit("ERROR: no addappid(N) en el .lua. Revisa el archivo.")
    app_id = appids[0]
    extra_appids = appids[1:]
    depot_keys = {int(d): k for d, k in RE_DEPOT_KEY.findall(text)}
    manifests  = {int(d): g for d, g in RE_MANIFEST_GID.findall(text)}
    return app_id, extra_appids, depot_keys, manifests


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


def write_lumalinux_keys(keys_path, depot_keys, manifests, manifest_sizes, app_id):
    """keys.txt formato EXTENDIDO de lumalinux:
       `depot_id;parent_app_id;manifest_gid;manifest_size;hex_key`

    - parent_app_id: el appid principal del .lua para TODOS los depots con key.
      lumalinux usa GetDepotsForApp(app) para saber qué entries de pDepotInfo
      parchear cuando construye las deps de ese app; los content depots y los
      shared (p.ej. Steamworks redist 228989) aparecen al construir las deps
      del app, así que todos deben llevar parent=app_id para que el PATCH los
      cubra. (Antes se usaba una heurística de "distancia al appid" frágil; el
      .lua no distingue content vs shared, así que asignamos todos al app.)
    - manifest_size: cb_disk_original parseado del .manifest. NO es crítico —
      LumaCore solo lo usa para el display de progreso (si es 0, mantiene el
      original). El crash que vimos al seleccionar el juego NO venía del size=0
      sino de inyectar depots en BuildDep; eso ya no se hace."""
    keys_path.parent.mkdir(parents=True, exist_ok=True)
    # Merge with existing entries (preserves entries from previous runs for other apps)
    existing = {}  # {depot_id: (parent_app_id, manifest_gid, manifest_size, hex_key)}
    if keys_path.exists():
        for line in keys_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"): continue
            parts = line.split(";")
            try:
                if len(parts) == 5:  # extended
                    did = int(parts[0])
                    existing[did] = (int(parts[1]), int(parts[2]), int(parts[3]), parts[4])
                elif len(parts) == 2:  # legacy, upgrade to extended with zeros
                    did = int(parts[0])
                    existing[did] = (0, 0, 0, parts[1])
            except ValueError:
                continue

    added = 0
    updated = 0
    for did, key in depot_keys.items():
        gid = manifests.get(did, 0)
        size = manifest_sizes.get(did, 0)
        # parent = app_id para todos los depots con key (ver docstring). El
        # propio app (did == app_id, sin manifest) no es un depot de contenido,
        # así que lo dejamos con parent=0 para no confundir GetDepotsForApp.
        parent = app_id if did != app_id else 0
        new_entry = (parent, gid, size, key)
        if did not in existing:
            existing[did] = new_entry
            added += 1
        elif existing[did] != new_entry:
            existing[did] = new_entry
            updated += 1

    lines = ["# Formato extendido: depot_id;parent_app_id;manifest_gid;manifest_size;hex_key",
             "# (gestionado por steamidra_lite.py)"]
    for did in sorted(existing):
        parent, gid, size, key = existing[did]
        lines.append(f"{did};{parent};{gid};{size};{key}")
    keys_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return added, updated


def update_config_vdf(vdf_path, depot_keys):
    """Opcional: añade DecryptionKey al config.vdf de Steam. Requiere 'vdf'."""
    try:
        import vdf
    except ImportError:
        print("  [SKIP] config.vdf: módulo 'vdf' no instalado. "
              "Sin problema: lumalinux servirá las keys vía hook.")
        return 0
    if not vdf_path.exists():
        print(f"  [SKIP] config.vdf no existe en {vdf_path}. ¿Steam nunca arrancó?")
        return 0
    shutil.copy2(vdf_path, vdf_path.with_suffix(".vdf.bak"))
    with vdf_path.open(encoding="utf-8") as f:
        data = vdf.load(f, mapper=vdf.VDFDict)
    node = data
    for key in ("InstallConfigStore", "Software", "Valve", "Steam", "depots"):
        if key not in node:
            node[key] = vdf.VDFDict()
        node = node[key]
    added = 0
    for did, k in depot_keys.items():
        sid = str(did)
        if sid not in node:
            node[sid] = {"DecryptionKey": k}
            added += 1
    with vdf_path.open("w", encoding="utf-8") as f:
        vdf.dump(data, f, pretty=True)
    return added


def parse_token_arg(s):
    """'APPID:HEX' -> (appid_int, hex_str)"""
    try:
        a, t = s.split(":", 1)
        return int(a), t.strip()
    except Exception:
        sys.exit(f"ERROR: --token mal formado: '{s}'. Usa APPID:HEX")


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
    ap.add_argument("--vdf", action="store_true",
                    help="también añadir las keys a Steam config.vdf (redundante)")
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
    app_id, extra_appids, depot_keys, manifests_from_lua = parse_lua_text(lua_text)
    # Merge: los del .lua tienen prioridad, los de los filenames del zip rellenan huecos
    manifests = dict(manifests_from_names)
    manifests.update(manifests_from_lua)
    print(f"  appid principal:    {app_id}")
    if extra_appids: print(f"  appids extra:       {extra_appids}")
    print(f"  depots con key:     {sorted(depot_keys.keys())}")
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
    print(f"== Actualizando {args.sls_config} ==")
    all_appids = [app_id] + extra_appids + list(depot_keys.keys())
    tokens_dict = dict(parse_token_arg(t) for t in args.token) if args.token else None
    n_apps, n_tokens = update_sls_yaml(args.sls_config, all_appids, tokens_dict)
    print(f"  [+] {n_apps} appids nuevos en AdditionalApps")
    if tokens_dict:
        print(f"  [+] {n_tokens} tokens nuevos en AppTokens")
    print()

    # ── lumalinux keys.txt (formato EXTENDIDO) ────────────────────────────
    print(f"== Escribiendo {args.luma_keys} (formato extendido) ==")
    n_new, n_upd = write_lumalinux_keys(args.luma_keys, depot_keys, manifests, manifest_sizes, app_id)
    print(f"  [+] {n_new} keys nuevas, {n_upd} actualizadas")
    patchable = [d for d in depot_keys if d != app_id and manifests.get(d, 0) != 0]
    print(f"  [*] depots que BuildDep parcheará (parent={app_id}, gid≠0): {patchable}")
    if not patchable:
        print(f"  [!] AVISO: ningún depot con gid≠0. BuildDep no parcheará nada.")
    print()

    # ── config.vdf (opcional) ─────────────────────────────────────────────
    if args.vdf:
        print(f"== Añadiendo keys a {args.steam_root/'config/config.vdf'} ==")
        n_vdf = update_config_vdf(args.steam_root/"config/config.vdf", depot_keys)
        print(f"  [+] {n_vdf} keys nuevas en config.vdf")
        print()

    print("== Hecho ==")
    print("Backups creados con sufijo .bak.")
    print("Reinicia Steam con lumalinux + SLSsteam cargados y prueba el Install.")


if __name__ == "__main__":
    main()
