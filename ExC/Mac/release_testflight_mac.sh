#!/usr/bin/env bash
#
# release_testflight_mac.sh - Prepara e archivia una nuova build macOS per TestFlight.
#
# Guida completa (iOS + macOS): ExC/docs/GUIDA_rilascio_build_ios_macos.md
#
# Gemello di release_testflight.sh (che resta la versione iOS, invariata). Stessa
# struttura e stessi passi; cambia solo cio' che e' davvero specifico del Mac:
#   - qt-cmake della toolchain macos invece di ios
#   - -destination 'generic/platform=macOS'
#   - cartella di build separata (build/macos-appstore)
#
#   1. Incrementa il BUILD NUMBER (+1) in TUTTI i punti coerenti:
#        CMakeLists.txt : MACOSX_BUNDLE_BUNDLE_VERSION, XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION
#        Info.plist     : CFBundleVersion
#      (la VERSIONE MARKETING X.Y NON viene toccata: si cambia con ExC/bump-version.sh)
#      NB: i campi sono gli stessi che usa la build iOS — il build number e'
#      condiviso fra le due piattaforme. Non e' un problema per App Store Connect,
#      che li tiene distinti per piattaforma, ma spiega perche' il numero "salta"
#      dopo un rilascio iOS.
#   2. Committa il bump (a meno di --no-commit).
#   3. Pulisce la cache Xcode (DerivedData) e la cartella del progetto generato:
#      il passo CRITICO — senza, Xcode riusa oggetti stantii e produce un binario
#      che passa la validazione ma non contiene i fix.
#   4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain macOS).
#   5. Archivia con `xcodebuild archive`.
#   6. Apre l'Organizer di Xcode per l'upload manuale su App Store Connect.
#
# NON fa l'upload: quello resta manuale in Xcode (nessuna API key richiesta).
#
# PERCHE' QUESTO E NON release_appstore.sh: qui firma, embedding dei framework e
# creazione del .pkg li fa Xcode durante Distribute App, con "Automatically
# manage signing". release_appstore.sh fa gli stessi passi a mano (macdeployqt +
# codesign + productbuild) ed e' molto piu' fragile: resta come alternativa se
# si vuole il .pkg senza passare da Xcode.
#
# PREREQUISITI (una volta sola):
#   - Certificati "Apple Distribution" e "Mac Installer Distribution" nel
#     portachiavi (creali su developer.apple.com -> Certificates).
#   - Su App Store Connect, la piattaforma macOS va ABILITATA sulla stessa app
#     (stesso bundle id dell'iOS): l'upload viene rifiutato se manca.
#
# Uso:
#   ./ExC/Mac/release_testflight_mac.sh                 bump +1, commit, clean, archivia, apri Organizer
#   ./ExC/Mac/release_testflight_mac.sh --no-commit     come sopra ma NON committa il bump
#   ./ExC/Mac/release_testflight_mac.sh --no-archive    solo bump + clean + rigenera Xcode, poi apre il progetto
#   ./ExC/Mac/release_testflight_mac.sh --help
#
# Richiede: macOS, Xcode + command line tools, qt-cmake macOS (Qt 6.12.0).

set -euo pipefail

# ---------------------------------------------------------------------------
# Configurazione
# ---------------------------------------------------------------------------
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"      # lo script vive in ExC/Mac/
CMAKE="$PROJECT_DIR/CMakeLists.txt"
# Il Mac ha un plist PROPRIO: Info.plist e' quello iOS (LSRequiresIPhoneOS,
# chiavi UIKit) e non e' valido per il Mac App Store. Bumpiamo entrambi, cosi'
# i due bundle non divergono mai sul build number.
PLIST="$PROJECT_DIR/Info-macos.plist"
PLIST_IOS="$PROJECT_DIR/Info.plist"
BUILD_DIR="$PROJECT_DIR/build/macos-appstore"
XCODEPROJ="$BUILD_DIR/SurfaceExplorer.xcodeproj"
SCHEME="SurfaceExplorer"
QT_CMAKE="$HOME/Qt/6.12.0/macos/bin/qt-cmake"
# Su macOS Qt e' DINAMICO: framework e plugin vanno incorporati nel bundle a
# mano, dopo l'archive. Xcode non lo fa (non sa nulla di Qt).
MACDEPLOYQT="$HOME/Qt/6.12.0/macos/bin/macdeployqt"
GIT_BRANCH="v1"
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
      sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) err "Argomento sconosciuto: $arg  (usa --help)" ;;
  esac
