#!/usr/bin/env bash
#
# release_linux.sh - Pipeline di release Linux per Surface Explorer.
#
#   Compila (Qt/CMake) -> impacchetta in AppImage autonoma -> (opz.) carica
#   su GitHub Releases, accanto a install-linux.sh.
#
# Uso:
#   ./release_linux.sh                build + AppImage in Linux/
#   ./release_linux.sh --upload       come sopra, poi crea/aggiorna la release
#                                     GitHub del tag corrente e carica gli asset
#   ./release_linux.sh --skip-build   riusa un binario gia' compilato
#   ./release_linux.sh --help
#
# >>> PER UNA NUOVA VERSIONE, prima di lanciare lo script aggiorna la versione
# >>> (vedi RELEASE_GUIDE.md) in:
# >>>   - CMakeLists.txt : project(SurfaceExplorer VERSION X.Y) e i build number
# >>>     (Info.plist e Info-macos.plist li ricevono da CMake: nessun numero li')
# >>> poi committa e pusha. La versione (=> tag vX.Y) viene letta da CMakeLists.txt.
#
# Requisiti: cmake, ninja, un Qt 6 gcc_64 (variabile QT_DIR), wget, curl, git.
# Non richiede FUSE (gli AppImage-tool girano in modalita' extract-and-run).

set -euo pipefail

# ============================ CONFIGURAZIONE ================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"       # lo script vive in ExC/Linux/
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"    # root del progetto = due cartelle sopra (ExC/Linux -> root)
QT_DIR="${QT_DIR:-$HOME/Qt/6.10.2/gcc_64}"      # override: QT_DIR=... ./release_linux.sh
REPO="dioscorid-design/SurfaceExplorer"
APP_BIN="surface-explorer"                       # OUTPUT_NAME Linux (vedi CMakeLists.txt)

BUILD_DIR="$PROJECT_DIR/build/linux-release"
APPDIR="$PROJECT_DIR/build/AppDir"
DIST_DIR="$SCRIPT_DIR"                           # AppImage prodotta accanto allo script, in ExC/Linux/
TOOLS_DIR="$PROJECT_DIR/build/appimage-tools"    # cache di linuxdeploy

LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
LDQT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

msg()  { printf '\n\033[1;34m>>> %s\033[0m\n' "$*"; }
err()  { printf '\033[1;31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
usage() { sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# ============================ ARGOMENTI ====================================
DO_UPLOAD=0; SKIP_BUILD=0
for a in "$@"; do
  case "$a" in
    --upload)     DO_UPLOAD=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    --help|-h)    usage ;;
    *) err "Argomento sconosciuto: $a (usa --help)" ;;
  esac
done

# ============================ VERSIONE =====================================
# Unica fonte: project(SurfaceExplorer VERSION X.Y ...). I campi del bundle la
# derivano da ${PROJECT_VERSION} e non contengono piu' un numero. Il vecchio
# fallback su 'VERSION "[0-9.]+"' pescava il BUILD NUMBER (v15 invece di v1.2).
VERSION="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' \
           "$PROJECT_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || err "Versione non trovata in project(...) di CMakeLists.txt"
TAG="v$VERSION"
OUTPUT="SurfaceExplorer-${TAG}-linux-x86_64.AppImage"
msg "Versione: $VERSION   Tag: $TAG   Output: $OUTPUT"

# ============================ 1. BUILD =====================================
if [ "$SKIP_BUILD" -eq 0 ]; then
  [ -x "$QT_DIR/bin/qmake" ] || err "Qt non trovato in QT_DIR=$QT_DIR (impostalo: QT_DIR=... $0)"

  # Ninja: preferisci quello di sistema (nel PATH); in fallback usa quello incluso
  # in Qt (~/Qt/Tools/Ninja), lo stesso che usa Qt Creator. Serve perche' da un
  # terminale nudo ninja spesso NON e' nel PATH (mentre il kit di Qt Creator si':
  # da qui l'errore "CMAKE_MAKE_PROGRAM is not set" lanciando lo script a mano).
  NINJA="$(command -v ninja || true)"
  if [ -z "$NINJA" ]; then
    for cand in "$QT_DIR/../../Tools/Ninja/ninja" "$HOME/Qt/Tools/Ninja/ninja"; do
      [ -x "$cand" ] && { NINJA="$cand"; break; }
    done
  fi
  [ -n "$NINJA" ] || err "ninja non trovato: installalo (es. 'sudo dnf install ninja-build') oppure verifica ~/Qt/Tools/Ninja/ninja"

  msg "1/4 Compilo la release (CMake + Ninja) con Qt in $QT_DIR ...  (ninja: $NINJA)"
  cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$NINJA" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_DIR"
  cmake --build "$BUILD_DIR"
