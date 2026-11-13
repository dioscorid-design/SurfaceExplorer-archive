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
#        (Info.plist e Info-macos.plist lo leggono da li': CFBundleVersion vale
#         ${MACOSX_BUNDLE_BUNDLE_VERSION}; lo script verifica che resti cosi')
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
#   5. Archivia con `xcodebuild archive`, FIRMATO come l'iOS (firma automatica,
#      certificato "Apple Development" del team): l'archivio deve dire a quale
#      team appartiene, o Distribute App si ferma con "team non trovato".
#   5-bis. macdeployqt dentro l'archivio, poi rifirma con la stessa identita' e
#      gli stessi entitlements che Xcode aveva dato all'app.
#   6. Apre l'Organizer di Xcode per l'upload manuale su App Store Connect.
#      Li' Xcode rifirma per la distribuzione (Automatically manage signing).
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
#   - Un certificato "Apple Development" del team nel portachiavi e l'account
#     del team in Xcode (gli stessi che firmano la build iOS).
#   - Per la distribuzione Xcode usa i certificati del team: se mancano in locale
#     prova quelli gestiti nel cloud. Se l'Organizer chiede "Apple Distribution"
#     o "Mac Installer Distribution", vanno creati su developer.apple.com.
#   - Su App Store Connect, la piattaforma macOS va ABILITATA sulla stessa app
#     (stesso bundle id dell'iOS): l'upload viene rifiutato se manca.
#
# Uso:
#   ./ExC/Mac/release_testflight_mac.sh                 bump +1, commit, clean, archivia, apri Organizer
#   ./ExC/Mac/release_testflight_mac.sh --no-commit     come sopra ma NON committa il bump
#   ./ExC/Mac/release_testflight_mac.sh --no-bump       riarchivia il build number attuale (nessun bump,
#                                                       nessun commit): per rifare un archivio mai caricato
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
ARCHIVE_DIR="$HOME/Library/Developer/Xcode/Archives"

err()  { printf '\033[31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[32m✓\033[0m %s\n' "$*"; }

# Il comando che pusha il ramo corrente sul suo remoto, per il promemoria dopo il
# commit del bump. Letto da git: prima era scritto a mano ("git push origin v1") e
# indicava un ramo che questo repo non ha. Coi nomi completi (refs/heads/...):
# dove un tag ha lo stesso nome del ramo (v1.5) "git push <remoto> v1.5" e' ambiguo.
# Stessa funzione in release_testflight.sh.
push_hint() {
  local ref branch remote merge
  # Il ref intero, non --short: col tag omonimo --short risponde "heads/v1.5",
  # che non e' il nome sotto cui git tiene il remoto del ramo.
  ref="$(git symbolic-ref --quiet HEAD 2>/dev/null || true)"
  branch="${ref#refs/heads/}"
  remote="$(git config --get "branch.$branch.remote" 2>/dev/null || true)"
  merge="$(git config --get "branch.$branch.merge" 2>/dev/null || true)"
  if [ -n "$branch" ] && [ -n "$remote" ] && [ -n "$merge" ]; then
    printf 'git push %s %s:%s' "$remote" "$ref" "$merge"
  else
    printf 'git push'
  fi
}

# ---------------------------------------------------------------------------
# Argomenti
# ---------------------------------------------------------------------------
DO_COMMIT=1
DO_ARCHIVE=1
DO_BUMP=1
for arg in "$@"; do
  case "$arg" in
    --no-commit)  DO_COMMIT=0 ;;
    --no-archive) DO_ARCHIVE=0 ;;
    --no-bump)    DO_BUMP=0; DO_COMMIT=0 ;;
    --help|-h)
      sed -n '2,/^# Richiede/p' "$0" | sed 's/^# \{0,1\}//'
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

# Certificato con cui xcodebuild firma l'archivio: senza, l'archive fallisce
# dopo tutta la compilazione. Meglio fermarsi ORA.
security find-identity -v -p codesigning 2>/dev/null | grep -q '"Apple Development' \
  || err "Nessun certificato \"Apple Development\" nel portachiavi: l'archivio non si puo' firmare (Xcode -> Settings -> Accounts -> Manage Certificates)."

cd "$PROJECT_DIR"

if [ "$DO_COMMIT" -eq 1 ] && ! git diff --quiet HEAD 2>/dev/null; then
  info "Working tree con modifiche non committate: verranno incluse nel commit del bump."
fi

