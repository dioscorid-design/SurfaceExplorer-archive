#!/usr/bin/env bash
#
# release_notes.sh - Sostituisce le note di una release GitHub con il testo di un file.
#
# Uso:
#   ./ExC/Mac/release_notes.sh <tag> <file.md>     es. ./ExC/Mac/release_notes.sh v2.0-beta note.md
#
# Fa solo questo: legge la release del tag, salva le note attuali in una copia
# (stampata alla fine: per tornare indietro basta ripassarla allo script),
# mostra le differenze, scrive le nuove note e rilegge la release per
# controllare che GitHub abbia proprio quel testo. Non tocca titolo, tag,
# stato di pre-release ne' i file allegati.
#
# Perche' esiste: le note si aggiornano a mano sul sito o con una chiamata
# all'API. Questo script e' l'unico punto che lo fa, cosi' Claude Code lo puo'
# lanciare con un permesso dedicato in .claude/settings.json invece di avere
# curl e il token GitHub in mano per qualunque cosa. release_macos.sh carica i
# DMG e non tocca mai le note di una release esistente.
#
# Token: come in release_macos.sh -- GH_TOKEN, gh CLI, ~/.git-credentials,
# credential helper di git (su macOS il portachiavi, dove git lo salva al push).
# Il token non viene mai stampato e non passa sulla riga di comando di curl.

set -euo pipefail

REPO="dioscorid-design/SurfaceExplorer"
API="https://api.github.com/repos/$REPO"

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

TAG="${1:-}"
FILE="${2:-}"
[ -n "$TAG" ] && [ -n "$FILE" ] || err "Uso: $0 <tag> <file.md>"
[ -f "$FILE" ] || err "file non trovato: $FILE"
[ -s "$FILE" ] || err "file vuoto: $FILE (le note non si cancellano con questo script)"

TOKEN="${GH_TOKEN:-}"
[ -n "$TOKEN" ] || TOKEN="$(gh auth token 2>/dev/null || true)"
# "|| true": con pipefail un grep senza ~/.git-credentials (exit 2) chiudeva lo
# script in silenzio prima di arrivare al portachiavi.
[ -n "$TOKEN" ] || TOKEN="$(grep 'github.com' "$HOME/.git-credentials" 2>/dev/null | sed -E 's#https://([^:]+):([^@]+)@.*#\2#' | head -1 || true)"
[ -n "$TOKEN" ] || TOKEN="$(printf 'protocol=https\nhost=github.com\n\n' | GIT_TERMINAL_PROMPT=0 git credential fill 2>/dev/null | sed -n 's/^password=//p' | head -1 || true)"
[ -n "$TOKEN" ] || err "nessun token GitHub (GH_TOKEN, 'gh auth login', oppure un push a GitHub per salvarlo nel portachiavi)."

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# L'intestazione col token sta in un file del solo utente: con -H "...$TOKEN"
# il token comparirebbe fra gli argomenti di curl, visibili a ps.
( umask 077; printf 'Authorization: token %s\nAccept: application/vnd.github+json\n' "$TOKEN" > "$WORK/headers" )
api() { curl -s --max-time 60 -H @"$WORK/headers" "$@"; }

# --- 1. la release del tag --------------------------------------------------
CODE="$(api -o "$WORK/release.json" -w '%{http_code}' "$API/releases/tags/$TAG")" || err "GitHub non raggiungibile."
[ "$CODE" = "200" ] || err "release del tag $TAG non trovata (HTTP $CODE)."

# --- 2. copia delle note attuali e differenze --------------------------------
BACKUP="${TMPDIR:-/tmp}/release-notes-$TAG-$(date +%Y%m%d-%H%M%S).md"
RC=0
python3 -I - "$WORK/release.json" "$FILE" "$BACKUP" "$WORK/payload.json" <<'EOF' || RC=$?
import json, sys
rel_path, file_path, backup_path, payload_path = sys.argv[1:5]
rel = json.load(open(rel_path, encoding="utf-8"))
old = (rel.get("body") or "").replace("\r\n", "\n")
new = open(file_path, encoding="utf-8").read().replace("\r\n", "\n")
open(backup_path, "w", encoding="utf-8").write(old)
if old.strip() == new.strip():
    print(f"Le note di {rel.get('tag_name')} sono gia' uguali al file: nessuna modifica.")
    sys.exit(3)
import difflib
diff = list(difflib.unified_diff(old.splitlines(), new.splitlines(), "su GitHub", "nuove", lineterm="", n=0))
print("\n".join(diff))
json.dump({"id": rel["id"], "body": new}, open(payload_path, "w", encoding="utf-8"))
EOF
[ "$RC" -eq 3 ] && exit 0
[ "$RC" -eq 0 ] || err "lettura delle note non riuscita."
RID="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$WORK/payload.json")"
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); json.dump({"body": d["body"]}, open(sys.argv[2], "w"))' \
    "$WORK/payload.json" "$WORK/body.json"

# --- 3. scrittura e verifica -------------------------------------------------
CODE="$(api -o "$WORK/patched.json" -w '%{http_code}' -X PATCH "$API/releases/$RID" --data @"$WORK/body.json")" \
    || err "GitHub non raggiungibile durante l'aggiornamento: le note potrebbero non essere cambiate."
[ "$CODE" = "200" ] || err "aggiornamento rifiutato (HTTP $CODE): $(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("message",""))' "$WORK/patched.json" 2>/dev/null)"

CODE="$(api -o "$WORK/check.json" -w '%{http_code}' "$API/releases/$RID")" || err "rilettura fallita."
python3 -I - "$WORK/check.json" "$FILE" <<'EOF' || err "su GitHub il testo non coincide col file: controlla la pagina."
import json, sys
body = (json.load(open(sys.argv[1], encoding="utf-8")).get("body") or "").replace("\r\n", "\n")
new = open(sys.argv[2], encoding="utf-8").read().replace("\r\n", "\n")
sys.exit(0 if body.strip() == new.strip() else 1)
EOF

echo
echo "Note di $TAG aggiornate e verificate: https://github.com/$REPO/releases/tag/$TAG"
echo "Note precedenti: $BACKUP  (per ripristinarle: $0 $TAG \"$BACKUP\")"
