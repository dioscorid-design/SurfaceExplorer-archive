#!/usr/bin/env bash
#
# release_testflight.sh - Prepara e archivia una nuova build iOS per TestFlight.
#
# Automatizza i passi 3-5 della guida ExC/docs/GUIDA_rilascio_build_ios_macos.md:
#   1. Incrementa il BUILD NUMBER (+1) in TUTTI i punti coerenti:
#        CMakeLists.txt : MACOSX_BUNDLE_BUNDLE_VERSION, XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION
#        Info.plist     : CFBundleVersion
#      (la VERSIONE MARKETING X.Y NON viene toccata: si cambia con ExC/bump-version.sh)
#   2. Committa il bump (a meno di --no-commit).
#   3. Pulisce la cache Xcode (DerivedData) e la cartella del progetto iOS generato:
#      il passo CRITICO della guida — senza, Xcode riusa oggetti stantii e produce un
#      binario che passa la validazione ma non contiene i fix.
#   4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain iOS).
#   5. Archivia con `xcodebuild archive`.
#   6. Apre l'Organizer di Xcode per l'upload manuale su App Store Connect
#      (Distribute App -> App Store Connect -> Upload), come da guida al passo 6.
#
# NON fa l'upload: quello resta manuale in Xcode (nessuna API key richiesta). Dopo
# l'upload seguire i passi 7-8 della guida (Export Compliance, assegnazione al gruppo
# Internal Testers).
#
# Uso:
#   ./ExC/Mac/release_testflight.sh                 bump +1, commit, clean, archivia, apri Organizer
#   ./ExC/Mac/release_testflight.sh --no-commit     come sopra ma NON committa il bump
#   ./ExC/Mac/release_testflight.sh --no-archive    solo bump + clean + rigenera Xcode, poi apre il progetto
#   ./ExC/Mac/release_testflight.sh --help
#
# Richiede: macOS, Xcode + command line tools, qt-cmake iOS (Qt 6.12.0).

set -euo pipefail

# ---------------------------------------------------------------------------
# Configurazione (allineata a ExC/docs/GUIDA_rilascio_build_ios_macos.md)
# ---------------------------------------------------------------------------
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"      # lo script vive in ExC/Mac/
CMAKE="$PROJECT_DIR/CMakeLists.txt"
PLIST="$PROJECT_DIR/Info.plist"
BUILD_DIR="$PROJECT_DIR/build/ios-appstore"
XCODEPROJ="$BUILD_DIR/SurfaceExplorer.xcodeproj"
SCHEME="SurfaceExplorer"
QT_CMAKE="$HOME/Qt/6.12.0/ios_device/bin/qt-cmake"
GIT_BRANCH="v1"
# Cartella dove xcodebuild deposita l'archivio (poi visibile anche nell'Organizer).
ARCHIVE_DIR="$HOME/Library/Developer/Xcode/Archives"

err()  { printf '\033[31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[32m✓\033[0m %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Argomenti
# ---------------------------------------------------------------------------
DO_COMMIT=1
DO_ARCHIVE=1
for arg in "$@"; do
  case "$arg" in
    --no-commit)  DO_COMMIT=0 ;;
    --no-archive) DO_ARCHIVE=0 ;;
    --help|-h)
      sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) err "Argomento sconosciuto: $arg  (usa --help)" ;;
  esac
done

# ---------------------------------------------------------------------------
# Controlli preliminari
# ---------------------------------------------------------------------------
[ "$(uname)" = "Darwin" ] || err "Questo script gira solo su macOS."
[ -f "$CMAKE" ] || err "CMakeLists.txt non trovato in $PROJECT_DIR"
[ -f "$PLIST" ] || err "Info.plist non trovato in $PROJECT_DIR"
command -v xcodebuild >/dev/null || err "xcodebuild non trovato (installa Xcode + command line tools)."
[ -x "$QT_CMAKE" ] || err "qt-cmake iOS non trovato/eseguibile: $QT_CMAKE"

cd "$PROJECT_DIR"

# Avviso se ci sono modifiche non committate (verranno incluse nel commit del bump).
if [ "$DO_COMMIT" -eq 1 ] && ! git diff --quiet HEAD 2>/dev/null; then
  info "Working tree con modifiche non committate: verranno incluse nel commit del bump."
fi

