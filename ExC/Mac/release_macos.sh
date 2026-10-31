#!/bin/bash
#
# release_macos.sh - Pipeline di release macOS per Surface Explorer.
#
#   macdeployqt -> firma (Developer ID + Hardened Runtime) -> .dmg (con link ad
#   /Applications) -> notarizza -> staple -> (opz.) carica su GitHub Releases.
#
# Uso:
#   ./release_macos.sh              firma + .dmg + notarizza (nessun upload)
#   ./release_macos.sh --upload     come sopra, poi crea/aggiorna la release
#                                   GitHub del tag corrente e carica il .dmg
#   ./release_macos.sh --help
#
# >>> PER UNA NUOVA VERSIONE, prima di lanciare lo script aggiorna la versione
# >>> (vedi RELEASE_GUIDE.md) in CMakeLists.txt / Info.plist, poi committa e
# >>> pusha. La versione (=> tag vX.Y) viene letta da CMakeLists.txt.
#
# Il token GitHub viene preso da GH_TOKEN oppure da ~/.git-credentials.

set -e

# --- CONFIGURAZIONE ---
APP_NAME="SurfaceExplorer"
BUNDLE_ID="com.dioscorid.surfaceexplorer"
SIGN_IDENTITY="Developer ID Application: GAETANO MOSCHETTI (BAPKX72394)"
NOTARY_PROFILE="notarytool-profile"
REPO="dioscorid-design/SurfaceExplorer"
MACDEPLOYQT="$HOME/Qt/6.10.1/macos/bin/macdeployqt"

# Lo script vive in ExC/Mac/ : la root del progetto e' due cartelle sopra
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # ExC/Mac
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"               # root del progetto
BUILD_DIR="$PROJECT_DIR/build/Desktop_Qt_6_10-Release"
APP_PATH="$BUILD_DIR/$APP_NAME.app"
DMG_NAME="$APP_NAME.dmg"
DMG_PATH="$SCRIPT_DIR/$DMG_NAME"                             # .dmg prodotto accanto allo script, in ExC/Mac

ENTITLEMENTS="$SCRIPT_DIR/macos_release.entitlements"

usage() { sed -n '3,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# --- ARGOMENTI ---
DO_UPLOAD=0
for a in "$@"; do
  case "$a" in
    --upload)  DO_UPLOAD=1 ;;
    --help|-h) usage ;;
    *) echo "ERRORE: argomento sconosciuto: $a (usa --help)"; exit 1 ;;
  esac
done

# --- VERSIONE / TAG (letti da CMakeLists.txt, come nello script Linux) ---
# Unica fonte: project(SurfaceExplorer VERSION X.Y ...). I campi del bundle la
# derivano da ${PROJECT_VERSION} e NON contengono piu' un numero, quindi qui non
# si possono leggere. Il vecchio fallback generico su 'VERSION "[0-9.]+"' era
# peggio che inutile: pescava MACOSX_BUNDLE_BUNDLE_VERSION "15", cioe' il BUILD
# NUMBER, e produceva il tag v15 al posto di v1.2.
VERSION="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' \
           "$PROJECT_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "ERRORE: versione non trovata in project(...) di CMakeLists.txt"; exit 1; }
TAG="v$VERSION"
# Nome versionato dell'asset su GitHub. Calcolato QUI dal tag di VERSIONE, prima
# dell'eventuale dirottamento su una pre-release (vedi 7a-pre): il file si chiama
# sempre "...-v1.3-macos.dmg" anche quando finisce sulla release "v1.3-beta".
# E' voluto ed e' coerente con l'AppImage Linux, che sulla beta si chiama gia'
# SurfaceExplorer-v1.3-linux-x86_64.AppImage: il nome porta la VERSIONE del
# binario, non il nome della release che lo ospita.
ASSET_NAME="SurfaceExplorer-${TAG}-macos.dmg"

if [ ! -d "$APP_PATH" ]; then
  echo "ERRORE: $APP_PATH non trovato. Compila prima la Release da Qt Creator."
  exit 1
fi
[ -x "$MACDEPLOYQT" ] || { echo "ERRORE: macdeployqt non trovato: $MACDEPLOYQT"; exit 1; }

