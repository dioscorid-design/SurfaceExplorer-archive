#!/usr/bin/env python3
import os

# --- CONFIGURAZIONE ---

# 1. Nome del file QRC
QRC_FILENAME = "resources.qrc"

# 2. Mappa dei gruppi: "Prefisso Qt" -> [Lista Cartelle Fisiche]
# 2. Mappa dei gruppi: "Prefisso Qt" -> [Lista Cartelle Fisiche]

RESOURCE_GROUPS = {
    "/library": [
        "presets/surfaces",
        "presets/textures",
        "presets/records",  
        "presets/sounds"
    ],
    # Gli shader si leggono solo da :/shaders/... (prefisso "/"): elencarli
    # anche sotto "/library" li impacchettava due volte nel binario.
    "/": [
        "shaders"
    ]
}

# 3. File singoli principali da includere SEMPRE nel prefisso "/"
# NB: tutta la documentazione sta in docs/ e conserva quel prefisso anche nel
# .qrc (qrc:/docs/...), cosi' i link relativi fra le pagine funzionano identici
# su disco e da risorsa.
# docs/script_guide.html è la guida linkata dalla documentazione: ometterla da
# questa lista uccide il link. È già successo più volte: c'è una guardia in
# CMakeLists che fa fallire il build se manca dal .qrc.
STATIC_FILES = [
    "icon.png",
    "background.png",
    # Logo in cima a documentation.html. La pagina lo cita RELATIVO a se stessa,
    # cioe' qrc:/docs/... nell'app e docs/... sul sito: serve quindi una copia
    # sotto docs/, non basta quella in radice (che resta l'icona della
    # finestra). Prima l'indice risaliva a "../icon.png"; il commit della
    # copertina per GitHub Pages ha tolto il "../" -- giusto per il sito, dove
    # la home sta dentro docs/ -- e da allora nell'app il logo non si vedeva.
    # E' l'icona gia' RIDOTTA a 220px, non l'originale da 512: QTextBrowser
    # scala senza interpolazione di qualita' e il logo usciva frastagliato.
    "docs/icon_doc.png",
    "docs/documentation.html",
    "docs/script_guide.html",
    # Crediti audio: esiste anche in presets/sounds/ (prefisso /library), ma la
    # copia qui e' quella linkata da doc_acknowledgments.html. Il link e'
    # relativo, quindi risolve a qrc:/docs/CREDITS.html nell'app e a
    # /CREDITS.html sul sito pubblicato: un solo href valido in entrambi.
    "docs/CREDITS.html",
]

# Capitoli del manuale: documentation.html è solo l'indice, il testo sta in una
# pagina per capitolo (docs/doc_*.html). Sono linkati dall'indice e fra loro,
# quindi valgono esattamente come script_guide.html: se ne manca uno il link
# muore. Tenere allineata la lista gemella in CMakeLists.txt (_doc_chapters).
DOC_CHAPTERS = [
    "intro", "quickstart", "interface", "interaction", "projection",
    "dimensions", "equations", "geodesic", "raymarching", "rendering",
    "audio", "animation", "scripting", "library-management",
    "modular-controls", "acknowledgments",
]
STATIC_FILES += [f"docs/doc_{name}.html" for name in DOC_CHAPTERS]

def generate_qrc():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"📂 Cartella Progetto rilevata: {base_dir}")

    qrc_path = os.path.join(base_dir, QRC_FILENAME)
    qrc_content = '<RCC>\n'
    total_files = 0

    # Itera su ogni gruppo
    for prefix, folders in RESOURCE_GROUPS.items():
        qrc_content += f'    <qresource prefix="{prefix}">\n'

        # --- FIX: Aggiungiamo i file statici se siamo nel prefisso root "/" ---
        if prefix == "/":
            for static_file in STATIC_FILES:
                if os.path.exists(os.path.join(base_dir, static_file)):
                    qrc_content += f'        <file>{static_file}</file>\n'
                    total_files += 1
                else:
                    # I file STATIC sono "da includere SEMPRE": se ne manca uno
                    # il .qrc sarebbe rotto (link/risorse morte). Meglio fermarsi
                    # subito che scrivere un file incompleto con esito "Successo".
                    print(f"❌ ERRORE: File statico non trovato: {static_file} — .qrc NON aggiornato.")
                    raise SystemExit(1)

        for folder in folders:
            abs_folder_path = os.path.join(base_dir, folder)

            if not os.path.exists(abs_folder_path):
                print(f"⚠️  ATTENZIONE: Cartella non trovata: {abs_folder_path}")
                continue
            else:
                print(f"   Scanning: {folder}...")

            for root, dirs, files in os.walk(abs_folder_path):
                for file in files:
                    if file.startswith('.'): continue

                    abs_file_path = os.path.join(root, file)
                    rel_path = os.path.relpath(abs_file_path, base_dir).replace("\\", "/")

                    qrc_content += f'        <file>{rel_path}</file>\n'
                    total_files += 1

        qrc_content += '    </qresource>\n'

    qrc_content += '</RCC>'

    try:
        with open(qrc_path, 'w') as f:
            f.write(qrc_content)
        print(f"✅ Successo! '{QRC_FILENAME}' aggiornato con {total_files} file.")
    except Exception as e:
        print(f"Errore: {e}")

if __name__ == "__main__":
    generate_qrc()
