# Guida al rilascio — Surface Explorer

Come pubblicare una nuova versione su GitHub Releases per Linux, macOS e Windows,
e integrarla nel desktop. Riassume il processo e gli strumenti per piattaforma:

| Script | Piattaforma | Cosa fa |
|--------|-------------|---------|
| [`Linux/release_linux.sh`](../Linux/release_linux.sh) | Linux | Compila → AppImage → carica su GitHub |
| [`Mac/release_macos.sh`](../Mac/release_macos.sh) | macOS | `macdeployqt` → firma → `.dmg` → notarizza (da lanciare **sul Mac**) |
| [`Windows/make_standalone.ps1`](../Windows/make_standalone.ps1) | Windows | Bundle DLL con `windeployqt` → `.zip` (upload manuale) |
| [`Linux/install-linux.sh`](../Linux/install-linux.sh) | Linux (utente finale) | Integra l'AppImage nel menu applicazioni |

> ℹ️ Gli script vivono in `ExC/Linux/`, `ExC/Mac/`, `ExC/Windows/` (cartella `ExC/`
> esclusa da `.gitignore`). I comandi qui sotto assumono di lanciarli dalla **root
> del progetto**, es. `./ExC/Linux/release_linux.sh`.

> 📱 **App Store / TestFlight** è un canale a parte, con certificati e procedura propri —
> non è coperto da questa guida:
>
> | Script | Piattaforma |
> |---|---|
> | [`Mac/release_testflight.sh`](../Mac/release_testflight.sh) | iOS |
> | [`Mac/release_testflight_mac.sh`](../Mac/release_testflight_mac.sh) | macOS |
>
> Procedura completa: [`GUIDA_rilascio_build_ios_macos.md`](GUIDA_rilascio_build_ios_macos.md).
> Nota che `release_macos.sh` (DMG Developer ID, qui sopra) e il canale App Store
> **coesistono**: entitlements e certificati sono diversi e non vanno mescolati.

---

## 🆕 Promemoria: cosa cambiare per una NUOVA versione

> ✅ **Modo rapido e a prova di errore:** usa lo script che aggiorna la versione
> in tutti i punti che devono coincidere e committa in un colpo solo:
> ```sh
> ./ExC/bump-version.sh 1.1          # aggiorna CMakeLists.txt + Info.plist e committa
> ./ExC/bump-version.sh 1.1 --no-commit   # aggiorna soltanto i file
> ```
> Aggiorna solo la **versione marketing** (X.Y); NON tocca il build number, che si
> incrementa a parte (vedi sotto). Dopo: `git push`, poi lo script di release.
> I dettagli manuali qui sotto restano come riferimento / per capire cosa cambia.

Prima di tutto, **aggiorna il numero di versione in due file** (devono coincidere):

1. **`CMakeLists.txt`** (blocco iOS, righe ~144-148):
   - `VERSION "X.Y"`
   - `MACOSX_BUNDLE_SHORT_VERSION_STRING "X.Y"`  ← lo script Linux legge da qui il tag
   - `MACOSX_BUNDLE_BUNDLE_VERSION "N"` e `XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "N"` ← **build number**, va incrementato a ogni upload su App Store/TestFlight
   - `XCODE_ATTRIBUTE_MARKETING_VERSION "X.Y"`

2. **`Info.plist`**:
   - `CFBundleShortVersionString` → `X.Y`  (versione mostrata)
   - `CFBundleVersion` → `N`  (build number, come sopra)

3. **Committa e pusha** le modifiche di versione **prima** di lanciare gli script:
   ```sh
   git add CMakeLists.txt Info.plist
   git commit -m "Bump versione X.Y build N"
   git push origin v1
   ```

Il tag della release sarà `vX.Y`, derivato automaticamente da `CMakeLists.txt`.

> ⚠️ **Regola d'oro:** X.Y = versione marketing (cambia raramente); N = build number
> (incrementa a **ogni** invio ad Apple, anche a parità di X.Y). Apple rifiuta due
> upload con lo stesso build number.

---

## 🐧 Rilascio Linux (automatico)

Un solo comando fa tutto: compila, crea l'AppImage autonoma e la carica su GitHub.

```sh
# solo build + AppImage in ExC/Linux/ (senza toccare GitHub)
./ExC/Linux/release_linux.sh

# build + AppImage + creazione/aggiornamento release GitHub + upload asset
./ExC/Linux/release_linux.sh --upload
```

Cosa fa `--upload`, nell'ordine:
1. Legge la versione da `CMakeLists.txt` → tag `vX.Y`.
2. Compila la release con CMake+Ninja (Qt in `QT_DIR`, default `~/Qt/6.10.2/gcc_64`).
3. Popola l'`AppDir` con le regole `install()` del `CMakeLists.txt` (binario + `.desktop` + icone).
4. Costruisce `SurfaceExplorer-vX.Y-linux-x86_64.AppImage` con `linuxdeploy` + plugin Qt
   (scaricati e messi in cache in `build/appimage-tools/`, senza bisogno di FUSE).