else
  msg "1/4 --skip-build: salto la compilazione (riuso $BUILD_DIR)"
fi
[ -f "$BUILD_DIR/$APP_BIN" ] || err "Binario $APP_BIN non trovato in $BUILD_DIR. Compila prima (senza --skip-build)."

# ============================ 2. AppDir ====================================
# Usa le regole install() del CMakeLists (bin + .desktop + icone hicolor)
msg "2/4 Popolo l'AppDir con 'cmake --install' ..."
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null
[ -f "$APPDIR/usr/bin/$APP_BIN" ] || err "install non ha prodotto $APPDIR/usr/bin/$APP_BIN"

# ============================ 3. AppImage ==================================
msg "3/4 Costruisco l'AppImage con linuxdeploy (+plugin Qt) ..."
mkdir -p "$TOOLS_DIR"

# Scarica ed ESTRAE gli AppImage-tool (l'estrazione via --appimage-extract non
# richiede FUSE; eseguirli poi tramite AppRun evita del tutto la dipendenza FUSE).
setup_tool() {  # $1=url  $2=nome-base
  local url="$1" name="$2"
  local ai="$TOOLS_DIR/$name.AppImage" ed="$TOOLS_DIR/$name.dir"
  [ -x "$ed/AppRun" ] && return 0            # gia' estratto e valido
  local try
  for try in 1 2; do
    [ -f "$ai" ] || wget -q -O "$ai" "$url"
    chmod +x "$ai"
    rm -rf "$ed" "$TOOLS_DIR/squashfs-root"
    if ( cd "$TOOLS_DIR" && "$ai" --appimage-extract >/dev/null 2>&1 ) && [ -x "$TOOLS_DIR/squashfs-root/AppRun" ]; then
      mv "$TOOLS_DIR/squashfs-root" "$ed"; return 0
    fi
    # download corrotto/troncato: butta e riprova
    rm -f "$ai"
  done
  err "Impossibile scaricare/estrarre $name (download corrotto). Riprova."
}
setup_tool "$LD_URL"   linuxdeploy
setup_tool "$LDQT_URL" linuxdeploy-plugin-qt

# --- patchelf moderno (fix crash all'avvio dell'AppImage) --------------------
# Il patchelf incluso in linuxdeploy (0.15) CORROMPE le librerie con relocazioni
# DT_RELR / '.relr.dyn' (toolchain recenti, es. Fedora) quando ne riscrive l'RPATH:
# la lib si carica ma con relocazioni rotte -> SIGSEGV nel costruttore, PRIMA di
# main() (crash in call_init/_dl_init, es. dentro libmp3lame o libxcb-xkb caricando
# libqxcb). patchelf ha corretto DT_RELR in 0.18. NB: ANCHE linuxdeploy-plugin-qt
# ha il SUO patchelf 0.15 (deploya lui le lib Qt/xcb) -> vanno sostituiti ENTRAMBI,
# altrimenti le librerie del plugin di piattaforma restano corrotte e l'app crasha
# quando Qt fa dlopen di libqxcb. Sorgente: patchelf di sistema se >= 0.18, altrimenti
# lo scarichiamo.
PE_VER="0.18.0"
PE_URL="https://github.com/NixOS/patchelf/releases/download/$PE_VER/patchelf-$PE_VER-x86_64.tar.gz"
ensure_modern_patchelf() {
  # 1. Trova/ottieni un patchelf >= 0.18 (una volta sola).
  local src="" ver
  local syspe; syspe="$(command -v patchelf || true)"
  if [ -n "$syspe" ]; then
    ver="$("$syspe" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
    if [ -n "$ver" ] && [ "$(printf '%s\n0.18\n' "$ver" | sort -V | head -1)" = "0.18" ]; then
      src="$syspe"; msg "patchelf: uso quello di sistema ($ver)"
    fi
  fi
  if [ -z "$src" ]; then
    local tgz="$TOOLS_DIR/patchelf-$PE_VER.tar.gz"
    [ -f "$tgz" ] || wget -q -O "$tgz" "$PE_URL" || err "download patchelf $PE_VER fallito"
    tar -xzf "$tgz" -C "$TOOLS_DIR" 2>/dev/null || err "estrazione patchelf $PE_VER fallita"
    [ -x "$TOOLS_DIR/bin/patchelf" ] || err "patchelf $PE_VER non trovato dopo l'estrazione"
    src="$TOOLS_DIR/bin/patchelf"; msg "patchelf: uso $PE_VER scaricato (fix DT_RELR)"
  fi
  # 2. Sostituiscilo in TUTTI i tool che ne hanno uno proprio (linuxdeploy E plugin-qt).
  local replaced=0 dst
  for dst in "$TOOLS_DIR/linuxdeploy.dir/usr/bin/patchelf" \
             "$TOOLS_DIR/linuxdeploy-plugin-qt.dir/usr/bin/patchelf"; do
    if [ -f "$dst" ]; then cp -f "$src" "$dst"; replaced=$((replaced+1)); fi
  done
  [ "$replaced" -gt 0 ] || err "nessun patchelf bundlato trovato da sostituire (struttura tool inattesa)"
  msg "patchelf sostituito in $replaced tool bundlati"
}
ensure_modern_patchelf

