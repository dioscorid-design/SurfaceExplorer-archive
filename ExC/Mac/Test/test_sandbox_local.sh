#!/usr/bin/env bash
#
# test_sandbox_local.sh - Compila e lancia l'app SANDBOXED in locale, senza TestFlight.
#
# A COSA SERVE: i bug che si vedono solo sotto sandbox (libreria vuota dal
# secondo avvio, export video "Cannot Save", qualunque accesso a un percorso
# scelto dall'utente) NON si riproducono con la build di sviluppo, che gira
# senza sandbox. Finora l'unico modo di provarli era caricare su TestFlight:
# un giro da parecchi minuti, un build number bruciato ogni volta, e l'attesa
# del processing prima di poter anche solo aprire l'app.
#
# La sandbox pero' NON ha niente a che vedere con TestFlight: si attiva
# firmando il binario con l'entitlement com.apple.security.app-sandbox. Basta
# firmare in locale con GLI STESSI entitlements dell'App Store e l'app gira
# esattamente com'e' su TestFlight, sulla tua macchina, in una manciata di
# secondi.
#
# DIFFERENZE dalla build TestFlight (nessuna rilevante per questi bug):
#   - firma con certificato di SVILUPPO (o ad-hoc) invece di Apple Distribution:
#     cambia chi garantisce il binario, non i permessi che ottiene;
#   - niente .pkg e niente Distribute App: non ci interessa distribuire.
# Il container sandbox e' lo stesso (~/Library/Containers/<bundle id>), quindi
# anche il comportamento di QSettings e dei percorsi salvati e' identico.
#
# Uso:
#   ./ExC/Mac/Test/test_sandbox_local.sh              compila (se serve), firma, lancia
#   ./ExC/Mac/Test/test_sandbox_local.sh --reset      AZZERA il container e poi lancia
#                                                (= simula la PRIMA installazione)
#   ./ExC/Mac/Test/test_sandbox_local.sh --no-run     compila e firma soltanto
#   ./ExC/Mac/Test/test_sandbox_local.sh --logs       lancia mostrando i log in console
#   ./ExC/Mac/Test/test_sandbox_local.sh --help
#
# IL CICLO DI PROVA per "funziona al primo avvio, non al secondo":
#   1. ./ExC/Mac/Test/test_sandbox_local.sh --reset    -> prima apertura: scegli la
#      cartella dei preset, verifica che la libreria si popoli e che l'export vada
#   2. chiudi l'app
#   3. ./ExC/Mac/Test/test_sandbox_local.sh            -> SECONDA apertura: la libreria
#      deve essere ancora piena e l'export deve funzionare ancora
#   Il passo 3 e' esattamente il caso che falliva su TestFlight.
#
# Richiede: macOS, Xcode command line tools, Qt 6.12.0 macOS.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/macos-sandbox-test"
ENTITLEMENTS="$PROJECT_DIR/ExC/Mac/macos_appstore.entitlements"
QT_CMAKE="$HOME/Qt/6.12.0/macos/bin/qt-cmake"
MACDEPLOYQT="$HOME/Qt/6.12.0/macos/bin/macdeployqt"
BUNDLE_ID="com.dioscorid.surfaceexplorer"
# ATTENZIONE: il container NON si chiama sempre come il bundle id. macOS puo'
# assegnargli un nome UUID (e' il caso di questa app), e il suo metadata non e'
# leggibile. Cercarlo per nome falliva in SILENZIO: --reset "funzionava" senza
# cancellare nulla, e ogni prova del PRIMO avvio era in realta' un avvio
# successivo — con il rischio di credere verificato un fix mai provato davvero.
# L'unico modo affidabile e' individuarlo dal file di preferenze dell'app, che
# porta il bundle id nel nome.
find_container() {
  local p
  p="$(ls -td "$HOME/Library/Containers"/*/Data/Library/Preferences/"$BUNDLE_ID".plist 2>/dev/null | head -1)"
  [ -n "$p" ] && echo "${p%%/Data/Library/Preferences/*}"
  # `return 0` OBBLIGATORIO: senza, quando il container NON esiste il test
  # precedente e' l'ultimo comando della funzione e ne diventa lo stato di
  # uscita (1). Con `set -e` questo abortiva l'INTERO script dentro la
  # sostituzione di comando `CONTAINER="$(find_container)"`, e lo faceva in
  # SILENZIO: exit 1, nessun messaggio, nemmeno una riga di log. Si presenta
  # solo a container assente -- cioe' dopo un --reset o su una macchina
  # pulita: esattamente il caso della PRIMA installazione che questo script
  # serve a provare.
  return 0
}
CONTAINER="$(find_container)"
CONTAINER="${CONTAINER:-$HOME/Library/Containers/$BUNDLE_ID}"
APP="$BUILD_DIR/SurfaceExplorer.app"