5. Genera lo `.sha256`.
6. Se il tag non esiste sul remoto lo crea su `HEAD` e lo pusha; poi crea/riusa la
   release e carica **AppImage + install-linux.sh**, sostituendo asset omonimi.
   Prima dell'upload rimuove dalla release gli AppImage di **versioni precedenti**
   (nome `SurfaceExplorer-vX.Y-linux-x86_64.AppImage`), che altrimenti resterebbero
   affiancati al nuovo (il `--clobber` sostituisce solo asset con lo stesso nome).

Opzioni utili:
```sh
QT_DIR=~/Qt/6.11.0/gcc_64 ./ExC/Linux/release_linux.sh --upload   # altro Qt
./ExC/Linux/release_linux.sh --skip-build --upload                # riusa un build gia' fatto
GH_TOKEN=ghp_xxx ./ExC/Linux/release_linux.sh --upload            # token esplicito
```

Il token GitHub viene preso da `GH_TOKEN` oppure da `~/.git-credentials`
(git `credential.helper store`). Serve permesso di scrittura sul repo.

---

## 🍎 Rilascio macOS (sul Mac)

Sul Mac, dopo aver compilato la Release da Qt Creator
(`build/Desktop_Qt_6_10-Release`):

```sh
# macdeployqt + firma + .dmg + notarizza (senza toccare GitHub)
./ExC/Mac/release_macos.sh

# come sopra, poi crea/aggiorna la release GitHub e carica il .dmg
./ExC/Mac/release_macos.sh --upload
```

Cosa fa lo script, nell'ordine:
0. Copia la `.app` (quella di Qt Creator resta intatta) ed esegue `macdeployqt`:
   senza, l'app trova Qt solo su questo Mac e sugli altri si ferma all'avvio.
1. Firma la copia con **Developer ID + Hardened Runtime** e verifica la firma.
2. Crea il `.dmg` (con un link ad `/Applications`, così l'utente installa col
   drag-and-drop) e lo firma.
3. Lo invia a **notarizzazione** Apple (`notarytool submit --wait`) e fa lo
   **staple** del ticket.
4. Solo con `--upload`: legge la versione da `CMakeLists.txt` → tag `vX.Y`; se il
   tag non esiste sul remoto lo crea su `HEAD` e lo pusha; crea/riusa la release e
   carica il `.dmg` come **`SurfaceExplorer-vX.Y-macos.dmg`** (nome versionato).
   Prima dell'upload rimuove dalla release i `.dmg` di **versioni precedenti**
   (il `--clobber` sostituisce solo asset con lo stesso nome).

Il token GitHub viene preso da `GH_TOKEN` oppure da `~/.git-credentials`, come per
Linux. Serve permesso di scrittura sul repo.

> Cambiando la versione in `CMakeLists.txt` cambiano automaticamente tag e nome
> asset: nessun valore è cablato nello script. **Ricompila** dopo il bump, così il
> binario dentro il `.dmg` corrisponde alla versione nel nome.

> ⚠️ Prima di ogni build iOS/macOS: `git restore Info.plist && plutil -lint Info.plist`.
> L'`Info.plist` si è già corrotto in passato con una sincronizzazione a copia-cartella
> (testo spurio dentro `<dict>`, invalido per la DTD Apple). Vedi la nota sotto.

### Troubleshooting macOS

- **`no identity found` nello script**: il certificato Developer ID non è nel
  Keychain. Verifica con `security find-identity -v -p codesigning` → deve comparire
  `Developer ID Application: GAETANO MOSCHETTI (BAPKX72394)`.