done

# ---------------------------------------------------------------------------
# Controlli preliminari
# ---------------------------------------------------------------------------
[ "$(uname)" = "Darwin" ] || err "Questo script gira solo su macOS."
[ -f "$CMAKE" ] || err "CMakeLists.txt non trovato in $PROJECT_DIR"
[ -f "$PLIST" ] || err "Info-macos.plist non trovato in $PROJECT_DIR"
[ -f "$PLIST_IOS" ] || err "Info.plist non trovato in $PROJECT_DIR"

# LSApplicationCategoryType e' obbligatoria sul Mac App Store: senza, l'archive
# riesce ma Distribute App fallisce a meta' con "The product archive is
# invalid" — dopo parecchi minuti di compilazione. Meglio fermarsi ORA.
grep -q "LSApplicationCategoryType" "$PLIST" \
  || err "Info-macos.plist non contiene LSApplicationCategoryType: la distribuzione fallirebbe."
command -v xcodebuild >/dev/null || err "xcodebuild non trovato (installa Xcode + command line tools)."
[ -x "$QT_CMAKE" ] || err "qt-cmake macOS non trovato/eseguibile: $QT_CMAKE"
[ -x "$MACDEPLOYQT" ] || err "macdeployqt non trovato/eseguibile: $MACDEPLOYQT"

# qt-cmake e' un wrapper che fa `exec cmake`: senza cmake nel PATH muore con
# "exec: cmake: not found" al passo 4, a bump gia' fatto e committato.
# Su questa macchina cmake NON e' nel PATH (arriva con Qt, non con Xcode):
# ce lo mettiamo noi, senza toccare l'ambiente dell'utente.
if ! command -v cmake >/dev/null; then
  QT_CMAKE_BIN="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin"
  [ -x "$QT_CMAKE_BIN/cmake" ] \
    || err "cmake non trovato: ne' nel PATH ne' in $QT_CMAKE_BIN (installalo da Qt Maintenance Tool)."
  export PATH="$QT_CMAKE_BIN:$PATH"
  info "cmake non era nel PATH: uso quello di Qt ($QT_CMAKE_BIN)."
fi
[ -f "$PROJECT_DIR/ExC/Mac/macos_appstore.entitlements" ] \
  || err "Entitlements sandbox non trovati: ExC/Mac/macos_appstore.entitlements"

# Certificato di distribuzione: senza, l'archive gira ma Distribute App fallisce
# a meta' strada. Meglio dirlo ORA (avviso, non errore: l'archive resta utile
# anche solo per verificare che la build stia in piedi).
if ! security find-identity -v -p codesigning 2>/dev/null | grep -q "Apple Distribution"; then
  info "ATTENZIONE: nessun certificato \"Apple Distribution\" nel portachiavi."
  info "  L'archive verra' creato, ma l'upload su App Store Connect fallira'."
  info "  Crealo su https://developer.apple.com/account/resources/certificates"
  info "  (servono \"Apple Distribution\" per l'app e \"Mac Installer Distribution\" per il .pkg)."
fi

cd "$PROJECT_DIR"

if [ "$DO_COMMIT" -eq 1 ] && ! git diff --quiet HEAD 2>/dev/null; then
  info "Working tree con modifiche non committate: verranno incluse nel commit del bump."
fi

# ---------------------------------------------------------------------------
# 1. Leggi e incrementa il build number
# ---------------------------------------------------------------------------
CUR_BUILD="$(sed -nE 's/.*MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
[ -n "$CUR_BUILD" ] || err "Build number non trovato in CMakeLists.txt (MACOSX_BUNDLE_BUNDLE_VERSION)."
NEW_BUILD=$(( CUR_BUILD + 1 ))
MARKETING="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' "$CMAKE" | head -1)"
MARKETING="${MARKETING:-?}"

info "Versione marketing: $MARKETING  |  Build number: $CUR_BUILD -> $NEW_BUILD"