# ---------------------------------------------------------------------------
# 1. Leggi e incrementa il build number
# ---------------------------------------------------------------------------
CUR_BUILD="$(sed -nE 's/.*MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
[ -n "$CUR_BUILD" ] || err "Build number non trovato in CMakeLists.txt (MACOSX_BUNDLE_BUNDLE_VERSION)."
NEW_BUILD=$(( CUR_BUILD + 1 ))
# Da project(...): MARKETING_VERSION ora vale "${PROJECT_VERSION}", non un numero.
MARKETING="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' "$CMAKE" | head -1)"
MARKETING="${MARKETING:-?}"

info "Versione marketing: $MARKETING  |  Build number: $CUR_BUILD -> $NEW_BUILD"

# --- CMakeLists.txt: i due campi build number (devono restare uguali) ---
tmp="$(mktemp)"
sed -E \
  -e "s/(MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  -e "s/(XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"

# --- Info.plist: <string> sulla riga DOPO la chiave CFBundleVersion ---
tmp="$(mktemp)"
awk -v v="$NEW_BUILD" '
  f && /<string>[0-9]+<\/string>/ { sub(/<string>[0-9]+<\/string>/, "<string>" v "</string>"); f=0 }
  /<key>CFBundleVersion<\/key>/ { f=1 }
  { print }
' "$PLIST" > "$tmp" && mv "$tmp" "$PLIST"

# --- Verifica coerenza dei tre punti ---
C1="$(sed -nE 's/.*MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
C2="$(sed -nE 's/.*XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
P1="$(awk '/<key>CFBundleVersion<\/key>/{getline; gsub(/[^0-9]/,""); print; exit}' "$PLIST")"
[ "$C1" = "$NEW_BUILD" ] && [ "$C2" = "$NEW_BUILD" ] && [ "$P1" = "$NEW_BUILD" ] \
  || err "Bump build number incoerente: CMake($C1,$C2) Plist($P1), atteso $NEW_BUILD. Controlla i file."
ok "Build number aggiornato a $NEW_BUILD (CMake x2 + Info.plist)."

# Info.plist deve restare un plist valido (una copia-cartella lo puo' corrompere).
plutil -lint "$PLIST" >/dev/null || err "Info.plist non valido dopo il bump (plutil -lint fallito)."

# ---------------------------------------------------------------------------
# 2. Commit del bump
# ---------------------------------------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  git add "$CMAKE" "$PLIST"
  git commit -m "iOS: build number -> $NEW_BUILD (TestFlight)" >/dev/null
  ok "Commit del bump creato."
  info "Ricorda di fare 'git push origin $GIT_BRANCH' quando vuoi."
else
  info "--no-commit: bump applicato ai file ma NON committato."
fi

# ---------------------------------------------------------------------------
# 3. Pulizia cache Xcode + progetto iOS generato (passo critico della guida)
# ---------------------------------------------------------------------------
info "Pulizia DerivedData (SurfaceExplorer-*) e $BUILD_DIR ..."
# find, non glob: su zsh un glob senza match dà "no matches found" e blocca.
find "$HOME/Library/Developer/Xcode/DerivedData" -maxdepth 1 -name 'SurfaceExplorer-*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$BUILD_DIR"
ok "Cache e progetto iOS puliti."

# ---------------------------------------------------------------------------
# 4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain iOS)
# ---------------------------------------------------------------------------
info "Rigenerazione progetto Xcode iOS con qt-cmake ..."
"$QT_CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Xcode >/dev/null
[ -d "$XCODEPROJ" ] || err "Rigenerazione fallita: $XCODEPROJ non creato."
ok "Progetto Xcode rigenerato."

# Verifica che il progetto porti i valori attesi.
info "Valori nel progetto generato:"
grep -h "MARKETING_VERSION\|CURRENT_PROJECT_VERSION\|PRODUCT_BUNDLE_IDENTIFIER" \
  "$XCODEPROJ/project.pbxproj" | sort -u | sed 's/^/    /'

if [ "$DO_ARCHIVE" -eq 0 ]; then
  info "--no-archive: apro il progetto in Xcode. Fai Product -> Archive a mano."
  open "$XCODEPROJ"
  ok "Fatto (preparazione build)."
  exit 0
fi

# ---------------------------------------------------------------------------
# 5. Archive con xcodebuild
# ---------------------------------------------------------------------------
# Nome archivio con timestamp, nella cartella standard degli Archivi così compare
# anche nell'Organizer di Xcode.
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
ARCHIVE_PATH="$ARCHIVE_DIR/$(date +%Y-%m-%d)/SurfaceExplorer_${MARKETING}_${NEW_BUILD}_${STAMP}.xcarchive"
mkdir -p "$(dirname "$ARCHIVE_PATH")"

info "Archiviazione (xcodebuild archive) — può richiedere PARECCHI minuti (Qt statico)."
info "NON interrompere anche se sembra ferma."

# Log COMPLETO su file: `| tail -40` da solo buttava via proprio le righe utili.
# Gli errori di firma di xcodebuild compaiono a meta' log (riga ~47 su ~624) e
# le ultime 40 righe contengono solo "** ARCHIVE FAILED **" e l'elenco dei
# comandi falliti: il messaggio che dice COSA fare non si vedeva mai.
XC_LOG="$(mktemp -t surfexp-archive)"
set +e
xcodebuild archive \
  -project "$XCODEPROJ" \
  -scheme "$SCHEME" \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath "$ARCHIVE_PATH" \
  CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates \
  > "$XC_LOG" 2>&1
XC_STATUS=$?
set -e

if [ "$XC_STATUS" -ne 0 ]; then
  printf '\033[31m--- errori riportati da xcodebuild ---\033[0m\n'
  # Le righe "error:" sono la diagnosi vera; se non ce ne fossero si mostra
  # comunque la coda del log per non restare senza informazioni.
  if grep -qE "error:" "$XC_LOG"; then
    grep -E "error:" "$XC_LOG" | sort -u | sed 's/^/  /'
  else
    tail -40 "$XC_LOG" | sed 's/^/  /'
  fi
  printf '\033[31m--------------------------------------\033[0m\n'
  info "Log completo: $XC_LOG"
  # Firma/profili: e' il modo tipico in cui fallisce, e la causa e' quasi sempre
  # il DEVELOPMENT_TEAM di CMakeLists.txt che non e' quello dei profili installati.
  if grep -q "No Account for Team\|No profiles for" "$XC_LOG"; then
    info "Sembra un problema di FIRMA. Team richiesto dal progetto:"
    grep -h "DEVELOPMENT_TEAM" "$XCODEPROJ/project.pbxproj" | sort -u | sed 's/^/    /'
    info "Team dei profili installati:"
    for f in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"/*.mobileprovision; do
      [ -e "$f" ] || continue
      security cms -D -i "$f" 2>/dev/null > "$XC_LOG.pp" || continue
      printf '    %s  (%s)\n' \
        "$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$XC_LOG.pp" 2>/dev/null)" \
        "$(/usr/libexec/PlistBuddy -c 'Print :Name' "$XC_LOG.pp" 2>/dev/null)"
    done | sort -u
    rm -f "$XC_LOG.pp"
    info "Se non coincidono, allinea DEVELOPMENT_TEAM in CMakeLists.txt (due punti: ramo iOS e ramo macOS)."
  fi
  err "xcodebuild archive fallito (exit $XC_STATUS)."