- **Errore 403 durante la notarizzazione**: le credenziali del profilo
  `notarytool-profile` sono scadute o sbagliate. Rigenera con:
  ```sh
  xcrun notarytool store-credentials "notarytool-profile" \
    --apple-id "adenio@libero.it" --team-id "BAPKX72394" \
    --password "NUOVA_APP_SPECIFIC_PASSWORD"
  ```
  (la app-specific password si genera da appleid.apple.com → Sicurezza →
  Password specifiche per l'app).

- **Notarizzazione fallita con `Code=-1003 ... hostname could not be found`**:
  è un problema di **rete/DNS** (l'host Apple non risolve), non dello script.
  Ricontrolla la connessione e rilancia; la firma/dmg già fatti si possono
  riutilizzare.

- **La notarizzazione ha funzionato ma `spctl` risponde `rejected / Insufficient
  Context`**: falso allarme di quel comando `--context` sui `.dmg`. Il verdetto
  autorevole è `xcrun stapler validate "<file>.dmg"` → se dice *"The validate
  action worked!"* il `.dmg` è a posto. In caso di dubbio ri-staple con
  `xcrun stapler staple "<file>.dmg"`.

- **L'app non si avvia dopo `macdeployqt`** (crash silenzioso): cerca in
  Console.app "SurfaceExplorer" — di solito è un plugin Qt mancante; rilancia
  `macdeployqt` con `-verbose=3` per capire cosa manca.

---

## 🪟 Rilascio Windows (su Windows)

Ora c'è uno script che automatizza il bundle; la **pubblicazione resta manuale**
(git/gh non sono sulla macchina Windows). Con il kit **Qt 6.10.2 MinGW**:

1. **Compila la Release** in Qt Creator (o via CMake), ottenendo `SurfaceExplorer.exe`
   in `build\Desktop_Qt_6_10_2_MinGW_64_bit-Release\`.
   L'icona dell'exe arriva da `app.rc` → `icon.ico` (vedi nota in fondo).

2. **Genera l'app indipendente** con lo script (dalla cartella `ExC\Windows`):
   ```bat
   .\make_standalone.bat
   ```
   La versione nel nome dello zip viene letta da `CMakeLists.txt` (come per Linux);
   puoi comunque forzarla con `-Version X.Y`.
   Lo script:
   - rileva automaticamente l'exe Release (o passalo con `-ExePath`);
   - costruisce il bundle in una cartella **temporanea** e vi lancia `windeployqt`
     (DLL Qt, plugin di piattaforma, runtime MinGW e i binari FFmpeg per Multimedia);
   - crea l'archivio `ExC\Windows\SurfaceExplorer-X.Y-windows-x64.zip` e poi elimina la
     cartella temporanea: in `ExC\Windows\` resta **solo lo .zip**.

   Opzioni: `-KeepFolder` (mantiene anche la cartella scompattata, per una verifica
   locale), `-QtBin`/`-MinGWBin` (percorsi Qt/MinGW se diversi dai default
   `C:\Qt\6.10.2\mingw_64\bin` e `C:\Qt\Tools\mingw1310_64\bin`).

3. **Verifica** scompattando lo `.zip` (oppure rilanciando con `-KeepFolder`) e
   avviando `SurfaceExplorer.exe` su una macchina pulita (senza Qt installato):
   deve partire usando solo le DLL presenti accanto all'eseguibile.

4. **Carica lo `.zip`** come asset della release `vX.Y`, manualmente sulla pagina
   *Releases → Draft a new release* (trascinalo nella zona *Attach binaries*).
   Lo script stampa a fine esecuzione il promemoria dei passaggi e la dimensione
   dell'archivio (deve restare sotto i 100 MB, limite GitHub per file).

> ℹ️ Gli `.zip` (e la cartella `ExC\Windows\SurfaceExplorer\` prodotta con `-KeepFolder`)
> sono esclusi da `.gitignore`: sono artefatti di build, non vanno committati.

> ⚠️ **SmartScreen:** l'exe non è firmato con un certificato Authenticode, quindi al
> primo avvio Windows può mostrare *"Windows ha protetto il tuo PC"* → **Ulteriori
> informazioni → Esegui comunque**. Per eliminarlo servirebbe un certificato di
> code-signing (a pagamento), non ancora previsto — a differenza di macOS, che è
> firmato e notarizzato.

---

## 🖥️ Integrazione desktop (utente finale, Linux)

L'utente scarica dalla release **l'AppImage + `install-linux.sh`** nella stessa
cartella, poi:

```sh
chmod +x install-linux.sh
./install-linux.sh            # auto-rileva l'AppImage, crea voce di menu + icona
```

- Installa in `~/Applications/SurfaceExplorer.AppImage` (nome fisso → aggiornamenti puliti).
- Crea `~/.local/share/applications/surface-explorer.desktop` e installa l'icona
  estratta dall'AppImage stessa.
- **Aggiornare:** rilancia lo script con la nuova AppImage.
- **Disinstallare:** `./install-linux.sh --uninstall`.

Se l'AppImage dà "Permission denied": manca il bit di esecuzione → `chmod +x`.
Se manca FUSE: `./SurfaceExplorer-...AppImage --appimage-extract-and-run`.

---

## ⚠️ Sincronizzazione tra Mac e Linux: usa git, non la copia-cartella

Copiare la cartella del progetto tra le macchine (drive condiviso, AirDrop…) ha
causato: `Info.plist` corrotto, file spazzatura macOS (`._*`, `.DS_Store`) sparsi,
e `._pack-*` dentro `.git` che rompono git. **Sincronizza sempre con
`git pull` / `git push`** (branch di lavoro: `v1`). Ogni macchina tiene la propria
working tree; git trasferisce solo contenuti versionati e validi.

Pulizia rapida dei file spazzatura macOS, se ricompaiono:
```sh
find . -name '._*' -not -path './.git/*' -delete
find . -name '.DS_Store' -not -path './.git/*' -delete
```

---

## 📦 Cosa contiene una release completa

Sulla pagina della release `vX.Y` devono comparire:
- `SurfaceExplorer-vX.Y-linux-x86_64.AppImage` (+ `.sha256`)
- `install-linux.sh`
- `SurfaceExplorer-X.Y-windows-x64.zip` (Windows, da `ExC/Windows/make_standalone.ps1`)
- `SurfaceExplorer.dmg` (macOS)
- *Source code (zip/tar.gz)* — generati automaticamente da GitHub dal **tag**:
  per essere aggiornati il tag deve puntare all'ultimo commit committato.