# ---------------------------------------------------------------------------
# 1. Leggi e incrementa il build number
# ---------------------------------------------------------------------------
CUR_BUILD="$(sed -nE 's/.*MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
[ -n "$CUR_BUILD" ] || err "Build number non trovato in CMakeLists.txt (MACOSX_BUNDLE_BUNDLE_VERSION)."
# --no-bump: si riarchivia il numero attuale (un archivio che non e' mai
# arrivato su App Store Connect non ha consumato il suo numero).
if [ "$DO_BUMP" -eq 1 ]; then NEW_BUILD=$(( CUR_BUILD + 1 )); else NEW_BUILD="$CUR_BUILD"; fi
MARKETING="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' "$CMAKE" | head -1)"
MARKETING="${MARKETING:-?}"

if [ "$DO_BUMP" -eq 1 ]; then
  info "Versione marketing: $MARKETING  |  Build number: $CUR_BUILD -> $NEW_BUILD"
else
  info "Versione marketing: $MARKETING  |  Build number: $NEW_BUILD (--no-bump: invariato)"
fi

# --- CMakeLists.txt: i campi build number (devono restare tutti uguali) ---
# NB: dopo l'aggiunta del ramo macOS i campi sono QUATTRO (due per iOS, due per
# macOS): la sed li aggiorna tutti perche' non e' ancorata a un ramo.
if [ "$DO_BUMP" -eq 1 ]; then
  tmp="$(mktemp)"
  sed -E \
    -e "s/(MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
    -e "s/(XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
    "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"
fi

# --- Plist: nessun numero da scrivere ---
# Dal 2026-11-06 CFBundleVersion vale ${MACOSX_BUNDLE_BUNDLE_VERSION} in entrambi i
# file (macOS e iOS), che CMake riempie dai campi qui sopra (anche col generatore
# Xcode): i due bundle non possono divergere. Sotto si verifica che restino cosi'.

# --- Verifica coerenza: TUTTE le occorrenze, non solo la prima ---
BAD="$(grep -E 'MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"[0-9]+"|XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+"[0-9]+"' "$CMAKE" \
        | grep -vE "\"${NEW_BUILD}\"" || true)"
[ -z "$BAD" ] || err "Bump incoerente in CMakeLists.txt, righe rimaste indietro:\n$BAD"
for p in "$PLIST" "$PLIST_IOS"; do
  grep -A1 '<key>CFBundleVersion</key>' "$p" | grep -qF '<string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>' \
    || err "$(basename "$p"): CFBundleVersion deve valere \${MACOSX_BUNDLE_BUNDLE_VERSION}, non un numero."
  # I plist devono restare validi (una copia-cartella li puo' corrompere).
  plutil -lint "$p" >/dev/null || err "$(basename "$p") non valido (plutil -lint fallito)."
done
ok "Build number $NEW_BUILD (CMakeLists; Info-macos.plist e Info.plist lo leggono da li')."

# ---------------------------------------------------------------------------
# 2. Commit del bump
# ---------------------------------------------------------------------------
if [ "$DO_BUMP" -eq 0 ]; then
  :   # niente da committare
elif [ "$DO_COMMIT" -eq 1 ]; then
  git add "$CMAKE" "$PLIST" "$PLIST_IOS"
  git commit -m "macOS: build number -> $NEW_BUILD (TestFlight)" >/dev/null
  ok "Commit del bump creato."
  info "Ricorda di fare '$(push_hint)' quando vuoi."
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
# Archive FIRMATO, come quello iOS: firma automatica col certificato "Apple
# Development" del team (vale per tutte le piattaforme) e -allowProvisioningUpdates
# perche' Xcode crei o aggiorni da se' il profilo macOS.
# Fino al 2026-11-13 l'archivio usciva SENZA firma (CODE_SIGNING_ALLOWED=NO) e la
# firma arrivava solo dalla rifirma con "Apple Distribution": senza quel
# certificato nel portachiavi l'app restava firmata ad-hoc e l'Info.plist
# dell'archivio aveva Team vuoto, e Distribute App si fermava con "team non
# trovato nell'archivio". Il team l'Organizer lo legge da li', e lo scrive
# soltanto xcodebuild quando firma.
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
  CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates \
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
  if grep -q "No Account for Team\|No profiles for\|No signing certificate" "$XC_LOG"; then
    info "Sembra un problema di FIRMA. Team richiesto dal progetto:"
    grep -h "DEVELOPMENT_TEAM" "$XCODEPROJ/project.pbxproj" | sort -u | sed 's/^/    /'
    info "Controlla in Xcode -> Settings -> Accounts che l'account del team sia collegato."
  fi
  err "xcodebuild archive fallito (exit $XC_STATUS)."
