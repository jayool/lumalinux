# Testing Harness — Deck Dependencies

Esta branch existe solo para que yo (LumaDeck/jayool) suba tarballs con dependencias
del Deck reales, para que el harness de testing de Claude pueda probar la fix de
SLSsteam/CR/lumalinux en un entorno fiel al hardware.

## Cómo se sube (mucho más rápido que git push de binarios grandes)

Estos archivos no van al repo git — van a una **Release** asociada a esta branch.
Sube los tarballs como release assets vía drag-drop en el navegador. Ventajas:
- No comen el repo git
- 2 GB/asset, 8 GB/release
- Descargables por API con PAT

### Pasos:
1. Ir a https://github.com/jayool/lumalinux/releases/new
2. Elegir como tag: `test-deps-v1` (nuevo)
3. Elegir como target: branch `testing-harness`
4. Title: "Testing Dependencies v1"
5. Marcar como **pre-release** (para que no aparezca como "latest" del repo)
6. Arrastrar los tarballs al área de assets
7. "Publish release"

## Tarballs que necesito (en orden de prioridad)

### Imprescindibles
- `steam_ubuntu12_32.tar.gz` — el Steam binary + runtime
  ```bash
  tar czf steam_ubuntu12_32.tar.gz -C ~/.local/share/Steam ubuntu12_32
  ```
- `steamos_libs32.tar.gz` — las libs 32-bit del SteamOS
  ```bash
  sudo tar czhf steamos_libs32.tar.gz \
      /usr/lib32/libc.so.6 /usr/lib32/libpthread.so* \
      /usr/lib32/libdl.so* /usr/lib32/libgcc_s.so* \
      /usr/lib32/libstdc++.so* /usr/lib32/libcrypto.so.3 \
      /usr/lib32/libssl.so.3 /usr/lib32/libcurl.so.4 \
      /usr/lib32/ld-linux.so.2 /usr/lib32/libm.so.6 \
      /usr/lib32/libresolv.so.2 2>/dev/null
  ```

### Útil pero opcional
- `my_setup_libs.tar.gz` — tus binarios actuales de SLSsteam/CR/lumalinux
  ```bash
  tar czf my_setup_libs.tar.gz \
      ~/.local/share/SLSsteam/SLSsteam.so \
      ~/.local/share/SLSsteam/library-inject.so \
      ~/.local/share/CloudRedirect/cloud_redirect.so \
      ~/.local/share/lumalinux/liblumalinux.so
  ```
- `steam_sh.tar.gz` — tu steam.sh actual (modificado por Headcrab)
  ```bash
  tar czf steam_sh.tar.gz -C ~/.local/share/Steam steam.sh
  ```

## Después de subir

Avisas en el chat y Claude (yo) descarga los assets vía API:
```
curl -L -H "Authorization: token $PAT" \
  -H "Accept: application/octet-stream" \
  https://api.github.com/repos/jayool/lumalinux/releases/tags/test-deps-v1
```

Y monta el entorno de test fiel a tu Deck.