mkdir -p "$DIST_DIR"
rm -f "$DIST_DIR/$OUTPUT"

# Alcune librerie stanno in sottocartelle NON nel search path standard del linker:
# tipico libpulsecommon di PulseAudio (dipendenza di Qt Multimedia), che vive in
# .../pulseaudio/. Senza aggiungerle a LD_LIBRARY_PATH linuxdeploy fallisce con
# "Could not find dependency: libpulsecommon-*.so". Aggiungiamo le dir esistenti
# (cross-distro: Fedora /usr/lib64, Debian multiarch).
EXTRA_LIBS=""
for d in /usr/lib64/pulseaudio /usr/lib/pulseaudio /usr/lib/x86_64-linux-gnu/pulseaudio; do
  [ -d "$d" ] && EXTRA_LIBS="$EXTRA_LIBS:$d"
done

# AppRun personalizzato: forza QT_QPA_PLATFORM=xcb. Il plugin di piattaforma
# 'wayland' NON viene bundlato (solo xcb): su una sessione Wayland Qt non trova la
# piattaforma e CRASHA (segfault dopo "Could not find the Qt platform plugin
# wayland"). xcb gira sotto XWayland ed e' incluso. Resta overridabile dall'utente:
# QT_QPA_PLATFORM=wayland ./AppImage. RUNPATH ($ORIGIN/../lib) e qt.conf gestiscono
# gia' librerie e plugin, quindi il wrapper deve solo impostare la piattaforma.
APPRUN_WRAPPER="$PROJECT_DIR/build/apprun-wrapper.sh"
cat > "$APPRUN_WRAPPER" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$HERE/usr/bin/surface-explorer" "$@"
EOF
chmod +x "$APPRUN_WRAPPER"

# linuxdeploy scrive l'output nella CWD: lo eseguo dentro dist/ cosi' l'AppImage
# ci finisce direttamente. APPIMAGE_EXTRACT_AND_RUN=1 serve per appimagetool
# (invocato internamente); il plugin qt viene trovato via PATH (usr/bin estratto).
# NB: disattiviamo temporaneamente set -e per catturare l'exit e dare un errore
# CHIARO: senza, un fallimento del subshell fa uscire lo script muto, prima dello
# step di upload (sintomo: "sembra finito" ma dist/ vuota e nessuna release).
set +e
# NO_STRIP=1: lo strip incluso in linuxdeploy (binutils datato) non riconosce la
# sezione ELF '.relr.dyn' (DT_RELR, relocazioni relative) prodotta dai toolchain
# recenti -> "Strip call failed: unknown type [0x13] section .relr.dyn" su quasi
# ogni libreria. Saltiamo lo stripping: AppImage un filo piu' grande ma corretta.
( cd "$DIST_DIR" && env APPIMAGE_EXTRACT_AND_RUN=1 \
    NO_STRIP=1 \
    QMAKE="$QT_DIR/bin/qmake" \
    PATH="$TOOLS_DIR/linuxdeploy-plugin-qt.dir/usr/bin:$PATH" \
    LD_LIBRARY_PATH="$QT_DIR/lib${EXTRA_LIBS}:${LD_LIBRARY_PATH:-}" \
    OUTPUT="$OUTPUT" \
    "$TOOLS_DIR/linuxdeploy.dir/AppRun" \
      --appdir "$APPDIR" \
      --custom-apprun "$APPRUN_WRAPPER" \
      --plugin qt \
      --output appimage )