# --- CMakeLists.txt: i campi build number (devono restare tutti uguali) ---
# NB: dopo l'aggiunta del ramo macOS i campi sono QUATTRO (due per iOS, due per
# macOS): la sed li aggiorna tutti perche' non e' ancorata a un ramo.
tmp="$(mktemp)"
sed -E \
  -e "s/(MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  -e "s/(XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"

# --- Plist: <string> sulla riga DOPO la chiave CFBundleVersion ---
# Entrambi i file: quello macOS (che verra' archiviato) e quello iOS, cosi' i due
# bundle non divergono e il prossimo rilascio iOS riparte dal numero giusto.
for p in "$PLIST" "$PLIST_IOS"; do
  tmp="$(mktemp)"
  awk -v v="$NEW_BUILD" '
    f && /<string>[0-9]+<\/string>/ { sub(/<string>[0-9]+<\/string>/, "<string>" v "</string>"); f=0 }
    /<key>CFBundleVersion<\/key>/ { f=1 }
    { print }
  ' "$p" > "$tmp" && mv "$tmp" "$p"
done

# --- Verifica coerenza: TUTTE le occorrenze, non solo la prima ---
BAD="$(grep -E 'MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"[0-9]+"|XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+"[0-9]+"' "$CMAKE" \
        | grep -vE "\"${NEW_BUILD}\"" || true)"
[ -z "$BAD" ] || err "Bump incoerente in CMakeLists.txt, righe rimaste indietro:\n$BAD"
for p in "$PLIST" "$PLIST_IOS"; do
  P1="$(awk '/<key>CFBundleVersion<\/key>/{getline; gsub(/[^0-9]/,""); print; exit}' "$p")"
  [ "$P1" = "$NEW_BUILD" ] || err "Bump incoerente: $(basename "$p") ha $P1, atteso $NEW_BUILD."
  # I plist devono restare validi (una copia-cartella li puo' corrompere).
  plutil -lint "$p" >/dev/null || err "$(basename "$p") non valido dopo il bump (plutil -lint fallito)."
done
ok "Build number aggiornato a $NEW_BUILD (CMakeLists + Info-macos.plist + Info.plist)."

# ---------------------------------------------------------------------------
# 2. Commit del bump
# ---------------------------------------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  git add "$CMAKE" "$PLIST" "$PLIST_IOS"
  git commit -m "macOS: build number -> $NEW_BUILD (TestFlight)" >/dev/null
  ok "Commit del bump creato."
  info "Ricorda di fare 'git push origin $GIT_BRANCH' quando vuoi."
else
  info "--no-commit: bump applicato ai file ma NON committato."
fi

# ---------------------------------------------------------------------------
# 3. Pulizia cache Xcode + progetto generato (passo critico)
# ---------------------------------------------------------------------------
info "Pulizia DerivedData (SurfaceExplorer-*) e $BUILD_DIR ..."
find "$HOME/Library/Developer/Xcode/DerivedData" -maxdepth 1 -name 'SurfaceExplorer-*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$BUILD_DIR"
ok "Cache e progetto puliti."

# ---------------------------------------------------------------------------
# 4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain macOS)
# ---------------------------------------------------------------------------
info "Rigenerazione progetto Xcode macOS con qt-cmake ..."
"$QT_CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Xcode >/dev/null
[ -d "$XCODEPROJ" ] || err "Rigenerazione fallita: $XCODEPROJ non creato."
ok "Progetto Xcode rigenerato."

info "Valori nel progetto generato:"
grep -h "MARKETING_VERSION\|CURRENT_PROJECT_VERSION\|PRODUCT_BUNDLE_IDENTIFIER\|CODE_SIGN_ENTITLEMENTS" \
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
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
ARCHIVE_PATH="$ARCHIVE_DIR/$(date +%Y-%m-%d)/SurfaceExplorer_mac_${MARKETING}_${NEW_BUILD}_${STAMP}.xcarchive"
mkdir -p "$(dirname "$ARCHIVE_PATH")"