# Si lavora su una copia: macdeployqt e la firma cambiano il bundle, e quello di Qt
# Creator deve restare com'e' (firmato in place, al primo Info.plist rigenerato senza
# relink la firma non combaciava piu' e macOS uccideva l'app all'avvio).
STAGING_DIR="$BUILD_DIR/dmg_staging"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"
STAGED_APP="$STAGING_DIR/$APP_NAME.app"
cp -R "$APP_PATH" "$STAGED_APP"

echo ">>> 0. macdeployqt: framework e plugin Qt dentro l'app..."
# Qt Creator non lo fa: la build trova Qt solo con un rpath verso ~/Qt, che esiste
# solo su questo Mac. Qui tutto funziona, su un altro Mac l'app si ferma all'avvio
# (Library not loaded: QtWidgets): i DMG dalla v1.2 alla v1.5 sono usciti cosi'.
"$MACDEPLOYQT" "$STAGED_APP"
[ -d "$STAGED_APP/Contents/Frameworks/QtCore.framework" ] \
  || { echo "ERRORE: macdeployqt non ha incorporato i framework Qt."; exit 1; }
[ -f "$STAGED_APP/Contents/PlugIns/platforms/libqcocoa.dylib" ] \
  || { echo "ERRORE: manca il plugin cocoa: l'app si fermerebbe all'avvio."; exit 1; }

echo ">>> 1. Firma con Developer ID + Hardened Runtime..."
codesign --force --deep --options runtime \
  --entitlements "$ENTITLEMENTS" \
  --sign "$SIGN_IDENTITY" \
  "$STAGED_APP"

echo ">>> 2. Verifica firma..."
codesign --verify --deep --strict --verbose=2 "$STAGED_APP"

echo ">>> 3. Creazione .dmg (con collegamento alla cartella Applicazioni)..."
rm -f "$DMG_PATH"
ln -s /Applications "$STAGING_DIR/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING_DIR" -ov -format UDZO "$DMG_PATH"
rm -rf "$STAGING_DIR"

echo ">>> 4. Firma del .dmg..."
codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"

echo ">>> 5. Invio a notarizzazione (attende il risultato, puo' richiedere alcuni minuti)..."
xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait

echo ">>> 6. Staple del ticket di notarizzazione..."
xcrun stapler staple "$DMG_PATH"

echo ""
echo "=== DMG PRONTO E NOTARIZZATO: $DMG_PATH ==="
echo "Verifica: spctl -a -t open --context context:primary-signing-identifier -v \"$DMG_PATH\""

# --- 7. UPLOAD su GitHub Releases (opz.) ---
if [ "$DO_UPLOAD" -eq 0 ]; then
  echo ""
  echo "Nessun upload. Per caricare su GitHub:  $0 --upload"
  exit 0
fi