ld_rc=$?
set -e
[ "$ld_rc" -eq 0 ] || err "linuxdeploy (build AppImage) fallito con exit $ld_rc. Leggi l'errore qui sopra (spesso una dipendenza non trovata)."
[ -f "$DIST_DIR/$OUTPUT" ] || err "linuxdeploy non ha prodotto $OUTPUT"
# NB: niente file .sha256 — GitHub mostra gia' lo sha256 di ogni asset nella UI.
msg "AppImage pronta: $DIST_DIR/$OUTPUT ($(du -h "$DIST_DIR/$OUTPUT" | cut -f1))"

# ============================ 4. UPLOAD (opz.) =============================
if [ "$DO_UPLOAD" -eq 0 ]; then
  cat <<EOF

=== BUILD COMPLETATA (nessun upload) ===
  $DIST_DIR/$OUTPUT
Per caricare su GitHub:  $0 --upload
EOF
  exit 0
fi

msg "4/4 Upload su GitHub Releases ($REPO, tag $TAG) ..."
TOKEN="${GH_TOKEN:-$(grep 'github.com' "$HOME/.git-credentials" 2>/dev/null | sed -E 's#https://([^:]+):([^@]+)@.*#\2#' | head -1)}"
[ -n "$TOKEN" ] || err "Nessun token GitHub (imposta GH_TOKEN o configura git credential.helper store)."
API="https://api.github.com/repos/$REPO"; UPL="https://uploads.github.com/repos/$REPO"
# NB: helper per le chiamate REST via curl. Si chiama gh_api (NON gh) per non
# oscurare la vera CLI 'gh', che usiamo per l'upload robusto degli asset.
gh_api() { curl -s -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

# 4a-pre. PRE-RELEASE APERTA: se esiste gia' su GitHub una release marcata
# pre-release il cui tag COMINCIA col tag di questa versione (es. "v1.3-beta",
# "v1.3-rc1"), si carica LI' invece di creare "v1.3".
#
# Perche': il tag viene da CMakeLists.txt, che contiene solo X.Y e non sa nulla
# del suffisso. Caricando col tag secco lo script CREEREBBE una release "v1.3"
# nuova accanto alla "v1.3 (beta)" ancora aperta, e l'AppImage finirebbe in una
# release che nessuno sta usando (successo con lo script macOS il 2026-09-17).
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

# 4a. assicura il tag vX.Y su origin (lo crea su HEAD se manca; NON sposta uno esistente)
if ! git ls-remote --tags origin "$TAG" | grep -q "$TAG"; then
  msg "Tag $TAG assente sul remoto: lo creo su HEAD ($(git rev-parse --short HEAD)) e lo pusho"
  git tag -a "$TAG" -m "Release $TAG" 2>/dev/null || git tag "$TAG"
  git push origin "$TAG"
else
  msg "Tag $TAG gia' presente sul remoto (lo uso cosi' com'e')"
fi

# Legge un campo da una risposta JSON tollerando corpi vuoti o non-JSON: in quel
# caso stampa la stringa vuota invece di morire con un traceback.
json_field() {  # $1 = nome del campo ; JSON su stdin
  python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
print(d.get(sys.argv[1], "") if isinstance(d, dict) else "")
' "$1"
}

# 4b. trova (o crea) la release del tag.
# Sulla v1.3 e' successo davvero: il POST di creazione era andato a buon fine su
# GitHub, ma il corpo della risposta e' tornato vuoto e json.load e' morto con un
# traceback, uccidendo lo script DOPO la build -- release creata e senza asset.
# Da qui due scelte: lettura tollerante con retry, e soprattutto NON fidarsi della
# risposta al POST. La creazione puo' riuscire anche se il corpo non arriva,
# quindi la fonte di verita' e' rileggere la release del tag.
RID=""
for attempt in 1 2 3; do
  RESP="$(gh_api "$API/releases/tags/$TAG" || true)"
  RID="$(printf '%s' "$RESP" | json_field id)"
  [ -n "$RID" ] && break
  # 404: la release non esiste ancora. Non e' un intoppo di rete, si crea sotto.
  printf '%s' "$RESP" | grep -q '"status": *"404"' && break
  msg "  risposta non valida dall'API (tentativo $attempt/3), riprovo..."
  sleep 3
done

if [ -z "$RID" ]; then
  msg "Creo la release per $TAG"
  gh_api -X POST "$API/releases" \
    -d "{\"tag_name\":\"$TAG\",\"name\":\"Surface Explorer $TAG\",\"draft\":false}" >/dev/null || true
  for attempt in 1 2 3; do
    RID="$(gh_api "$API/releases/tags/$TAG" | json_field id)"
    [ -n "$RID" ] && break
    msg "  release non ancora leggibile (tentativo $attempt/3), riprovo..."
    sleep 3
  done
fi
[ -n "$RID" ] || err "Impossibile ottenere/creare la release."
msg "Release id: $RID"

# 4b-bis. rimuovi gli AppImage di VERSIONI PRECEDENTI rimasti sulla release.
# Il nome include il tag (SurfaceExplorer-vX.Y-linux-x86_64.AppImage): a un cambio
# di versione il nuovo asset ha nome diverso dal vecchio, quindi gh --clobber (che
# sostituisce solo asset OMONIMI) non lo tocca e i due AppImage restano affiancati.
# Cancelliamo qui tutti gli AppImage tranne quello che stiamo per caricare.
msg "Rimuovo eventuali AppImage di versioni precedenti dalla release ..."
STALE_IDS="$(gh_api "$API/releases/$RID/assets" | python3 -c '
import json,sys
keep=sys.argv[1]
for a in json.load(sys.stdin):
    n=a.get("name","")
    if n.startswith("SurfaceExplorer-v") and n.endswith("-linux-x86_64.AppImage") and n!=keep:
        print(a["id"])
' "$OUTPUT")"
for aid in $STALE_IDS; do
  msg "  elimino AppImage obsoleto (asset id $aid)"
  gh_api -X DELETE "$API/releases/assets/$aid" >/dev/null
done

# 4c. carica gli asset in modo ROBUSTO.
# - NON cancella l'asset esistente PRIMA: carica; solo se GitHub risponde 422
#   (nome gia' presente) rimuove il vecchio e riprova. Cosi' un upload fallito non
#   ti lascia senza binario sulla release (com'era successo).
# - Verifica il codice HTTP e FALLISCE con errore chiaro (prima lo ignorava e
#   stampava comunque "RELEASE PUBBLICATA").
# Upload RAW via curl. -H "Expect:" DISABILITA l'handshake 100-continue: con file
# grandi curl manderebbe prima i soli header e l'uploader GitHub, non vedendo il
# corpo, risponde 400 "Multipart form data required". -X POST esplicito.
do_upload() {  # $1=file $2=ctype -> stampa "body<newline>httpcode"
  # URL corretto: .../repos/OWNER/REPO/releases/RID/assets  (il '/releases/' era
  # MANCANTE: senza, GitHub risponde 400 "Multipart form data required").
  curl -s -X POST --max-time 900 \
    -H "Authorization: token $TOKEN" \
    -H "Content-Type: $2" \
    -H "Expect:" \
    --data-binary @"$1" -w $'\n%{http_code}' "$UPL/releases/$RID/assets?name=$(basename "$1")"
}
upload_asset() {
  local file="$1" ctype="$2" name resp code existing
  name="$(basename "$file")"
  msg "Carico $name ($(du -h "$file" | cut -f1)) ..."

  # Via preferita: la CLI gh ufficiale (gestisce correttamente l'upload; --clobber
  # sostituisce un asset omonimo in sicurezza). Auth via GH_TOKEN.
  if command -v gh >/dev/null 2>&1; then
    if GH_TOKEN="$TOKEN" gh release upload "$TAG" "$file" --clobber -R "$REPO"; then
      msg "  -> OK ($name via gh)"; return 0
    fi
    err "gh release upload di $name fallito (vedi output sopra)."
  fi

  # Fallback: REST API via curl. Carica; solo se 422 (nome gia' presente) rimuove il
  # vecchio e riprova, cosi' un upload fallito non lascia la release senza binario.
  resp="$(do_upload "$file" "$ctype")"; code="${resp##*$'\n'}"
  if [ "$code" = "422" ]; then
    existing="$(gh_api "$API/releases/$RID/assets" | python3 -c "import json,sys;print(next((a['id'] for a in json.load(sys.stdin) if a['name']=='$name'),''))")"
    [ -n "$existing" ] && { msg "  asset omonimo presente: lo sostituisco"; gh_api -X DELETE "$API/releases/assets/$existing" >/dev/null; }
    resp="$(do_upload "$file" "$ctype")"; code="${resp##*$'\n'}"
  fi
  [ "$code" = "201" ] || err "upload di $name fallito (HTTP $code). Risposta GitHub: ${resp%$'\n'*}"
  msg "  -> OK ($name)"
}
upload_asset "$DIST_DIR/$OUTPUT"        "application/octet-stream"
[ -f "$SCRIPT_DIR/install-linux.sh" ] && upload_asset "$SCRIPT_DIR/install-linux.sh" "application/x-shellscript"

# 4d. scrive le istruzioni Linux NEL TESTO della release.
# Stavano nel README, ma chi scarica atterra qui e il README non lo apre mai (il
# bottone del sito punta a /releases/latest): il sintomo era l'AppImage che, senza
# bit exec, il desktop passa a un gestore di immagini disco invece di eseguirla.
# Il blocco e' delimitato da marcatori HTML (invisibili nel render): se c'e' gia'
# viene SOSTITUITO - cosi' il nome file segue la versione - altrimenti accodato.
# Il testo scritto a mano intorno non viene mai toccato.
# NON fatale: gli asset sono gia' caricati, non si butta via una release per questo.
msg "Aggiorno le istruzioni Linux nel testo della release ..."
if ! BODY_STATUS="$(API="$API" RID="$RID" TOKEN="$TOKEN" OUTPUT="$OUTPUT" python3 - <<'PY'
import json, os, subprocess, sys

API, RID, TOKEN, OUTPUT = (os.environ[k] for k in ("API", "RID", "TOKEN", "OUTPUT"))
BEGIN, END = "<!-- linux-install:begin -->", "<!-- linux-install:end -->"

block = f"""{BEGIN}
### Linux

Download both `{OUTPUT}` and `install-linux.sh` into the same folder, then run:

```bash
bash install-linux.sh
```

It copies the AppImage to `~/Applications`, makes it executable and adds it to your
applications menu with its icon, so from then on you launch it like any other app.
Re-run it to update, `bash install-linux.sh --uninstall` to remove. No root needed.

**To run the AppImage directly instead**, make it executable first. A downloaded file
carries no permissions, and without this your desktop may hand it to a disk-image
tool instead of launching it:

```bash
chmod +x {OUTPUT}
./{OUTPUT}
```
{END}"""


def api(method, payload=None):
    cmd = ["curl", "-s", "--max-time", "60",
           "-H", f"Authorization: token {TOKEN}",
           "-H", "Accept: application/vnd.github+json",
           "-X", method, f"{API}/releases/{RID}"]
    if payload is not None:
        cmd += ["-d", json.dumps(payload)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return json.loads(r.stdout)


body = api("GET").get("body") or ""

if BEGIN in body and END in body:
    head, rest = body.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    new, what = head + block + tail, "sostituito"
elif body.strip():
    new, what = body.rstrip() + "\n\n" + block, "accodato"
else:
    new, what = block, "creato"

if new == body:
    print("gia' aggiornato")
    sys.exit(0)

if api("PATCH", {"body": new}).get("body") != new:
    sys.exit(1)
print(what)
PY
)"; then
  printf '\033[1;33mATTENZIONE:\033[0m istruzioni Linux NON scritte nel testo della release (gli asset sono caricati).\n' >&2
  BODY_STATUS="non aggiornato"
fi
msg "  -> blocco istruzioni: $BODY_STATUS"

cat <<EOF

=== RELEASE $TAG PUBBLICATA ===
  https://github.com/$REPO/releases/tag/$TAG
Asset caricati: $OUTPUT e install-linux.sh

SourceForge si aggiorna DA SE': un webhook sull'evento 'release' copia gli
asset in sourceforge.net/projects/surface-explorer/files/ con questi stessi
nomi. NON ricaricarli a mano e non usare la "import form": faresti doppioni.
  Verifica dopo qualche minuto: https://sourceforge.net/projects/surface-explorer/files/
  Il README mostrato li' e' ExC/SourceForge/README.md, scritto con wildcard
  (SurfaceExplorer-*-...): NON va aggiornato a ogni versione.
EOF