err()  { printf '\033[31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '\033[33m!\033[0m %s\n' "$*"; }

DO_RESET=0
DO_RUN=1
SHOW_LOGS=0
for arg in "$@"; do
  case "$arg" in
    --reset)   DO_RESET=1 ;;
    --no-run)  DO_RUN=0 ;;
    --logs)    SHOW_LOGS=1 ;;
    --help|-h) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) err "Argomento sconosciuto: $arg  (usa --help)" ;;
  esac
done

[ "$(uname)" = "Darwin" ] || err "Questo script gira solo su macOS."
[ -f "$ENTITLEMENTS" ] || err "Entitlements non trovati: $ENTITLEMENTS"
[ -x "$QT_CMAKE" ] || err "qt-cmake macOS non trovato: $QT_CMAKE"

# qt-cmake fa `exec cmake`: su questa macchina cmake arriva con Qt, non con
# Xcode, e non e' nel PATH. Stesso accorgimento di release_testflight_mac.sh.
if ! command -v cmake >/dev/null; then
  QT_CMAKE_BIN="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin"
  [ -x "$QT_CMAKE_BIN/cmake" ] || err "cmake non trovato ne' nel PATH ne' in $QT_CMAKE_BIN"
  export PATH="$QT_CMAKE_BIN:$PATH"
fi

cd "$PROJECT_DIR"

# ---------------------------------------------------------------------------
# 1. Compilazione (Debug: la Release la gestisce l'utente)
# ---------------------------------------------------------------------------
# -DSE_APPSTORE_ID=ON: questa build simula il canale APP STORE, quindi deve
# avere il suo bundle id. Senza, CMakeLists le darebbe quello del DMG
# ("...surfaceexplorer.dmg"), perche' il discriminante normale e' il generatore
# e qui non si usa Xcode. Conseguenze MISURATE: --reset azzerava container e
# dominio dell'App Store mentre l'app leggeva e scriveva quelli del DMG (il
# reset "non funzionava" e la libreria si riapriva sul percorso precedente), e
# i due bundle con lo stesso id -- questo e quello in /Applications -- si
# contendevano l'attivazione, da cui l'icona nel Dock sostituita da un'altra.
#
# Si riconfigura anche a cache ESISTENTE se l'id memorizzato e' quello del DMG:
# una cache creata prima di questa correzione continuerebbe a produrre l'id
# sbagliato per sempre, e il sintomo tornerebbe senza spiegazione.
NEEDS_CONFIG=0
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  NEEDS_CONFIG=1
elif ! grep -q "^SE_APPSTORE_ID:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
  info "Cache senza SE_APPSTORE_ID (bundle id del DMG): riconfiguro."
  NEEDS_CONFIG=1
fi

if [ "$NEEDS_CONFIG" -eq 1 ]; then
  info "Configurazione in $BUILD_DIR ..."
  "$QT_CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
              -DSE_APPSTORE_ID=ON >/dev/null
