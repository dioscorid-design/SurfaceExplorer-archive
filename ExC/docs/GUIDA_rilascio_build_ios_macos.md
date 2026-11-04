# Guida: correggere un bug e ricaricare una nuova build su App Store Connect (iOS e macOS)

Procedura passo-passo per quando trovi un bug (su TestFlight o altrove), lo correggi nel
codice, e devi far arrivare la build aggiornata ai tester/App Store. Scritta dopo il primo
giro di rilascio della 3.0, che ha incontrato alcuni intoppi — sono documentati qui sotto
insieme alla soluzione, per non doverli ridiagnosticare da zero.

**Vale per entrambe le piattaforme.** Il flusso è lo stesso: bump del build number →
pulizia → rigenerazione del progetto Xcode → archive → upload dall'Organizer. Dove iOS e
macOS divergono è segnalato con **iOS** / **macOS**; il resto è identico. La differenza
sostanziale è solo lo script da lanciare e la destinazione dell'archive.

---

## 0. Percorsi e strumenti di riferimento

| Cosa | Percorso |
|---|---|
| Repo sorgente | `/Users/dioscorid/Projects/C/SurfaceExplorer` |
| Progetto Xcode **iOS** (generato) | `build/ios-appstore/SurfaceExplorer.xcodeproj` |
| Progetto Xcode **macOS** (generato) | `build/macos-appstore/SurfaceExplorer.xcodeproj` |
| qt-cmake **iOS** | `/Users/dioscorid/Qt/6.12.0/ios_device/bin/qt-cmake` |
| qt-cmake **macOS** | `/Users/dioscorid/Qt/6.12.0/macos/bin/qt-cmake` |
| Bundle ID (lo stesso per iOS e macOS) | `com.dioscorid.surfaceexplorer` |
| Team | GAETANO MOSCHETTI (AJ655XKJR8) |
| Apple ID developer | adenio@libero.it |
| App su App Store Connect | Surface Explorer — App ID 6787015297 |
| Gruppo TestFlight | Internal Testers |

### Script per piattaforma

| Piattaforma | Script |
|---|---|
| iOS | `./ExC/Mac/release_testflight.sh` |
| macOS | `./ExC/Mac/release_testflight_mac.sh` |

Fanno le stesse cose; cambiano toolchain, destinazione dell'archive e cartella di build.
Non confonderli: lanciare quello iOS per una release macOS produce un archivio della
piattaforma sbagliata, che App Store Connect rifiuta.

### Prerequisiti specifici di macOS (una volta sola)

Il Mac ha un canale di distribuzione diverso da iOS, e richiede **certificati propri**:

| Certificato | A cosa serve |
|---|---|
| **Apple Distribution** | firma l'app |
| **Mac Installer Distribution** | firma il `.pkg` |

