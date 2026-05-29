#!/usr/bin/env python3
"""
vdf_inject_keys.py — mete las DecryptionKey de los depots en el config.vdf de
Steam SIN depender del módulo 'vdf' (SteamOS no trae pip). Parser VDF de texto
propio, mínimo pero correcto para el formato de config.vdf.

Lee las keys de ~/.config/lumalinux/keys.txt:
  - extendido: depot_id;parent;gid;size;hexkey
  - legacy:    depot_id;hexkey

Inserta/actualiza:
  InstallConfigStore > Software > Valve > Steam > depots > <depot_id> > DecryptionKey

IMPORTANTE: Steam debe estar CERRADO (reescribe config.vdf al salir). Hace .bak.

Uso:
  python3 vdf_inject_keys.py
  python3 vdf_inject_keys.py --config <ruta config.vdf> --keys <ruta keys.txt>
"""
import argparse
import shutil
import sys
from pathlib import Path


# ───────────────────────── VDF text parser ─────────────────────────
# Soporta: tokens entre comillas, bloques { }, comentarios //, escapes \n \t \\ \"

def vdf_parse(text):
    i = 0
    n = len(text)

    def skip_ws():
        nonlocal i
        while i < n:
            c = text[i]
            if c in ' \t\r\n':
                i += 1
            elif text[i:i + 2] == '//':
                while i < n and text[i] != '\n':
                    i += 1
            else:
                break

    def read_token():
        nonlocal i
        skip_ws()
        if i >= n:
            return None
        c = text[i]
        if c == '{' or c == '}':
            i += 1
            return c
        if c == '"':
            i += 1
            buf = []
            while i < n:
                ch = text[i]
                if ch == '\\' and i + 1 < n:
                    nxt = text[i + 1]
                    buf.append({'n': '\n', 't': '\t', '\\': '\\', '"': '"'}.get(nxt, nxt))
                    i += 2
                    continue
                if ch == '"':
                    i += 1
                    break
                buf.append(ch)
                i += 1
            return ('STR', ''.join(buf))
        # token sin comillas
        start = i
        while i < n and text[i] not in ' \t\r\n{}"':
            i += 1
        return ('STR', text[start:i])

    def parse_block():
        items = []
        while True:
            tok = read_token()
            if tok is None or tok == '}':
                return items
            if tok == '{':
                continue  # anomalía: ignorar
            key = tok[1]
            nxt = read_token()
            if nxt is None:
                items.append((key, ''))
                return items
            if nxt == '{':
                items.append((key, parse_block()))
            elif nxt == '}':
                items.append((key, ''))
                return items
            else:
                items.append((key, nxt[1]))

    skip_ws()
    top = read_token()
    if top is None:
        return []
    nxt = read_token()
    if nxt == '{':
        return [(top[1], parse_block())]
    return [(top[1], nxt[1] if nxt else '')]


def vdf_dump(items, indent=0):
    out = []
    pad = '\t' * indent
    for key, val in items:
        if isinstance(val, list):
            out.append(f'{pad}"{key}"')
            out.append(f'{pad}{{')
            inner = vdf_dump(val, indent + 1)
            if inner:
                out.append(inner)
            out.append(f'{pad}}}')
        else:
            out.append(f'{pad}"{key}"\t\t"{val}"')
    return '\n'.join(out)


def find_child(items, key):
    for idx, (k, _v) in enumerate(items):
        if k.lower() == key.lower():
            return idx
    return -1


def ensure_path(items, path):
    cur = items
    for key in path:
        idx = find_child(cur, key)
        if idx < 0:
            cur.append((key, []))
            idx = len(cur) - 1
        k, v = cur[idx]
        if not isinstance(v, list):
            v = []
            cur[idx] = (k, v)
        cur = v
    return cur


def set_scalar(items, key, value):
    """Inserta o actualiza un par (key -> value escalar) en items."""
    idx = find_child(items, key)
    if idx < 0:
        items.append((key, value))
        return 'added'
    k, v = items[idx]
    if v == value:
        return 'same'
    items[idx] = (k, value)
    return 'updated'


# ───────────────────────── keys.txt loader ─────────────────────────

def load_keys(keys_path):
    keys = {}  # depot_id(str) -> hexkey
    for line in keys_path.read_text(encoding='utf-8', errors='replace').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(';')
        if len(parts) == 5:
            depot, key = parts[0].strip(), parts[4].strip()
        elif len(parts) == 2:
            depot, key = parts[0].strip(), parts[1].strip()
        else:
            continue
        if depot.isdigit() and key:
            keys[depot] = key
    return keys


def main():
    home = Path.home()
    ap = argparse.ArgumentParser(description="Inyecta DecryptionKey en config.vdf sin el módulo vdf.")
    ap.add_argument('--config', type=Path,
                    default=home / '.local/share/Steam/config/config.vdf')
    ap.add_argument('--keys', type=Path,
                    default=home / '.config/lumalinux/keys.txt')
    args = ap.parse_args()

    if not args.config.exists():
        sys.exit(f"ERROR: no existe config.vdf en {args.config}. ¿Steam nunca arrancó?")
    if not args.keys.exists():
        sys.exit(f"ERROR: no existe keys.txt en {args.keys}. Corre steamidra_lite.py primero.")

    keys = load_keys(args.keys)
    if not keys:
        sys.exit("ERROR: keys.txt no tiene ninguna key válida.")
    print(f"Keys a inyectar: {sorted(keys.keys())}")

    text = args.config.read_text(encoding='utf-8', errors='replace')
    tree = vdf_parse(text)
    if not tree or tree[0][0].lower() != 'installconfigstore':
        sys.exit(f"ERROR: config.vdf no empieza por InstallConfigStore (raíz='{tree[0][0] if tree else None}'). "
                 "Aborto para no corromperlo.")

    root_items = tree[0][1]
    if not isinstance(root_items, list):
        sys.exit("ERROR: InstallConfigStore no es un bloque. Aborto.")

    depots = ensure_path(root_items, ['Software', 'Valve', 'Steam', 'depots'])

    n_add = n_upd = n_same = 0
    for depot, key in sorted(keys.items()):
        didx = find_child(depots, depot)
        if didx < 0:
            depots.append((depot, [('DecryptionKey', key)]))
            n_add += 1
            print(f"  + depot {depot}: DecryptionKey añadida")
            continue
        dk, dv = depots[didx]
        if not isinstance(dv, list):
            dv = []
            depots[didx] = (dk, dv)
        r = set_scalar(dv, 'DecryptionKey', key)
        if r == 'added':
            n_add += 1; print(f"  + depot {depot}: DecryptionKey añadida (depot existía)")
        elif r == 'updated':
            n_upd += 1; print(f"  ~ depot {depot}: DecryptionKey actualizada")
        else:
            n_same += 1; print(f"  = depot {depot}: ya tenía la key correcta")

    backup = args.config.with_suffix('.vdf.lumabak')
    shutil.copy2(args.config, backup)
    out = vdf_dump(tree) + '\n'
    args.config.write_text(out, encoding='utf-8')

    print(f"\nHecho: {n_add} añadidas, {n_upd} actualizadas, {n_same} ya estaban.")
    print(f"Backup: {backup}")
    print("Arranca Steam y reintenta el Install de Balatro.")


if __name__ == '__main__':
    main()