fi

info "Compilazione ..."
cmake --build "$BUILD_DIR" --parallel 2>&1 | grep -E "error|Error" && err "Compilazione fallita." || true
[ -d "$APP" ] || err "Bundle non prodotto: $APP"
ok "Compilato."

# L'ID DEVE ESSERE QUELLO DELL'APP STORE, o tutto il test e' falsato: il
# container e le preferenze che --reset azzera sono quelli di $BUNDLE_ID, e con
# un id diverso l'app ne userebbe altri -- il reset non avrebbe effetto visibile
# e la libreria si riaprirebbe sul percorso precedente. E' successo, quindi si
# controlla invece di darlo per scontato.
BUILT_ID="$(plutil -extract CFBundleIdentifier raw "$APP/Contents/Info.plist" 2>/dev/null || echo '')"
[ "$BUILT_ID" = "$BUNDLE_ID" ] || err \
  "Bundle id sbagliato: '$BUILT_ID' invece di '$BUNDLE_ID'. La cache in $BUILD_DIR e' stantia: cancellala e rilancia."

# ---------------------------------------------------------------------------
# 2. Framework Qt dentro il bundle
# ---------------------------------------------------------------------------
# Serve perche' la firma con sandbox rende il bundle sigillato: un'app che carica
# i framework da ~/Qt (fuori dal bundle, Team ID diverso) verrebbe uccisa
# all'avvio dalla library validation. E' lo stesso motivo per cui la build
# App Store incorpora Qt con macdeployqt.
#
# ATTENZIONE — la condizione giusta e' l'RPATH DEL BINARIO, non la presenza
# della cartella Frameworks. Ogni ricompilazione RIGENERA il binario con
# l'rpath originale (~/Qt), mentre i framework copiati da macdeployqt restano
# li' dal giro precedente: controllando solo la cartella, il passo veniva
# saltato e si otteneva un bundle incoerente (framework dentro, binario che li
# cerca fuori). Il risultato e' un crash SIGABRT all'avvio con
# "Library not loaded ... different Team IDs", che sembra un crash dell'app ma
# e' un difetto di confezionamento.
if ! otool -l "$APP/Contents/MacOS/SurfaceExplorer" 2>/dev/null \
     | grep -q "@executable_path/../Frameworks"; then
  [ -x "$MACDEPLOYQT" ] || err "macdeployqt non trovato: $MACDEPLOYQT"

  # macdeployqt si rifiuta di ricablare un bundle che ha GIA' la cartella
  # Frameworks (la considera gia' deployata) e termina senza fare nulla: e'
  # proprio il caso che si presenta dopo una ricompilazione, quando il binario
  # e' nuovo ma i framework sono quelli del giro prima. Si toglie la cartella
  # cosi' riparte da zero, invece di lasciare un bundle incoerente.
  if [ -d "$APP/Contents/Frameworks" ]; then
    info "Bundle gia' deployato ma binario ricompilato: rifaccio il deploy."
    rm -rf "$APP/Contents/Frameworks" "$APP/Contents/PlugIns"
  fi

  info "Incorporo i framework Qt e ricablo l'rpath (~1 min) ..."
  "$MACDEPLOYQT" "$APP" >/dev/null 2>&1 || warn "macdeployqt ha segnalato warning (di norma innocui)."

  # macdeployqt aggiunge l'rpath giusto ma NON sempre rimuove quello vecchio:
  # se ~/Qt resta in lista viene provato per primo e la library validation
  # uccide comunque il processo. Va tolto esplicitamente.
  install_name_tool -delete_rpath "$HOME/Qt/6.12.0/macos/lib" \
                    "$APP/Contents/MacOS/SurfaceExplorer" 2>/dev/null || true

  otool -l "$APP/Contents/MacOS/SurfaceExplorer" 2>/dev/null \
    | grep -q "@executable_path/../Frameworks" \
    || err "macdeployqt non ha ricablato l'rpath: il bundle crasherebbe all'avvio."
  ok "Framework Qt incorporati e rpath ricablato."
