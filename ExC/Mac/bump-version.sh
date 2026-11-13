#!/usr/bin/env bash
#
# bump-version.sh - Aggiorna il numero di versione marketing in TUTTI i punti che
# devono restare coerenti, poi committa. Evita il disallineamento (es. 1.0 vs 3.0)
# nato dal doverli modificare a mano uno per uno.
#
# Aggiorna:
#   CMakeLists.txt   : project(SurfaceExplorer VERSION X.Y ...)  <- unico punto
# Info.plist e Info-macos.plist non contengono il numero: CFBundleShortVersionString
# vale ${MACOSX_BUNDLE_SHORT_VERSION_STRING}, che CMake riempie da ${PROJECT_VERSION}
# (dal 2026-11-06). Lo script verifica soltanto che restino cosi'.
#
# I campi del bundle (MACOSX_BUNDLE_SHORT_VERSION_STRING, XCODE_..._MARKETING_VERSION)
# e la define APP_VERSION letta dal dialogo About derivano da ${PROJECT_VERSION}:
# non contengono il numero e non vanno aggiornati.
#
# NON tocca il BUILD NUMBER (MACOSX_BUNDLE_BUNDLE_VERSION / CFBundleVersion): quello
# si incrementa a parte, a ogni upload verso Apple (vedi RELEASE_GUIDE.md).
#
# Uso:
#   ./ExC/Mac/bump-version.sh 1.1          aggiorna i file e committa "Bump versione 1.1"
#   ./ExC/Mac/bump-version.sh 1.1 --no-commit   aggiorna soltanto i file
#
# Portabile Linux/macOS (bash + sed/awk POSIX). Su Windows: Git Bash o WSL.

set -euo pipefail

# --- root del progetto: lo script vive in ExC/Mac/, la root e' due cartelle sopra ---
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CMAKE="$PROJECT_DIR/CMakeLists.txt"
PLIST="$PROJECT_DIR/Info.plist"
PLIST_MAC="$PROJECT_DIR/Info-macos.plist"   # bundle macOS App Store (CMakeLists:281)

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

# --- argomenti ---
NEW="${1:-}"
DO_COMMIT=1
[ "${2:-}" = "--no-commit" ] && DO_COMMIT=0
[ -n "$NEW" ] || err "Uso: $0 X.Y [--no-commit]   (es. $0 1.1)"
printf '%s' "$NEW" | grep -qE '^[0-9]+\.[0-9]+$' || err "Versione '$NEW' non valida: attesa X.Y (es. 1.1)"
[ -f "$CMAKE" ] || err "CMakeLists.txt non trovato in $PROJECT_DIR"
[ -f "$PLIST" ] || err "Info.plist non trovato in $PROJECT_DIR"
[ -f "$PLIST_MAC" ] || err "Info-macos.plist non trovato in $PROJECT_DIR"

# --- versione attuale (per il riepilogo) ---
OLD="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' "$CMAKE" | head -1)"
OLD="${OLD:-?}"

# --- 1. CMakeLists.txt (3 campi) ---------------------------------------------
# NB: il primo pattern ancora 'VERSION' a inizio riga (dopo soli spazi) per NON
# toccare cmake_minimum_required(VERSION ...) ne' i *_BUNDLE_VERSION (build number).
# Dalla 1.2 il numero sta in UN SOLO punto: project(SurfaceExplorer VERSION X.Y ...).
# I campi del bundle e la define APP_VERSION lo derivano da ${PROJECT_VERSION},
# quindi qui non vanno piu' toccati (se li si tocca si rompe la derivazione).
tmp="$(mktemp)"
sed -E \
  -e "s/^(project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+)[0-9][0-9.]*/\1${NEW}/" \
  "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"

# --- 2. i DUE plist: nessun numero da scrivere ------------------------------
# Info.plist       -> bundle iOS / macOS diretto
# Info-macos.plist -> bundle macOS App Store
# Fino al 2026-11-06 il numero era scritto anche li' e questo script lo sostituiva;
# ora CFBundleShortVersionString vale ${MACOSX_BUNDLE_SHORT_VERSION_STRING} e il
# controllo qui sotto verifica che nessuno ci abbia rimesso un numero a mano.

# --- verifica che i 4 punti riportino ora la nuova versione ------------------
check() { grep -qE "$1" "$2" || err "sostituzione fallita: $3 (pattern non trovato dopo l'edit)"; }
check "^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+${NEW}[[:space:]]" "$CMAKE" "CMakeLists project(VERSION)"
# La derivazione deve restare in piedi: se qualcuno rimette un numero letterale
# in questi campi, il bump aggiornerebbe project() e loro resterebbero indietro.
check 'MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+"\$\{PROJECT_VERSION\}"' "$CMAKE" "SHORT_VERSION_STRING derivato da PROJECT_VERSION"
check 'XCODE_ATTRIBUTE_MARKETING_VERSION[[:space:]]+"\$\{PROJECT_VERSION\}"'  "$CMAKE" "MARKETING_VERSION derivato da PROJECT_VERSION"
check 'APP_VERSION="\$\{PROJECT_VERSION\}"'                                   "$CMAKE" "define APP_VERSION derivata da PROJECT_VERSION"
for p in "$PLIST" "$PLIST_MAC"; do
  grep -A1 '<key>CFBundleShortVersionString</key>' "$p" \
    | grep -qF '<string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>' \
    || err "$(basename "$p"): CFBundleShortVersionString deve valere \${MACOSX_BUNDLE_SHORT_VERSION_STRING}, non un numero"
done

printf '\nVersione: %s -> %s   (solo CMakeLists.txt: i plist la ricevono da CMake)\n' "$OLD" "$NEW"

# --- Il dialogo About NON va piu' toccato: dalla 1.2 stampa APP_VERSION, che
# arriva da project(... VERSION ...) nel CMakeLists via target_compile_definitions.
# Se un giorno tornasse a mostrare un numero sbagliato, il colpevole e' un binario
# stantio (ricompilare) oppure qualcuno che ha riscritto a mano quella stringa.

# --- 3. commit (salvo --no-commit) -------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  ( cd "$PROJECT_DIR" && git add CMakeLists.txt \
      && git commit -m "Bump versione $NEW" )
  printf '\nCommit creato. Ricorda di: git push\n'
else
  printf '\n--no-commit: file aggiornati ma non committati.\n'
fi