info "Archiviazione (xcodebuild archive) — può richiedere PARECCHI minuti."
info "NON interrompere anche se sembra ferma."
# Archive SENZA FIRMA, di proposito.
#
# Con CODE_SIGN_STYLE=Automatic, xcodebuild pretende un certificato "Mac
# Development" valido per il team — che e' cosa diversa da "Apple Development"
# per iOS, e diversa ancora dai certificati di DISTRIBUZIONE. L'archive
# fallirebbe prima ancora di compilare, con:
#   No signing certificate "Mac Development" found
#
# Firmare qui sarebbe comunque inutile: subito dopo macdeployqt riscrive il
# bundle e INVALIDA la firma, e il passo 5-bis rifirma tutto da capo con
# "Apple Distribution". Quindi saltiamo del tutto la firma in questa fase e
# lasciamo che sia la rifirma a fare l'unico lavoro che conta.
#
# Log COMPLETO su file: `| tail -40` da solo buttava via proprio le righe utili.
# Gli errori di xcodebuild compaiono a meta' log, mentre le ultime 40 righe
# contengono solo "** ARCHIVE FAILED **" e l'elenco dei comandi falliti: il
# messaggio che dice COSA fare non si vedeva mai. Stesso difetto (e stessa cura)
# dello script iOS.
XC_LOG="$(mktemp -t surfexp-archive-mac)"
set +e
xcodebuild archive \
  -project "$XCODEPROJ" \
  -scheme "$SCHEME" \
  -configuration Release \
  -destination 'generic/platform=macOS' \
  -archivePath "$ARCHIVE_PATH" \
  CODE_SIGN_IDENTITY="" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  > "$XC_LOG" 2>&1
XC_STATUS=$?
set -e

if [ "$XC_STATUS" -ne 0 ]; then
  printf '\033[31m--- errori riportati da xcodebuild ---\033[0m\n'
  if grep -qE "error:" "$XC_LOG"; then
    grep -E "error:" "$XC_LOG" | sort -u | sed 's/^/  /'
  else
    tail -40 "$XC_LOG" | sed 's/^/  /'
  fi
  printf '\033[31m--------------------------------------\033[0m\n'
  info "Log completo: $XC_LOG"
  err "xcodebuild archive fallito (exit $XC_STATUS)."
fi
rm -f "$XC_LOG"
[ -d "$ARCHIVE_PATH" ] || err "Archivio non prodotto: $ARCHIVE_PATH"
ok "Archivio creato: $ARCHIVE_PATH"

# ---------------------------------------------------------------------------
# 5-bis. macdeployqt sull'app DENTRO l'archivio (PASSO OBBLIGATORIO)
# ---------------------------------------------------------------------------
# Xcode non sa nulla di Qt: archivia il binario cosi' com'e', con
# Contents/Frameworks e Contents/PlugIns VUOTE e un rpath che punta a
# ~/Qt/6.12.0/macos/lib. Sulla macchina di sviluppo i framework si trovano lo
# stesso e sembra tutto a posto, ma i PLUGIN no: manca il plugin di piattaforma
# "cocoa", QGuiApplicationPrivate::createPlatformIntegration() fallisce e Qt
# chiama qFatal -> l'app ABORTISCE all'avvio (SIGABRT). Su un altro Mac non
# partirebbe affatto.
#
# Su iOS il problema non esiste perche' li' Qt e' statico; su macOS i framework
# sono dinamici e vanno incorporati a mano, qui.
APP_IN_ARCHIVE="$ARCHIVE_PATH/Products/Applications/$SCHEME.app"
[ -d "$APP_IN_ARCHIVE" ] || err "App non trovata nell'archivio: $APP_IN_ARCHIVE"

info "macdeployqt: incorporo framework e plugin Qt nell'app archiviata ..."
"$MACDEPLOYQT" "$APP_IN_ARCHIVE" -verbose=1 >/dev/null 2>&1 \
  || err "macdeployqt fallito su $APP_IN_ARCHIVE"

# Verifica che abbia davvero fatto il lavoro: senza il plugin di piattaforma
# l'app crasha all'avvio, ed e' un errore che si scopre solo DOPO l'upload e
# l'installazione da TestFlight — cioe' nel modo piu' lento possibile.
[ -d "$APP_IN_ARCHIVE/Contents/Frameworks/QtCore.framework" ] \
  || err "macdeployqt non ha incorporato i framework Qt (Contents/Frameworks incompleta)."
[ -f "$APP_IN_ARCHIVE/Contents/PlugIns/platforms/libqcocoa.dylib" ] \
  || err "manca il plugin di piattaforma cocoa: l'app abortirebbe all'avvio."
ok "Framework e plugin Qt incorporati."