echo ""
echo ">>> 7. Upload su GitHub Releases ($REPO, tag $TAG)..."
# Token GitHub: GH_TOKEN esplicito -> gh CLI (keyring) -> ~/.git-credentials (store)
TOKEN="${GH_TOKEN:-}"
[ -n "$TOKEN" ] || TOKEN="$(gh auth token 2>/dev/null || true)"
[ -n "$TOKEN" ] || TOKEN="$(grep 'github.com' "$HOME/.git-credentials" 2>/dev/null | sed -E 's#https://([^:]+):([^@]+)@.*#\2#' | head -1)"
[ -n "$TOKEN" ] || { echo "ERRORE: nessun token GitHub (imposta GH_TOKEN, oppure 'gh auth login', oppure configura git credential.helper store)."; exit 1; }
API="https://api.github.com/repos/$REPO"
gh_api() { curl -s -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

# 7a-pre. PRE-RELEASE APERTA: se esiste gia' su GitHub una release marcata
# pre-release il cui tag COMINCIA col tag di questa versione (es. "v1.3-beta",
# "v1.3-rc1"), si carica LI' invece di creare "v1.3".
#
# Perche': il tag viene da CMakeLists.txt, che contiene solo X.Y e non sa nulla
# del suffisso. Caricando col tag secco lo script CREAVA una release "v1.3"
# nuova accanto alla "v1.3 (beta)" ancora aperta, e il .dmg finiva in una
# release che nessuno stava usando (successo il 2026-09-17).
#
# Solo PRE-RELEASE: una release definitiva con lo stesso prefisso non dirotta
# nulla, altrimenti pubblicare la 1.3 finale rimanderebbe per sempre gli upload
# alla beta. Se ce n'e' piu' d'una si prende la piu' RECENTE.
# Fallisce in modo innocuo: senza rete o senza risposta valida, TAG resta quello
# derivato dalla versione e il comportamento e' quello di prima.
PRERELEASE_TAG="$(gh_api "$API/releases?per_page=100" | python3 -c '
import json,sys
try:
    rels = json.load(sys.stdin)
except Exception:
    sys.exit(0)
if not isinstance(rels, list): sys.exit(0)
base = sys.argv[1]
cand = [r for r in rels
        if r.get("prerelease") and isinstance(r.get("tag_name"), str)
        and r["tag_name"].startswith(base) and r["tag_name"] != base]
cand.sort(key=lambda r: r.get("created_at",""), reverse=True)
print(cand[0]["tag_name"] if cand else "")
' "$TAG" 2>/dev/null || true)"

if [ -n "$PRERELEASE_TAG" ]; then
  echo ">>> Trovata pre-release aperta per $TAG: carico su '$PRERELEASE_TAG'"
  echo ">>>   (per pubblicare sul tag secco $TAG, chiudi prima la pre-release su GitHub)"
  TAG="$PRERELEASE_TAG"
fi

# 7a. assicura il tag vX.Y su origin (lo crea su HEAD se manca; NON sposta uno esistente)
if ! git -C "$PROJECT_DIR" ls-remote --tags origin "$TAG" | grep -q "$TAG"; then
  echo ">>> Tag $TAG assente sul remoto: lo creo su HEAD ($(git -C "$PROJECT_DIR" rev-parse --short HEAD)) e lo pusho"
  git -C "$PROJECT_DIR" tag -a "$TAG" -m "Release $TAG" 2>/dev/null || git -C "$PROJECT_DIR" tag "$TAG"
  git -C "$PROJECT_DIR" push origin "$TAG"
else
  echo ">>> Tag $TAG gia' presente sul remoto (lo uso cosi' com'e')"
fi

# 7b. trova (o crea) la release del tag
# La risposta si legge in due passi, non in pipe diretta a python: una risposta
# VUOTA o non-JSON (rete che cade, 5xx di GitHub, proxy che intercetta) faceva
# morire json.load con un traceback, e lo script si fermava DOPO aver costruito
# e notarizzato il DMG -- il lavoro lungo buttato per un intoppo di un secondo.
# Con un retry e un parse tollerante il caso si risolve da solo.
RID=""
for attempt in 1 2 3; do
  RESP="$(gh_api "$API/releases/tags/$TAG" || true)"
  RID="$(printf '%s' "$RESP" | python3 -c '
import json,sys
try:
    d=json.load(sys.stdin)
except Exception:
    sys.exit(0)                      # risposta non-JSON: RID vuoto, si riprova
print(d.get("id","") if isinstance(d,dict) else "")
')"
  [ -n "$RID" ] && break
  # 404 = la release non esiste ancora: non e' un errore di rete, si crea sotto
  printf '%s' "$RESP" | grep -q '"status": *"404"' && break
  echo ">>>   risposta non valida dall'\''API (tentativo $attempt/3), riprovo..."
  sleep 3
done
if [ -z "$RID" ]; then
  echo ">>> Creo la release per $TAG"
  # Anche qui NON ci si fida della risposta al POST: la lettura sopra era gia'
  # stata resa tollerante, ma questa riga era rimasta una pipe diretta a
  # json.load. Sulla v1.3 e' esplosa proprio qui nello script Linux, con la
  # release creata su GitHub e lo script morto nel leggerne la risposta.
  # La creazione puo' riuscire anche se il corpo non arriva: si rilegge il tag.
  gh_api -X POST "$API/releases" \
    -d "{\"tag_name\":\"$TAG\",\"name\":\"Surface Explorer $TAG\",\"draft\":false}" >/dev/null || true
  for attempt in 1 2 3; do
    RID="$(gh_api "$API/releases/tags/$TAG" | python3 -c '
import json,sys
try:
    d=json.load(sys.stdin)
except Exception:
    sys.exit(0)
print(d.get("id","") if isinstance(d,dict) else "")
')"
    [ -n "$RID" ] && break
    echo ">>>   release non ancora leggibile (tentativo $attempt/3), riprovo..."
    sleep 3
  done
fi
[ -n "$RID" ] || { echo "ERRORE: impossibile ottenere/creare la release."; exit 1; }
echo ">>> Release id: $RID"

# 7c. rimuovi i .dmg di VERSIONI PRECEDENTI rimasti sulla release (nome diverso dal
#     nuovo => --clobber non li tocca). Cancelliamo tutti i .dmg tranne quello nuovo.
echo ">>> Rimuovo eventuali .dmg di versioni precedenti dalla release..."
STALE_IDS="$(gh_api "$API/releases/$RID/assets" | python3 -c '
import json,sys
keep=sys.argv[1]
for a in json.load(sys.stdin):
    n=a.get("name","")
    if n.startswith("SurfaceExplorer-v") and n.endswith("-macos.dmg") and n!=keep:
        print(a["id"])
' "$ASSET_NAME")"
for aid in $STALE_IDS; do
  echo ">>>   elimino .dmg obsoleto (asset id $aid)"
  gh_api -X DELETE "$API/releases/assets/$aid" >/dev/null
done

# 7d. carica il .dmg con nome versionato. gh richiede che il file abbia il nome
#     dell'asset: copio il .dmg col nome versionato in una staging temporanea.
UPLOAD_DIR="$(mktemp -d)"
cp "$DMG_PATH" "$UPLOAD_DIR/$ASSET_NAME"
echo ">>> Carico $ASSET_NAME ($(du -h "$UPLOAD_DIR/$ASSET_NAME" | cut -f1))..."
if command -v gh >/dev/null 2>&1; then
  GH_TOKEN="$TOKEN" gh release upload "$TAG" "$UPLOAD_DIR/$ASSET_NAME" --clobber -R "$REPO" \
    || { echo "ERRORE: gh release upload di $ASSET_NAME fallito (vedi output sopra)."; rm -rf "$UPLOAD_DIR"; exit 1; }
  echo ">>>   -> OK ($ASSET_NAME via gh)"
else
  # Fallback: REST API via curl. Se 422 (nome gia' presente) rimuove il vecchio e riprova.
  UPL="https://uploads.github.com/repos/$REPO"
  do_upload() { curl -s -X POST --max-time 900 -H "Authorization: token $TOKEN" \
    -H "Content-Type: application/octet-stream" -H "Expect:" \
    --data-binary @"$1" -w $'\n%{http_code}' "$UPL/releases/$RID/assets?name=$(basename "$1")"; }
  resp="$(do_upload "$UPLOAD_DIR/$ASSET_NAME")"; code="${resp##*$'\n'}"
  if [ "$code" = "422" ]; then
    existing="$(gh_api "$API/releases/$RID/assets" | python3 -c "import json,sys;print(next((a['id'] for a in json.load(sys.stdin) if a['name']=='$ASSET_NAME'),''))")"
    [ -n "$existing" ] && { echo ">>>   asset omonimo presente: lo sostituisco"; gh_api -X DELETE "$API/releases/assets/$existing" >/dev/null; }
    resp="$(do_upload "$UPLOAD_DIR/$ASSET_NAME")"; code="${resp##*$'\n'}"
  fi
  [ "$code" = "201" ] || { echo "ERRORE: upload di $ASSET_NAME fallito (HTTP $code). Risposta: ${resp%$'\n'*}"; rm -rf "$UPLOAD_DIR"; exit 1; }
  echo ">>>   -> OK ($ASSET_NAME)"
fi
rm -rf "$UPLOAD_DIR"

echo ""
echo "=== RELEASE $TAG PUBBLICATA ==="
echo "  https://github.com/$REPO/releases/tag/$TAG"
echo "Asset caricato: $ASSET_NAME"