fi
rm -f "$XC_LOG"
[ -d "$ARCHIVE_PATH" ] || err "Archivio non prodotto: $ARCHIVE_PATH"
ok "Archivio creato: $ARCHIVE_PATH"

# Verifica anti-stantìo: il binario deve essere più recente dell'ultima modifica ai sorgenti.
BIN="$ARCHIVE_PATH/Products/Applications/SurfaceExplorer.app/SurfaceExplorer"
if [ -f "$BIN" ]; then
  info "Binario archiviato: $(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$BIN")"
fi

# ---------------------------------------------------------------------------
# 6. Apri l'Organizer per l'upload manuale
# ---------------------------------------------------------------------------
info "Apro l'Organizer di Xcode. Da lì:"
cat <<'STEPS'
    1. Seleziona l'archivio appena creato (in alto la Version deve mostrare il nuovo build).
    2. Distribute App -> App Store Connect -> Upload -> Next.
    3. Lascia attivo "Upload your app's symbols" -> Next.
    4. Automatically manage signing -> Next -> Upload.
       (Un warning "Upload Symbols Failed" sui dSYM è NON bloccante: Done e prosegui.)
    5. Poi: attendi l'elaborazione, gestisci Export Compliance ("Nessuno degli algoritmi"),
       assegna la build al gruppo "Internal Testers". Vedi ExC/docs/GUIDA_rilascio_build_ios_macos.md
       passi 7-8.
STEPS
open -a Xcode "$ARCHIVE_PATH"
ok "Build $MARKETING ($NEW_BUILD) pronta per l'upload su TestFlight."