fi

# ---------------------------------------------------------------------------
# 3. Firma con gli entitlements dell'App Store  <-- IL PASSO CHE ATTIVA LA SANDBOX
# ---------------------------------------------------------------------------
# Identita': si preferisce un certificato di sviluppo vero; in mancanza si firma
# ad-hoc ("-"), che per la sandbox va benissimo. Cio' che conta e' --entitlements.
IDENTITY="-"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "Apple Development"; then
  IDENTITY="$(security find-identity -v -p codesigning | grep "Apple Development" | head -1 | sed -E 's/.*"(.*)"/\1/')"
  info "Firma con: $IDENTITY"
else
  info "Nessun certificato Apple Development: firma ad-hoc (va bene per la sandbox)."
fi

# --deep e' deprecato ma resta il modo piu' semplice di firmare anche i framework
# incorporati; per una build di PROVA (mai distribuita) e' adeguato.
codesign --force --deep --sign "$IDENTITY" \
         --entitlements "$ENTITLEMENTS" \
         --options runtime \
         "$APP" 2>/dev/null || err "codesign fallito."

# Verifica che la sandbox sia DAVVERO attiva: senza questo controllo si rischia
# di "provare la sandbox" su un binario che gira libero, e concludere che il bug
# e' risolto quando invece non e' mai stato riprodotto.
if codesign -d --entitlements - "$APP" 2>/dev/null | grep -q "app-sandbox"; then
  ok "Sandbox ATTIVA sul bundle firmato."
else
  err "L'entitlement app-sandbox non risulta sul bundle: la prova non sarebbe valida."
fi
if codesign -d --entitlements - "$APP" 2>/dev/null | grep -q "files.bookmarks.app-scope"; then
  ok "Entitlement bookmarks.app-scope presente."
else
  info "bookmarks.app-scope non presente (serve solo se si usano i security-scoped bookmark)."
fi

# Il bundle si avvia davvero? Un errore di CONFEZIONAMENTO (librerie non
# risolvibili) si manifesta come SIGABRT immediato e sembra un crash dell'APP:
# meglio scoprirlo qui, con il messaggio del linker in chiaro, che davanti a un
# report di crash da interpretare.
#
# ATTENZIONE — questo controllo NON deve far partire l'applicazione. L'app non
# interpreta alcun argomento (main.cpp non guarda argv): passarle `--version`
# non la faceva uscire, la AVVIAVA, e restava viva in `app.exec()`. Con lo
# stdout rediretto la sua finestra passava inosservata dietro a quella lanciata
# subito dopo da `open`, cosi' chiudendo l'app "se ne riapriva una seconda":
# in realta' era la PRIMA, mai chiusa. (Il sospetto era caduto su `open -n`,
# vedi il commento al passo 5: quello e' un problema diverso e reale, ma il
# doppione nasceva QUI.)
#
# Si sfrutta invece il fatto che dyld risolve le librerie PRIMA di entrare in
# main(): se manca un framework il processo muore subito, senza mai creare una
# QApplication. DYLD_PRINT_LIBRARIES=1 stampa cio' che carica e rende esplicito
# l'errore; il processo viene comunque terminato appena dyld ha finito, cosi'
# nessuna finestra puo' comparire nemmeno se l'avvio andasse a buon fine.
DYLD_LOG="$(mktemp)"
DYLD_PRINT_LIBRARIES=1 "$APP/Contents/MacOS/SurfaceExplorer" >/dev/null 2>"$DYLD_LOG" &
DYLD_PID=$!
# Il tempo di risolvere le librerie: se sopravvive, il link e' a posto.
sleep 2
kill -9 "$DYLD_PID" 2>/dev/null || true
wait "$DYLD_PID" 2>/dev/null || true
DYLD_ERR="$(grep -i "Library not loaded\|different Team IDs\|code signature" "$DYLD_LOG" | head -1 || true)"
rm -f "$DYLD_LOG"
if [ -n "$DYLD_ERR" ]; then
  err "Il bundle non si avvia: $DYLD_ERR
  Cancella $BUILD_DIR e rilancia lo script per rifare il confezionamento da zero."