fi
rm -f "$XC_LOG"
[ -d "$ARCHIVE_PATH" ] || err "Archivio non prodotto: $ARCHIVE_PATH"
ok "Archivio creato: $ARCHIVE_PATH"

# Il team nell'archivio: e' cio' che l'Organizer cerca per distribuire.
TEAM="$(plutil -extract ApplicationProperties.Team raw "$ARCHIVE_PATH/Info.plist" 2>/dev/null || true)"
[ -n "$TEAM" ] || err "L'archivio non dice il suo team (ApplicationProperties.Team vuoto): xcodebuild non l'ha firmato."
ok "Archivio firmato per il team $TEAM."

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

# PRIMA di macdeployqt, che riscrive il bundle e lascia una firma ad-hoc: la
# firma che Xcode ha dato all'app. Si rifirma dopo con la STESSA identita' (per
# impronta SHA-1: due certificati "Apple Development" con lo stesso nome
# renderebbero ambiguo il nome) e gli STESSI entitlements (sandbox, team e
# application-identifier del profilo incluso nell'app).
SIGN_DIR="$(mktemp -d -t surfexp-sign)"
codesign -d --extract-certificates="$SIGN_DIR/cert" "$APP_IN_ARCHIVE" >/dev/null 2>&1 || true
[ -f "$SIGN_DIR/cert0" ] || err "L'app archiviata non ha una firma con certificato: niente da rifirmare."
SIGN_HASH="$(shasum -a 1 "$SIGN_DIR/cert0" | awk '{print toupper($1)}')"
SIGN_NAME="$(security find-identity -v -p codesigning 2>/dev/null | grep "$SIGN_HASH" | sed -E 's/.*"(.*)"$/\1/' | head -1)"
[ -n "$SIGN_NAME" ] || err "Il certificato che ha firmato l'archivio ($SIGN_HASH) non e' fra le identita' del portachiavi."
codesign -d --entitlements - --xml "$APP_IN_ARCHIVE" > "$SIGN_DIR/entitlements.plist" 2>/dev/null || true
grep -q "app-sandbox" "$SIGN_DIR/entitlements.plist" \
  || err "La firma di Xcode non porta la sandbox: controlla XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS nel ramo macOS del CMakeLists."
RUNTIME_OPT=()
codesign -dv "$APP_IN_ARCHIVE" 2>&1 | grep -q 'flags=.*runtime' && RUNTIME_OPT=(--options runtime)

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
# E' una firma di sviluppo, come quella che Xcode aveva messo: in Distribute App
# l'Organizer rifirma tutto per la distribuzione.
info "Rifirma dopo macdeployqt ($SIGN_NAME) ..."

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

# Due passate, due soli accessi alla chiave: --deep firma tutto l'albero
# (framework, plugin) SENZA entitlements, poi l'app sola con i suoi. Con
# --entitlements e --deep insieme la sandbox finirebbe anche su framework e plugin.
# ${A[@]+"${A[@]}"}: col bash 3.2 di macOS e set -u un array vuoto scritto
# "${A[@]}" ferma lo script ("unbound variable").
codesign --force --deep ${RUNTIME_OPT[@]+"${RUNTIME_OPT[@]}"} --sign "$SIGN_HASH" "$APP_IN_ARCHIVE" \
  || err "Rifirma di framework e plugin fallita."
codesign --force ${RUNTIME_OPT[@]+"${RUNTIME_OPT[@]}"} --entitlements "$SIGN_DIR/entitlements.plist" \
         --sign "$SIGN_HASH" "$APP_IN_ARCHIVE" \
  || err "Rifirma dell'app fallita."
codesign --verify --deep --strict "$APP_IN_ARCHIVE" \
  || err "La firma non supera la verifica dopo macdeployqt."
SIGNED_TEAM="$(codesign -dv "$APP_IN_ARCHIVE" 2>&1 | sed -n 's/^TeamIdentifier=//p')"
[ "$SIGNED_TEAM" = "$TEAM" ] \
  || err "Dopo la rifirma l'app ha il team '$SIGNED_TEAM', l'archivio dice '$TEAM'."
rm -rf "$SIGN_DIR"
ok "Bundle rifirmato e verificato (team $SIGNED_TEAM)."

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