# Rifirma: macdeployqt ha modificato il bundle e invalidato la firma di Xcode.
# Firma dall'interno verso l'esterno; gli entitlements vanno solo sull'app.
SIGN_ID="$(security find-identity -v -p codesigning 2>/dev/null \
            | grep '"Apple Distribution' | head -1 | sed -E 's/.*"(.*)"$/\1/')"
if [ -n "$SIGN_ID" ]; then
  info "Rifirma dopo macdeployqt ($SIGN_ID) ..."

  # NIENTE unlock-keychain qui: senza -p diventa INTERATTIVO e si pianta ad
  # aspettare la password sul terminale, con l'input nascosto — sembra un
  # blocco dello script. Il portachiavi "login" e' comunque gia' sbloccato
  # dall'accesso all'utente, quindi non serve.
  #
  # Al primo popup del portachiavi rispondere "Consenti sempre" (Always Allow):
  # con "Consenti" l'autorizzazione vale per UNA firma sola. Per non vederlo
  # affatto, una volta per tutte:
  #   security set-key-partition-list -S apple-tool:,apple:,codesign: \
  #     -k <password-login> ~/Library/Keychains/login.keychain-db

  # --deep firma l'intero albero (framework, plugin, helper) in UNA passata,
  # invece di un codesign per file: molto piu' rapido e, soprattutto, un solo
  # accesso alla chiave. Gli entitlements valgono per l'eseguibile principale.
  codesign --force --deep --timestamp --options runtime \
           --entitlements "$PROJECT_DIR/ExC/Mac/macos_appstore.entitlements" \
           --sign "$SIGN_ID" "$APP_IN_ARCHIVE" \
    || err "Rifirma dell'app fallita."
  codesign --verify --deep --strict "$APP_IN_ARCHIVE" \
    || err "La firma non supera la verifica dopo macdeployqt."
  ok "Bundle rifirmato e verificato."
else
  # L'archive e' stato creato SENZA firma di proposito (vedi sopra): se non
  # possiamo rifirmare qui, il bundle resta non firmato e Distribute App
  # fallisce. Meglio dirlo forte adesso che lasciar credere sia tutto pronto.
  info "ATTENZIONE: nessun certificato \"Apple Distribution\": bundle NON firmato."
  info "  L'archivio esiste ed e' ispezionabile, ma NON e' distribuibile."
  info "  Crea i due certificati e rilancia lo script:"
  info "    https://developer.apple.com/account/resources/certificates"
  info "      - Apple Distribution          (firma l'app)"
  info "      - Mac Installer Distribution  (firma il .pkg)"
fi

# Verifica anti-stantìo + controllo che la sandbox sia davvero attiva: se manca,
# l'App Store rifiuta il pacchetto, ed e' molto meglio accorgersene qui.
BIN="$ARCHIVE_PATH/Products/Applications/SurfaceExplorer.app"
if [ -d "$BIN" ]; then
  info "Bundle archiviato: $(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$BIN")"
  if codesign -d --entitlements - --xml "$BIN" 2>/dev/null | grep -q "app-sandbox"; then
    ok "Sandbox attiva sul bundle archiviato."
  else
    info "ATTENZIONE: sandbox NON rilevata sul bundle. L'App Store rifiutera' il pacchetto."
    info "  Verifica XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS nel ramo macOS del CMakeLists."
  fi
fi

# ---------------------------------------------------------------------------
# 6. Apri l'Organizer per l'upload manuale
# ---------------------------------------------------------------------------
info "Apro l'Organizer di Xcode. Da lì:"
cat <<'STEPS'
    1. Seleziona l'archivio appena creato (in alto la Version deve mostrare il nuovo build).
    2. Distribute App -> App Store Connect -> Upload -> Next.
    3. Automatically manage signing -> Next -> Upload.
       (Xcode firma, incorpora i framework Qt e costruisce il .pkg da solo.)
    4. Poi: attendi l'elaborazione, gestisci Export Compliance ("Nessuno degli algoritmi"),
       assegna la build al gruppo "Internal Testers".
    5. I tester su Mac installano dall'app TestFlight PER MAC (va scaricata a parte
       dal Mac App Store): a differenza di iOS non e' gia' presente sul sistema.
STEPS
open -a Xcode "$ARCHIVE_PATH"
ok "Build macOS $MARKETING ($NEW_BUILD) pronta per l'upload su TestFlight."