Si creano su
[developer.apple.com → Certificates](https://developer.apple.com/account/resources/certificates),
si scaricano e si installano con doppio clic. I certificati usati per iOS
(*Apple Development*) e per il DMG fuori dallo Store (*Developer ID Application*) **non**
sono accettati da App Store Connect.

Inoltre, sempre una volta sola: su App Store Connect la **piattaforma macOS va abilitata**
sulla stessa app. Bundle id identico non basta — se la piattaforma non c'è, l'upload viene
rifiutato.

> **Sandbox.** La build macOS per lo Store è sandboxed: gli entitlements stanno in
> `ExC/Mac/macos_appstore.entitlements` e il CMakeLists li aggancia al progetto Xcode
> (`XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS`), così è Xcode a firmare sandboxed durante
> l'archive. Sono volutamente distinti da `macos_release.entitlements`, che serve al DMG
> Developer ID e contiene permessi che l'App Store rifiuta.
>
> È per la sandbox che l'export video su macOS **non usa più ffmpeg**: un'app sandboxed non
> può lanciare eseguibili esterni al bundle. Al suo posto c'è l'encoder nativo
> `nativevideoencoder_mac.mm` (AVFoundation). Se l'export video si rompe solo nella build
> dello Store e non in locale, è lì che bisogna guardare.

---

## 1. Correggi il bug nel codice

Modifica i file sorgente in `/Users/dioscorid/Projects/C/SurfaceExplorer` come sempre
(mainwindow.cpp, ecc.). Nessuna differenza rispetto al normale lavoro su desktop.

## 2. Testa velocemente in locale (facoltativo ma consigliato)

Per iterare rapidamente SENZA rifare tutto il giro Xcode/App Store Connect ogni volta:
- Compila ed esegui da **Qt Creator** (kit iOS) sul tuo device via cavo/wireless, oppure
- Compila la build **Desktop** e verifica lì la logica se il bug non è iOS-specifico.

Solo quando sei soddisfatto del fix, procedi al packaging per App Store Connect.

## VIA RAPIDA — script `release_testflight*.sh` (automatizza i passi 3–5)

Lo script esegue in un colpo solo: incremento del build number, commit, pulizia cache
Xcode, rigenerazione pulita del progetto e `xcodebuild archive`. Al termine apre
l'Organizer per l'upload manuale (passo 6).

```bash
cd /Users/dioscorid/Projects/C/SurfaceExplorer

./ExC/Mac/release_testflight.sh        # iOS
./ExC/Mac/release_testflight_mac.sh    # macOS

# opzioni (identiche per entrambi):
#   --no-commit    applica il bump ma non committa
#   --no-archive   solo bump + clean + rigenera, poi apre il progetto (Archive a mano)
#   --help
```

> **Il build number è condiviso fra le due piattaforme**: i campi bumpati
> (`MACOSX_BUNDLE_BUNDLE_VERSION`, `XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION`,
> `CFBundleVersion`) sono gli stessi. Non è un problema — App Store Connect tiene i numeri
> distinti per piattaforma — ma spiega perché il numero "salta" dopo un rilascio sull'altra
> piattaforma. Non c'è bisogno di riallinearlo.

Dettagli:
- **Build number auto-incrementale**: legge il valore corrente da CMakeLists.txt e fa +1,
  sincronizzando i 3 punti (CMake ×2 + Info.plist) con verifica di coerenza e `plutil -lint`.
  NON tocca la versione marketing X.Y (quella si cambia con `ExC/bump-version.sh`).
- **Fa da sé la pulizia critica** del passo 4 (DerivedData + `build/ios-appstore`): niente
  binari stantii.
- **Non fa l'upload** (nessuna API key richiesta): l'Organizer si apre già sull'archivio,
  tu procedi con *Distribute App → App Store Connect → Upload* (passo 6) e poi con
  Export Compliance + assegnazione tester (passi 7–8).
- Se `xcodebuild archive` fallisce per firma/profili, vedi il Troubleshooting in fondo.

I passi 3–8 qui sotto restano la **procedura manuale di riferimento** (e il fallback se lo
script si ferma su un intoppo): leggili per capire cosa fa lo script e per i passi che
restano comunque manuali (6–8).

## 3. Incrementa il build number (OBBLIGATORIO)

> Automatizzato dallo script `release_testflight.sh` (vedi "Via rapida" sopra). Questa
> sezione descrive la procedura manuale equivalente.

Apple non accetta due upload con lo stesso build number, anche se il precedente
non è mai stato pubblicato. Ad ogni ricarica, incrementa di 1.

Apri **CMakeLists.txt** e cambia i numeri nel ramo della piattaforma che stai rilasciando —
`if (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")` per iOS, `elseif (APPLE)` per macOS. In
ciascun ramo sono DUE, e devono restare uguali tra loro:
```cmake
MACOSX_BUNDLE_BUNDLE_VERSION "N"             # <- incrementa
XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "N"  # <- incrementa (stesso valore)
```
> Da quando esiste anche il ramo macOS, in tutto il file queste chiavi compaiono
> **quattro** volte (due per ramo). Gli script le allineano tutte; a mano, tienile
> allineate anche tu — un valore rimasto indietro produce un archivio con un build number
> diverso da quello che credi di star caricando.
Poi apri **Info.plist** e cambia lo stesso numero:
```xml
<key>CFBundleVersion</key>
<string>N</string>
```
(`CFBundleShortVersionString` / `MARKETING_VERSION` = "3.0" resta invariato finché non
cambi la versione "commerciale", es. per la 3.1 o 4.0.)

Committa e pusha (buona pratica, non strettamente necessario per il build):
```bash
cd /Users/dioscorid/Projects/C/SurfaceExplorer
git add -A
git commit -m "Fix <descrizione bug>; build number -> N"
git push origin v1
```

## 4. Rigenera il progetto Xcode PULITO (passo critico)

⚠️ **Questo è il passo che ci ha causato problemi la prima volta**: rigenerare il progetto
Xcode ripetutamente senza pulire la cache può far sì che Xcode riusi oggetti compilati
VECCHI invece di ricompilare i file modificati, producendo un binario con codice stantio
che PASSA la validazione ma non contiene i tuoi fix.

**Fai SEMPRE così, in quest'ordine:**
```bash
# 1. Pulisci la cache di build di Xcode per questo progetto.
#    (find, non 'rm -rf .../SurfaceExplorer-*': su zsh il glob senza match
#     dà "no matches found" e non esegue. Con find, cache già pulita = nessun output.)
find ~/Library/Developer/Xcode/DerivedData -maxdepth 1 -name 'SurfaceExplorer-*' -exec rm -rf {} +

# 2. Cancella la cartella del progetto generato + 3. rigenera con qt-cmake
#    (NON usare cmake generico: serve il toolchain della piattaforma giusta)

# --- iOS ---
rm -rf build/ios-appstore
/Users/dioscorid/Qt/6.12.0/ios_device/bin/qt-cmake -S . -B build/ios-appstore -G Xcode

# --- macOS ---
rm -rf build/macos-appstore
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"   # vedi nota sotto
/Users/dioscorid/Qt/6.12.0/macos/bin/qt-cmake -S . -B build/macos-appstore -G Xcode
```

> ⚠️ **`qt-cmake` ha bisogno di `cmake` nel PATH.** È un wrapper che fa `exec cmake`, e su
> questa macchina `cmake` non è nel PATH (arriva con Qt, non con Xcode): senza l'`export`
> qui sopra muore con `exec: cmake: not found`. Gli script lo gestiscono da soli; a mano
> serve ricordarsene.

Verifica che i valori siano quelli attesi (sostituisci la cartella con quella della
piattaforma):
```bash
grep -h "MARKETING_VERSION\|CURRENT_PROJECT_VERSION\|PRODUCT_BUNDLE_IDENTIFIER\|CODE_SIGN_ENTITLEMENTS" \
  build/macos-appstore/SurfaceExplorer.xcodeproj/project.pbxproj | sort -u
```
Su **macOS** devono comparire anche `DEVELOPMENT_TEAM`, `CODE_SIGN_STYLE = Automatic` e
`CODE_SIGN_ENTITLEMENTS` che punta a `macos_appstore.entitlements`. Se mancano, l'archive
gira ma la firma automatica fallisce: controlla il ramo `elseif (APPLE)` del CMakeLists.

## 5. Apri Xcode e archivia

```bash
open build/ios-appstore/SurfaceExplorer.xcodeproj      # iOS
open build/macos-appstore/SurfaceExplorer.xcodeproj    # macOS
```
1. In alto, seleziona la destinazione:
   - **iOS** → **"Any iOS Device (arm64)"** (NON un simulatore: con un simulatore
     selezionato "Archive" resta disabilitato).
   - **macOS** → **"My Mac"**.
2. Verifica **Signing & Capabilities** del target SurfaceExplorer: Team = GAETANO MOSCHETTI,
   "Automatically manage signing" spuntato. Se vedi errori tipo "No Account for Team" o
   "No profiles found", vedi la sezione Troubleshooting sotto.
3. Menu **Product → Archive** (usa il menu in alto, non i pulsanti ▶/■ della toolbar).
4. ⚠️ Con la cache pulita la compilazione sarà LENTA (parecchi minuti, Qt è statico e
   pesante da linkare): NON interromperla, anche se sembra ferma.
5. Al termine si apre l'**Organizer** con il nuovo archivio. Verifica in alto a destra che
   **Version** mostri il build number appena impostato (es. "3.0 (N)").

## 6. Distribuisci (upload su App Store Connect)

Nell'Organizer, con l'archivio nuovo selezionato:
1. **Distribute App**
2. **App Store Connect** → Next
3. **Upload** → Next
4. Lascia **"Upload your app's symbols"** attivo (per crash report leggibili) → Next
5. **Automatically manage signing** → Next
6. **Upload**

Su **macOS** Xcode firma e costruisce il `.pkg`, ma ⚠️ **non incorpora Qt**: non ne sa
nulla. `macdeployqt` va eseguito sull'app dentro l'`.xcarchive` *prima* di distribuire —
lo fa `release_testflight_mac.sh` (passo 5-bis), che poi rifirma il bundle. Se archivi a
mano da Xcode, devi farlo tu: vedi il troubleshooting *"l'app crasha all'avvio"*.

Servono comunque i due certificati del canale App Store (vedi *Prerequisiti specifici di
macOS* al passo 0).

Se compare **"Upload completed with warnings" / "Upload Symbols Failed"**: è un warning
NON bloccante sui file dSYM, capitato regolarmente con questo progetto Qt/CMake. Clicca
**Done** e prosegui — l'upload del binario è comunque riuscito. (Se un giorno vuoi
risolverlo alla radice, serve configurare `DEBUG_INFORMATION_FORMAT` nel CMakeLists, ma
non è mai stato necessario finora.)