fi

# ---------------------------------------------------------------------------
# 4. Reset del container = simula la PRIMA installazione
# ---------------------------------------------------------------------------
# Il container e' dove la sandbox confina QSettings, le preferenze e i bookmark.
# Cancellarlo riporta l'app allo stato "mai aperta": e' l'unico modo di riprovare
# il primo avvio, ed e' cio' che distingue il caso che funziona da quello rotto.
#
# SI SVUOTA "Data/", NON SI CANCELLA LA CARTELLA DEL CONTAINER.
# `rm -rf "$CONTAINER"` non puo' funzionare: dentro c'e'
# .com.apple.containermanagerd.metadata.plist, che appartiene al sistema e non e'
# rimovibile ("Operation not permitted") nemmeno dal proprietario della home.
# Il rm usciva quindi con stato 1 e, con `set -euo pipefail`, ABORTIVA lo script
# subito prima del lancio: l'app non si apriva affatto e il test sembrava
# "non partito" senza un errore che lo spiegasse.
# Svuotare Data/ ottiene comunque lo stato "mai aperta": li' dentro stanno
# preferenze, bookmark e le cartelle create dall'app.
if [ "$DO_RESET" -eq 1 ]; then
  if [ -d "$CONTAINER/Data" ]; then
    info "Azzero il contenuto del container: $CONTAINER/Data"
    # -mindepth 1: svuota senza rimuovere Data/ stessa (che il sistema ricrea
    # con i suoi permessi). I 2>/dev/null || true coprono i pochi file
    # protetti: non impediscono il reset e non devono fermare lo script.
    find "$CONTAINER/Data" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
    ok "Container azzerato: il prossimo avvio e' una PRIMA installazione."
  elif [ -d "$CONTAINER" ]; then
    info "Container presente ma senza Data/: l'app risulta gia' mai aperta."
  else
    info "Nessun container da azzerare (l'app risulta gia' mai aperta)."
  fi
  # Le preferenze possono sopravvivere in cache al processo cfprefsd.
  defaults delete "$BUNDLE_ID" 2>/dev/null || true
  killall -u "$USER" cfprefsd 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 5. Avvio
# ---------------------------------------------------------------------------
if [ "$DO_RUN" -eq 0 ]; then
  ok "Pronto (non avviato): $APP"
  exit 0
fi

info "Container sandbox: $CONTAINER"
[ -d "$CONTAINER" ] && info "  (esiste: questo e' un avvio SUCCESSIVO al primo)" \
                    || info "  (non esiste: questo e' il PRIMO avvio)"

if [ "$SHOW_LOGS" -eq 1 ]; then
  info "Avvio con log in console (Ctrl-C per chiudere) ..."
  "$APP/Contents/MacOS/SurfaceExplorer"
else
  info "Avvio ..."
  # -n (nuova istanza) NON va usato qui: con una copia gia' registrata da
  # LaunchServices puo' far ricomparire l'app dopo la chiusura, e sembra un
  # difetto dell'applicazione ("si riapre da sola") che invece e' un effetto
  # del modo in cui l'abbiamo lanciata. `open` semplice usa il ciclo di vita
  # normale del bundle, lo stesso che avra' l'utente finale.
  open "$APP"
  ok "App avviata. Per vedere i log: $0 --logs"
  info "Violazioni di sandbox in tempo reale, in un altro terminale:"
  info "  log stream --predicate 'senderImagePath CONTAINS \"Sandbox\"' --style compact"
fi