## 7. Attendi l'elaborazione e verifica

1. Aspetta **10-30 minuti**.
2. Controlla la posta **adenio@libero.it** (anche spam): se Apple respinge la build per un
   problema di conformità (es. ITMS-xxxxx), arriva un'email con la spiegazione. Se non
   arriva nulla, buon segno.
3. Vai su **appstoreconnect.apple.com → Surface Explorer → TestFlight**: la nuova build
   deve comparire con stato **verde "Completo"**.
4. Se compare **"⚠️ Conformità mancante"** (Export Compliance): clicca **Gestisci** →
   alla domanda sugli algoritmi di crittografia scegli **"Nessuno degli algoritmi citati
   sopra"** (l'app non usa crittografia propria) → Salva. Va ripetuto per OGNI build.

## 8. Assegna la build al gruppo tester e reinstalla

1. Tab **TestFlight** → gruppo **Internal Testers** → tab **Build** → assegna/seleziona
   la nuova build (di solito è automatico se è l'unica "pronta"). Le build iOS e macOS
   compaiono in **sezioni separate** per piattaforma.
2. Installa:
   - **iOS** — sul device (iPad 11 / iPhone 16e): apri **TestFlight** → aggiornamento
     disponibile → **Update**.
   - **macOS** — apri **TestFlight per Mac**. ⚠️ Non è preinstallato come su iOS: va
     scaricato una volta dal Mac App Store. Se non lo trovi sul Mac non è un errore di
     configurazione, semplicemente non c'è ancora.
3. Apri Surface Explorer e verifica che il fix sia presente.

   Su **macOS**, in più: è la prima volta che l'app gira **sandboxed**, condizione che in
   locale non si riproduce. Vale la pena provare esplicitamente
   **l'export video** (con audio e senza) e il **salvataggio/caricamento dei preset**, cioè
   i due percorsi che toccano il filesystem e che la sandbox può bloccare.

---

## Troubleshooting — problemi già incontrati e soluzione

### "No Account for Team AJ655XKJR8" / "No profiles for com.dioscorid.surfaceexplorer were found"
Il pulsante "Download Manual Profiles" in Xcode → Settings → Apple Accounts a volte si
blocca silenziosamente (nessun errore, nessun risultato) anche con l'account regolarmente
loggato. Soluzione che ha funzionato:
1. Vai su **developer.apple.com/account/resources/profiles/list** (browser)
2. **+** → **iOS App Development** → Continue
3. Seleziona l'App ID `com.dioscorid.surfaceexplorer` → Continue
4. Seleziona il certificato "Apple Development: adenio@libero.it" → Continue
5. Seleziona i device di test → Continue
6. Nome profilo (es. "SurfaceExplorer Development") → **Generate**
7. **Download** il file `.mobileprovision` (va in ~/Downloads)
8. Installalo manualmente (se il doppio click non basta):
   ```bash
   UUID=$(security cms -D -i ~/Downloads/NOMEFILE.mobileprovision 2>/dev/null | \
     grep -A1 "UUID" | grep string | head -1 | sed -E 's/.*<string>(.*)<\/string>.*/\1/')
   cp ~/Downloads/NOMEFILE.mobileprovision \
     ~/Library/MobileDevice/Provisioning\ Profiles/"$UUID.mobileprovision"
   ```
9. Riapri Xcode e riprova Archive.

### Il binario caricato non contiene i fix recenti (bug "vecchi" nonostante il codice sia aggiornato)
Causa: cache DerivedData/build incrociata da rigenerazioni ripetute del progetto Xcode →
oggetti .o non ricompilati per i file modificati. Prevenzione: segui SEMPRE il passo 4
sopra (pulizia completa prima di rigenerare). Per verificare che un archivio sia
genuinamente fresco:
```bash
ARCHIVE=$(ls -dt ~/Library/Developer/Xcode/Archives/*/*.xcarchive | head -1)
BIN="$ARCHIVE/Products/Applications/SurfaceExplorer.app/SurfaceExplorer"
stat -f "%Sm" -t "%Y-%m-%d %H:%M:%S" "$BIN"   # deve essere DOPO l'ultima modifica ai sorgenti
strings "$BIN" | grep "<una stringa univoca del tuo fix più recente>"   # deve trovarla
```

### macOS: l'app installata da TestFlight CRASHA all'avvio (SIGABRT)
Sintomo: si chiude subito, senza finestre. Nel report (`~/Library/Logs/DiagnosticReports/`,
anche in `Retired/`) lo stack è:
```
QMessageLogger::fatal  <-  QGuiApplicationPrivate::createPlatformIntegration  <-  abort()
```
Causa: **il bundle non contiene Qt.** Xcode archivia il binario così com'è, con
`Contents/Frameworks` e `Contents/PlugIns` vuote e l'rpath che punta a
`~/Qt/6.12.0/macos/lib`. Sulla macchina di sviluppo i *framework* si trovano lo stesso e
sembra tutto a posto, ma i *plugin* no: senza il plugin di piattaforma **cocoa** Qt chiama
`qFatal` e abortisce. Su un altro Mac non parte affatto.

Su iOS non capita perché lì Qt è **statico**; su macOS è dinamico e va incorporato.

Risolto nello script (passo 5-bis): `macdeployqt` sull'app dentro l'`.xcarchive`, poi
rifirma — `macdeployqt` modifica il bundle e invalida la firma di Xcode. Verifica:
```bash
A="<archivio>.xcarchive/Products/Applications/SurfaceExplorer.app"
ls "$A/Contents/Frameworks/QtCore.framework"            # deve esistere
ls "$A/Contents/PlugIns/platforms/libqcocoa.dylib"      # deve esistere
otool -l "$A/Contents/MacOS/SurfaceExplorer" | grep -A2 LC_RPATH | grep path
#   -> @executable_path/../Frameworks  (NON ~/Qt/...)
```

### macOS: "The product archive is invalid. The Info.plist must contain a LSApplicationCategoryType key"
Compare in **Distribute App**, dopo un archive riuscito. La chiave è obbligatoria solo sul
Mac App Store (iOS non la richiede), quindi non si era mai vista.

È già risolta: il ramo macOS usa un plist proprio, **`Info-macos.plist`**, che la contiene.
`Info.plist` resta quello iOS e per il Mac non è utilizzabile — dichiara
`LSRequiresIPhoneOS=true` e porta chiavi UIKit (`UILaunchScreen`, orientamenti, status bar)
prive di senso su Mac.

> ⚠️ **Ora i plist da tenere allineati sono DUE.** `release_testflight_mac.sh` bumpa
> entrambi; a mano, ricordarsi del secondo file.

La categoria nel plist accetta **un solo valore** e **non** è quella mostrata nello Store:
le categorie primaria e secondaria si impostano sulla scheda prodotto di App Store Connect
e in caso di discordanza vincono su questa. Si possono quindi tenere categorie diverse fra
iOS e macOS (es. Education su iOS, Graphics & Design su macOS).

Per verificare che il bundle prodotto sia a posto:
```bash
B=build/macos-appstore/Release/SurfaceExplorer.app/Contents/Info.plist
/usr/libexec/PlistBuddy -c 'Print LSApplicationCategoryType' "$B"   # deve esserci
/usr/libexec/PlistBuddy -c 'Print LSRequiresIPhoneOS' "$B"          # deve dare errore
```

### "Upload Symbols Failed" (warning, NON bloccante)
Riguarda solo i dSYM per i crash report simbolicati. Clicca **Done**: se non ci sono errori
**rossi**, l'upload del binario è riuscito.

Su **macOS** non è uno solo ma una **lunga serie**, ed è normale: `macdeployqt` incorpora
decine di framework Qt e per ognuno manca il dSYM. Le voci `for the A` sono i binari dentro
`QtCore.framework/Versions/**A**/` e simili. Conseguenza pratica: i crash *dentro i
framework Qt* non saranno simbolicati — non il codice dell'app, e non qualcosa che si
debugghi da un crash report. Ignorabile.

### macOS: "No signing certificate 'Mac App Distribution' found"
Mancano i certificati del canale App Store: quelli di iOS e del DMG **non valgono**.
Verifica cosa hai:
```bash
security find-identity -v -p codesigning        # deve elencare "Apple Distribution"
security find-identity -v | grep -i installer   # deve elencare "Mac Installer Distribution"
```
Se non compaiono, creali su
[developer.apple.com → Certificates](https://developer.apple.com/account/resources/certificates)
e installali con doppio clic. Vedi *Prerequisiti specifici di macOS* al passo 0.

### macOS: l'upload viene rifiutato benché il bundle id sia giusto
Sulla stessa app di App Store Connect la **piattaforma macOS va abilitata esplicitamente**:
stesso bundle id non basta. Va aggiunta dalla pagina dell'app.

### macOS: "Library not loaded ... different Team IDs" durante la firma
Il bundle non ha i framework Qt incorporati e punta a `~/Qt/...`, che ha il Team ID di Qt.
Sembra un problema di sandbox ed è invece un bundle non deployato. Con il flusso di questa
guida non capita — è Xcode a incorporarli durante *Distribute App*. Se lo vedi, stai
firmando a mano un `.app` costruito con la build normale: usa lo script, oppure lancia
`macdeployqt` prima di firmare.

### ITMS-90683: Missing purpose string in Info.plist
Se il codice referenzia API camera/microfono (anche solo simbolicamente, es. tramite un
framework multimediale linkato) senza le relative purpose string, Apple respinge la build
con questo errore via email. Le chiavi sono già presenti in Info.plist
(`NSCameraUsageDescription`, `NSMicrophoneUsageDescription`) — non serve ripetere il fix,
ma se in futuro aggiungi un NUOVO framework che tocca altre API sensibili (foto, posizione,
bluetooth...), potrebbe servire una chiave analoga.

### Divergenza git dopo un ripristino manuale di file
Se hai ripristinato manualmente vecchi file (es. da un backup) e `git push` rifiuta con
"fetch first"/rifiuto per divergenza, NON forzare con `--force`. Usa:
```bash
git fetch origin v1
git log --oneline origin/v1..HEAD   # cosa hai tu che loro non hanno
git log --oneline HEAD..origin/v1   # cosa hanno loro che tu non hai
git pull origin v1 --no-edit        # merge automatico se i file non si sovrappongono
git push origin v1
```

### Screenshot iPad "Display da 13"" richiesto ma hai solo iPad 11"
App Store Connect ha sezioni separate per iPad 11" e iPad 13" (12,9"). Puoi caricare gli
screenshot del tuo iPad 11 nella sezione corrispondente ("iPad — Display da 11""); se il
salvataggio della scheda richiede obbligatoriamente anche la 13", bisogna generarli dal
Simulatore Xcode (device "iPad Pro 13-inch") oppure ridimensionare quelli esistenti alle
risoluzioni esatte: 2064×2752, 2752×2064, 2048×2732 o 2732×2048 px.

### Errore -1003 "hostname could not be found" durante upload/notarizzazione
Sintomo (visto sul flusso macOS con `notarytool`, ma vale per ogni upload verso Apple):
```
Error: HTTPError(statusCode: nil, ... Code=-1003 "A server with the specified hostname
could not be found." ... NSErrorFailingURLKey=https://appstoreconnect.apple.com/...)
```
**NON è un problema di firma, API key o del pacchetto**: è un fallimento di risoluzione
**DNS** transitorio (spesso Wi-Fi debole — nei log compare `LQM: moderate` — e cache DNS
che restituisce "Resolved 0 endpoints"). La sottomissione non parte nemmeno. Soluzione:
1. **Riprova**: di solito basta, è transitorio.
2. Svuota la cache DNS: `sudo dscacheutil -flushcache; sudo killall -HUP mDNSResponder`
3. Verifica di essere online e che l'host risolva:
   ```bash
   nslookup appstoreconnect.apple.com          # deve risolvere
   curl -sS -o /dev/null -w "%{http_code}\n" https://appstoreconnect.apple.com/notary/v2/asp
   # un HTTP 401 qui = server RAGGIUNGIBILE (401 = solo "non autenticato", atteso)
   ```
4. Con Wi-Fi instabile, passa a **ethernet** o avvicinati al router: l'upload trasferisce
   il pacchetto e serve una connessione stabile.

---

## Riepilogo comandi rapidi (copia-incolla)

```bash
# === VIA RAPIDA: script (fa bump + clean + rigenera + archivia, poi apre l'Organizer) ===
cd /Users/dioscorid/Projects/C/SurfaceExplorer

./ExC/Mac/release_testflight.sh        # iOS
./ExC/Mac/release_testflight_mac.sh    # macOS

# poi in Organizer: Distribute App > App Store Connect > Upload; quindi Export Compliance
# e assegnazione al gruppo "Internal Testers" (passi 7–8).
```

### Procedura manuale equivalente

```bash
# --- Dopo aver corretto il bug e aggiornato i numeri di versione a mano ---
cd /Users/dioscorid/Projects/C/SurfaceExplorer
git add -A && git commit -m "Fix <descrizione>; build number -> N" && git push origin v1

find ~/Library/Developer/Xcode/DerivedData -maxdepth 1 -name 'SurfaceExplorer-*' -exec rm -rf {} +

# --- iOS ---
rm -rf build/ios-appstore
/Users/dioscorid/Qt/6.12.0/ios_device/bin/qt-cmake -S . -B build/ios-appstore -G Xcode
open build/ios-appstore/SurfaceExplorer.xcodeproj
# -> in Xcode: destinazione "Any iOS Device (arm64)", Product > Archive,
#    poi Distribute App > App Store Connect > Upload

# --- macOS ---
rm -rf build/macos-appstore
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"   # qt-cmake esegue `cmake`
/Users/dioscorid/Qt/6.12.0/macos/bin/qt-cmake -S . -B build/macos-appstore -G Xcode
open build/macos-appstore/SurfaceExplorer.xcodeproj
# -> in Xcode: destinazione "My Mac", Product > Archive,
#    poi Distribute App > App Store Connect > Upload
```
