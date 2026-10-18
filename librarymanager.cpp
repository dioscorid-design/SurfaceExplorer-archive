#include "librarymanager.h"
#include <QJsonArray>
#include <QDirIterator>
#include <QUrl>
#include <QDebug>
#include <QSettings>
#include <QColor>
#include <QRegularExpression>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

// FILE "DATALESS": esiste, ha una dimensione, ma NON ha contenuto su questo
// disco -- e' un segnaposto di un file provider (iCloud Drive, "Desktop e
// Documenti" sincronizzati, Dropbox...). Aprirlo e leggerlo NON fallisce: il
// kernel sospende la read() e chiede il download al provider. Se il provider non
// risponde -- offline, rete lenta, demone bloccato -- quella read() NON RITORNA
// MAI, e siccome la libreria si carica dal thread della UI l'intera applicazione
// resta piantata, senza errore e senza finestra.
//
// MISURATO (sample su processo bloccato): puntando la libreria a
// ~/Documents/presets, cartella sincronizzata da iCloud, tutti i 2534 campioni
// erano fermi nello stesso punto -- parseJson -> QIODevice::readAll -> read().
// 66 dei preset erano dataless. Peggio: la radice era gia' stata salvata, quindi
// ogni riavvio ripeteva la stessa lettura e l'app non si riapriva piu'.
//
// La domanda giusta NON e' exists() ne' isReadable(): entrambe rispondono true.
// L'unica che distingue e' quanti blocchi occupa il file DAVVERO: st_blocks == 0
// con st_size > 0 significa "nessun dato qui, va scaricato".
// Volutamente basata su stat() e non su NSURLUbiquitousItemDownloadingStatusKey:
// vale per QUALUNQUE file provider, non solo iCloud, e non lega questo file a
// Foundation.
static bool isDatalessFile(const QString &path)
{
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return false;              // nessun equivalente: OneDrive espone i segnaposto come reparse point
#else
    struct stat st;
    if (::stat(path.toUtf8().constData(), &st) != 0) return false;
    return st.st_size > 0 && st.st_blocks == 0;
#endif
}

// "discreteConstants": {"A": [2,6]} — la costante A assume solo i valori interi
// da 2 a 6 e il campo scatta all'intero piu' vicino quando l'utente rilascia lo
// slider o preme Enter. E' l'equivalente della direttiva "A := int(2,6);", ma
// dichiarato dal PRESET: le direttive := si leggono solo dallo scriptCode
// (parseAndApplyScriptParams), quindi un preset con le equazioni nel dock
// Equations e nessuno script non avrebbe modo di dichiararle.
// Chiave assente o malformata = nessuna costante discreta (tutte continue).
static void parseDiscreteConstants(const QJsonObject& root, LibraryItem& d)
{
    if (!root.contains("discreteConstants")) return;
    const QJsonObject disc = root["discreteConstants"].toObject();
    for (auto it = disc.constBegin(); it != disc.constEnd(); ++it) {
        const QJsonArray range = it.value().toArray();
        if (range.size() != 2) continue;             // ignora voci malformate
        int lo = range[0].toInt();
        int hi = range[1].toInt();
        if (lo > hi) std::swap(lo, hi);              // "[6,2]" tollerato
        d.discreteConstants.insert(it.key().toUpper(), qMakePair(lo, hi));
    }
}

// PARTI COMUNI al parsing di superfici e record: stesse chiavi, stessi
// default. E' lo specchio di LibraryManager::toJson (in fondo al file): una
// chiave che toJson scrive per entrambi i tipi si legge qui. Prima questo
// codice era copiato identico nei due rami di parseJson.
static void parseMeshParts(const QJsonObject &root, LibraryItem &d)
{
    // Aspetto per-mesh (opzionale). Ogni campo assente resta negativo, cioe'
    // "eredita dallo stato globale": un preset che personalizza solo il
    // colore di una parte non impone alpha o luce alle altre.
    if (!root.contains("meshParts")) return;
    const QJsonArray arr = root["meshParts"].toArray();
    d.meshParts.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        MeshPart mp;
        if (o.contains("r")) {
            mp.colorR = (float)o["r"].toDouble(-1.0);
            mp.colorG = (float)o["g"].toDouble(-1.0);
            mp.colorB = (float)o["b"].toDouble(-1.0);
        }
        if (o.contains("alpha"))      mp.alpha = (float)o["alpha"].toDouble(-1.0);
        if (o.contains("light"))      mp.lightIntensity = (float)o["light"].toDouble(-1.0);
        // Modalita' propria: senza la chiave la parte eredita, quindi i
        // preset salvati prima di questa feature restano identici.
        if (o.contains("mode")) {
            mp.renderMode = o["mode"].toInt(0);
            mp.hasCustomRenderMode = true;
        }
        if (o.contains("wfU"))        mp.wfStepU = o["wfU"].toInt(0);
        if (o.contains("wfV"))        mp.wfStepV = o["wfV"].toInt(0);
        // Dominio proprio della parte (campi u/v del pannello Multi Mesh).
        // Chiave assente = la parte usa il dominio dichiarato dalla sezione
        // //MESH_BEGIN, quindi i preset salvati prima restano identici.
        if (o.contains("uMin")) {
            mp.uMin = (float)o["uMin"].toDouble(0.0);
            mp.uMax = (float)o["uMax"].toDouble(0.0);
            mp.vMin = (float)o["vMin"].toDouble(0.0);
            mp.vMax = (float)o["vMax"].toDouble(0.0);
            mp.hasCustomDomain = true;
        }
        // Texture procedurale propria: come "mode", la chiave assente lascia
        // la parte a EREDITARE.
        if (o.contains("texCode")) {
            mp.textureCode = o["texCode"].toString();
            mp.textureLibName = o["texLibName"].toString().trimmed();
            mp.textureEnabled = o["texOn"].toBool(true);
            mp.hasCustomTexture = true;
        }
        if (o.contains("texC1r")) {
            mp.texCol1R = (float)o["texC1r"].toDouble(-1.0);
            mp.texCol1G = (float)o["texC1g"].toDouble(-1.0);
            mp.texCol1B = (float)o["texC1b"].toDouble(-1.0);
            mp.texCol2R = (float)o["texC2r"].toDouble(-1.0);
            mp.texCol2G = (float)o["texC2g"].toDouble(-1.0);
            mp.texCol2B = (float)o["texC2b"].toDouble(-1.0);
        }
        if (o.contains("texZoom")) {
            mp.texZoom = (float)o["texZoom"].toDouble(-1.0);
            mp.texPanX = (float)o["texPanX"].toDouble(0.0);
            mp.texPanY = (float)o["texPanY"].toDouble(0.0);
            mp.texRotation = (float)o["texRot"].toDouble(0.0);
        }
        d.meshParts.push_back(mp);
    }
}

static void parseSceneCommon(const QJsonObject &root, LibraryItem &d)
{
    if (root.contains("geodesic")) {
        QJsonObject geo = root["geodesic"].toObject();
        d.geoU0 = geo["u0"].toString();
        d.geoV0 = geo["v0"].toString();
        d.geoW0 = geo["w0"].toString();
        d.geoDU = geo["du"].toString();
        d.geoDV = geo["dv"].toString();
        d.geoDW = geo["dw"].toString();
        d.geoConform = geo["conform"].toString();
    }
    if (root.contains("isImplicitMode")) {
        d.isImplicitMode = root["isImplicitMode"].toBool();
        d.implicitEq = root["implicitEquation"].toString();
        // Sotto-tab implicito attivo al salvataggio. Chiavi assenti nei record
        // precedenti al Cross Section -> false/vuoto, cioe' il ramo 3D di sempre.
        d.shellThickness = (float)root["shellThickness"].toDouble(0.005);
        d.usesCrossSection = root["implicitUsesCrossSection"].toBool();
        d.crossSectionEq = root["crossSectionEquation"].toString();
        d.crossSectionP = (float)root["crossSectionP"].toDouble(0.0);
        // MARCHER. Chiave assente nei record precedenti ai radio: il default
        // dipende dal SOTTO-TAB, e non e' un vezzo di compatibilita'.
        //  - 3D -> "Fast" (sphere tracing storico): sono decine, si disegnano
        //    bene cosi', e il marcher preciso introduce difetti sui bordi di
        //    alcuni (misurato: Chain perde 4 pixel su 1681).
        //  - Cross Section -> "Precise": superfici 4D di grado alto, dove il
        //    marcher storico produce le "saldature" (sul T^3 il 22% degli hit era
        //    falso, a 0.10 unita' dalla superficie).
        // Chi ha la chiave usa il proprio valore, in entrambi i sotto-tab.
        d.hybridMarcher = root.contains("hybridMarcher")
                          ? root["hybridMarcher"].toBool()
                          : d.usesCrossSection;
    }
    // Numero di copie (campo Meshes): numero o espressione; assente = 1.
    if (root.contains("meshCount")) {
        const QJsonValue mc = root["meshCount"];
        d.meshCount = mc.isString() ? mc.toString() : QString::number(mc.toDouble(1.0), 'g', 12);
        if (d.meshCount.trimmed().isEmpty()) d.meshCount = QStringLiteral("1");
    }
    // Tubo: la chiave c'e' solo nelle scene del sotto-tab Tubes.
    if (root.contains("tube")) {
        const QJsonObject tube = root["tube"].toObject();
        d.isTube = true;
        d.tubeX = tube["x"].toString();
        d.tubeY = tube["y"].toString();
        d.tubeZ = tube["z"].toString();
        d.tubeP = tube["p"].toString();
        d.tubeThickness = tube["thickness"].toString();
    }
    // Messaggio in sovrimpressione (suggerimento d'uso). Vuoto = nessuno.
    if (root.contains("hintText")) {
        d.hintText = root["hintText"].toString();
        d.hintSeconds = (float)root["hintSeconds"].toDouble(6.0);
    }
    parseDiscreteConstants(root, d);
    if (root.contains("limits")) {
        QJsonObject l = root["limits"].toObject();
        d.uMin=l["uMin"].toDouble(); d.uMax=l["uMax"].toDouble();
        d.vMin=l["vMin"].toDouble(); d.vMax=l["vMax"].toDouble();
        d.wMin=l["wMin"].toDouble(); d.wMax=l["wMax"].toDouble();

        // Formule dei limiti (assenti nei record fino alla v1): se ci sono
        // vincono sul numero, che resta il fallback.
        d.uMinExpr=l["uMinExpr"].toString(); d.uMaxExpr=l["uMaxExpr"].toString();
        d.vMinExpr=l["vMinExpr"].toString(); d.vMaxExpr=l["vMaxExpr"].toString();
        d.wMinExpr=l["wMinExpr"].toString(); d.wMaxExpr=l["wMaxExpr"].toString();

        d.xMin=l["xMin"].toDouble(-1000.0); d.xMax=l["xMax"].toDouble(1000.0);
        d.yMin=l["yMin"].toDouble(-1000.0); d.yMax=l["yMax"].toDouble(1000.0);
        d.zMin=l["zMin"].toDouble(-1000.0); d.zMax=l["zMax"].toDouble(1000.0);
        d.xMinExpr=l["xMinExpr"].toString(); d.xMaxExpr=l["xMaxExpr"].toString();
        d.yMinExpr=l["yMinExpr"].toString(); d.yMaxExpr=l["yMaxExpr"].toString();
        d.zMinExpr=l["zMinExpr"].toString(); d.zMaxExpr=l["zMaxExpr"].toString();
    }
    d.steps = root["steps"].toInt(100);
    if (root.contains("constants")) {
        QJsonObject c = root["constants"].toObject();
        d.a=c["A"].toDouble(0.0); d.b=c["B"].toDouble(0.0); d.c=c["C"].toDouble(0.0);
        d.d=c["D"].toDouble(0.0); d.e=c["E"].toDouble(0.0); d.f=c["F"].toDouble(0.0);
        if (c.contains("S")) d.s = c["S"].toDouble(0.0);
    }
    if (root.contains("lightingMode")) {
        d.lightingMode = root["lightingMode"].toInt();
    }
    // Luce di riempimento: chiave assente -> 0 (spenta), il valore storico.
    d.fillLight = (float)root["fillLight"].toDouble(0.0);
    // Bordo: chiave assente -> nessun bordo, col colore di default.
    if (root.contains("border")) {
        const QJsonObject b = root["border"].toObject();
        d.borderThickness = (float)b["thickness"].toDouble(0.0);
        d.borderColor = QColor::fromRgbF(b["r"].toDouble(1.0), b["g"].toDouble(1.0),
                                         b["b"].toDouble(1.0));
        d.borderMode = b["mode"].toInt(0);
        d.borderWireAlong = b["wireAlong"].toInt(8);
        d.borderWireAround = b["wireAround"].toInt(2);
        if (b.contains("texture")) {
            const QJsonObject t = b["texture"].toObject();
            auto color = [](const QJsonValue &v, const QColor &def) {
                const QJsonArray a = v.toArray();
                return a.size() == 3 ? QColor::fromRgbF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble())
                                     : def;
            };
            d.borderTexCode = t["code"].toString();
            d.borderTexEnabled = t["enabled"].toBool(true);
            d.borderTexCol1 = color(t["col1"], d.borderTexCol1);
            d.borderTexCol2 = color(t["col2"], d.borderTexCol2);
            d.borderTexZoom = (float)t["zoom"].toDouble(1.0);
            d.borderTexPanX = (float)t["panX"].toDouble(0.0);
            d.borderTexPanY = (float)t["panY"].toDouble(0.0);
            d.borderTexRotation = (float)t["rotation"].toDouble(0.0);
            d.borderTexLibName = t["libName"].toString();
            d.borderTexHintText = t["hintText"].toString();
            d.borderTexHintSeconds = (float)t["hintSeconds"].toDouble(6.0);
        }
    }
    if (root.contains("lightIntensity")) {
        d.lightIntensity = root["lightIntensity"].toDouble(1.0);
    }
    if (root.contains("use4DLighting")) {
        d.use4DLighting = root["use4DLighting"].toBool();
        d.hasLightingState = true;
    }
    d.renderMode = root.contains("renderMode") ? root["renderMode"].toInt() : 0;
    if (root.contains("projectionMode")) {
        d.projectionMode = root["projectionMode"].toInt();
    }
    d.cameraFov = (float)root["cameraFov"].toDouble(45.0);
    // FOV indipendenti dei due path; i JSON vecchi (solo cameraFov) lo
    // ereditano su entrambi.
    d.fov3D = (float)root["fov3D"].toDouble(d.cameraFov);
    d.fov4D = (float)root["fov4D"].toDouble(d.cameraFov);

    // Densita' wireframe (opzionale): assente nei preset vecchi -> hasWireframe
    // resta false e il load applica il default. STEP_DEF=4 lato GLWidget clampa.
    if (root.contains("wireframe")) {
        QJsonObject wf = root["wireframe"].toObject();
        d.hasWireframe = true;
        d.wireframeUStep = wf["uStep"].toInt(4);
        d.wireframeVStep = wf["vStep"].toInt(4);
    }

    parseMeshParts(root, d);
    // Ambito All/Mesh salvato col preset (assente nei preset vecchi).
    d.meshScopeAll = root.contains("meshScopeAll") && root["meshScopeAll"].toBool(false);
    // Dominio dell'ambito "All" (assente nei preset che non l'hanno usato).
    if (root.contains("allUMin")) {
        d.hasAllDomain = true;
        d.allUMin = (float)root["allUMin"].toDouble(0.0);
        d.allUMax = (float)root["allUMax"].toDouble(0.0);
        d.allVMin = (float)root["allVMin"].toDouble(0.0);
        d.allVMax = (float)root["allVMax"].toDouble(0.0);
    }

    if (root.contains("camera3D")) {
        d.hasCamera3D = true;
        QJsonObject cam = root["camera3D"].toObject();
        d.camX = cam["x"].toDouble(0.0);
        d.camY = cam["y"].toDouble(0.0);
        d.camZ = cam["z"].toDouble(4.0);
        d.rotW = cam["rot_w"].toDouble(1.0);
        d.rotX = cam["rot_x"].toDouble(0.0);
        d.rotY = cam["rot_y"].toDouble(0.0);
        d.rotZ = cam["rot_z"].toDouble(0.0);
        d.camYaw = cam["yaw"].toDouble(0.0);
        d.camPitch = cam["pitch"].toDouble(0.0);
        d.camRoll = cam["roll"].toDouble(0.0);
    }
}

// Colore e trasparenza globali. Due formati storici per il colore:
//  - stringa "#rrggbb" in "surfColor" (record, saveScript);
//  - componenti numeriche r/g/b 0..1 (saveSurface, es. Ergosphere.json).
// Il reader leggeva solo il primo: i preset salvati col secondo restavano senza
// colore (default verde) e non trasparenti. color1 e' il nome che il load usa,
// surfaceColor il colore a piena precisione che il Save delle superfici scrive.
// (La vecchia chiave "bordColor" di preset legacy viene semplicemente ignorata.)
static void parseSurfaceColor(const QJsonObject &col, LibraryItem &d)
{
    if (col.contains("surfColor")) {
        d.color1 = col["surfColor"].toString();
        d.surfaceColor = QColor(d.color1);
    } else if (col.contains("r")) {
        d.surfaceColor = QColor::fromRgbF(col["r"].toDouble(), col["g"].toDouble(), col["b"].toDouble());
        d.color1 = d.surfaceColor.name();
    }
    if (col.contains("alpha")) d.alpha = col["alpha"].toDouble(1.0);
}

LibraryManager::LibraryManager() {}

void LibraryManager::clear()
{
    m_surfaces.clear();
    m_textures.clear();
    m_motions.clear();
    m_sounds.clear();
    // Senza questo si accumulerebbe a ogni refresh: refreshRepositories chiama
    // clear() e poi ricarica i quattro rami, quindi il conteggio mostrato
    // all'utente crescerebbe a ogni giro sugli stessi file.
    m_skippedDataless.clear();
}

const LibraryItem& LibraryManager::getSurface(int index) const {
    if (index >= 0 && index < m_surfaces.size()) return m_surfaces[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem* LibraryManager::getSurfaceByPath(const QString &filePath) const {
    for (const LibraryItem &it : m_surfaces) {
        if (it.filePath == filePath) return &it;
    }
    return nullptr;
}

const LibraryItem* LibraryManager::getMotionByPath(const QString &filePath) const {
    for (const LibraryItem &it : m_motions) {
        if (it.filePath == filePath) return &it;
    }
    return nullptr;
}

const LibraryItem& LibraryManager::getTexture(int index) const {
    if (index >= 0 && index < m_textures.size()) return m_textures[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem& LibraryManager::getMotion(int index) const {
    if (index >= 0 && index < m_motions.size()) return m_motions[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem& LibraryManager::getSound(int index) const {
    if (index >= 0 && index < m_sounds.size()) return m_sounds[index];
    static LibraryItem dummy; return dummy;
}

void LibraryManager::loadFromDirectory(const QString &dirPath, QTreeWidget *tree, LibraryType type)
{
    QString rootNameRaw = QFileInfo(dirPath).fileName();
    if (rootNameRaw.isEmpty()) rootNameRaw = QDir(dirPath).dirName();
    QString rootName = QUrl::fromPercentEncoding(rootNameRaw.toUtf8());

    QStringList validExtensions;
    if (type == LibraryType::Texture) {
        validExtensions << "json" << "png" << "jpg" << "jpeg" << "bmp";
    }
    // ---> FIX 1: Diciamo al programma di cercare anche i file audio! <---
    else if (type == LibraryType::Sound) {
        validExtensions << "json" << "mp3" << "wav" << "ogg";
    }
    else {
        validExtensions << "json";
    }

    QDirIterator it(dirPath, QStringList(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    if (tree->columnCount() < 1) tree->setColumnCount(1);

    while (it.hasNext()) {
        QString rawFullPath = it.next();
        QFileInfo fileInfo(rawFullPath);

        // Calcoliamo i percorsi puliti per navigare l'albero
        QString decodedFullPath = QUrl::fromPercentEncoding(rawFullPath.toUtf8());
        QString cleanFull = QDir::cleanPath(decodedFullPath);
        QStringList pathParts = cleanFull.split('/', Qt::SkipEmptyParts);

        int rootIndex = -1;
        for (int i = pathParts.size() - 2; i >= 0; --i) {
            if (pathParts[i] == rootName) {
                rootIndex = i;
                break;
            }
        }

        // --- A. GESTIONE CARTELLE (Se è una directory) ---
        if (fileInfo.isDir()) {
            if (rootIndex != -1) {
                QTreeWidgetItem *parentNode = nullptr;
                for (int i = rootIndex + 1; i < pathParts.size(); ++i) {
                    parentNode = getOrCreateSubCategory(tree, parentNode, pathParts[i]);

                    QString segmentPath = QStringList(pathParts.mid(0, i + 1)).join("/");
#ifdef Q_OS_WIN
                    // Su Windows, se mid(0) include la lettera drive, va bene.
#else
                    if (!segmentPath.startsWith("/")) segmentPath.prepend("/");
#endif
                    parentNode->setData(0, Qt::UserRole + 10, segmentPath);
                }
            }
            continue;
        }

        // --- B. GESTIONE FILE (Se è un file valido) ---
        if (!validExtensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
            continue;
        }

        // NON MATERIALIZZATO: si salta PRIMA di qualunque apertura. Vale per
        // tutti i tipi, non solo i JSON: un .wav o un .png in iCloud blocca la
        // lettura esattamente allo stesso modo. Vedi isDatalessFile in cima al
        // file per il perche' una read() qui non tornerebbe mai.
        // Si contano per poterlo DIRE all'utente: saltarli in silenzio
        // riprodurrebbe il vecchio sintomo "la libreria e' incompleta e non si
        // capisce perche'".
        if (isDatalessFile(rawFullPath)) {
            m_skippedDataless.append(rawFullPath);
            continue;
        }

        QList<LibraryItem> *targetList = nullptr;
        if (type == LibraryType::Surface) targetList = &m_surfaces;
        else if (type == LibraryType::Texture) targetList = &m_textures;
        else if (type == LibraryType::Motion) targetList = &m_motions;
        else if (type == LibraryType::Sound) targetList = &m_sounds;

        bool alreadyLoaded = false;
        for (const auto &item : *targetList) {
            if (item.filePath == rawFullPath) {
                alreadyLoaded = true;
                break;
            }
        }
        if (alreadyLoaded) continue;

        LibraryItem item;
        item.filePath = rawFullPath;
        item.type = type;
        bool valid = false;

        if (rawFullPath.endsWith(".json", Qt::CaseInsensitive)) {
            item = parseJson(rawFullPath, type);
            if (!item.name.isEmpty()) valid = true;
        }
        else if (type == LibraryType::Texture) {
            item.name = QFileInfo(rawFullPath).baseName();
            item.isImage = true;
            valid = true;
        }
        // ---> FIX 2: Creiamo l'oggetto in memoria col nome del brano <---
        else if (type == LibraryType::Sound) {
            item.name = QFileInfo(rawFullPath).baseName(); // Rimuove l'estensione per pulizia visiva
            valid = true;
        }

        if (!valid) continue;

        targetList->append(item);
        int listIndex = targetList->size() - 1;

        // Costruzione Albero per il FILE
        if (rootIndex != -1 && rootIndex < pathParts.size() - 1) {
            QTreeWidgetItem *parentNode = nullptr;
            for (int i = rootIndex + 1; i < pathParts.size() - 1; ++i) {
                parentNode = getOrCreateSubCategory(tree, parentNode, pathParts[i]);

                QString segmentPath = QStringList(pathParts.mid(0, i + 1)).join("/");
#ifndef Q_OS_WIN
                if (!segmentPath.startsWith("/")) segmentPath.prepend("/");
#endif
                parentNode->setData(0, Qt::UserRole + 10, segmentPath);
            }

            QTreeWidgetItem *fileItem = new QTreeWidgetItem();
            QString displayText = item.name;
            if (type == LibraryType::Texture && item.isImage) displayText = "[IMG] " + item.name;
            // ---> FIX 3: Aggiunge la scritta [AUDIO] prima del nome nell'albero <---
            else if (type == LibraryType::Sound && !rawFullPath.endsWith(".json", Qt::CaseInsensitive)) displayText = "[AUDIO] " + item.name;

            fileItem->setText(0, displayText);
            fileItem->setToolTip(0, item.filePath);

            int roleOffset = 0;
            if (type == LibraryType::Surface) roleOffset = 0;
            else if (type == LibraryType::Texture) roleOffset = 1;
            else if (type == LibraryType::Motion) roleOffset = 2;
            else if (type == LibraryType::Sound) roleOffset = 3;

            fileItem->setData(0, Qt::UserRole + roleOffset, listIndex);

            if (parentNode) parentNode->addChild(fileItem);
            else tree->addTopLevelItem(fileItem);
        } else {
            // Caso file nella root
            QTreeWidgetItem *fileItem = new QTreeWidgetItem();
            QString displayText = item.name;
            if (type == LibraryType::Texture && item.isImage) displayText = "[IMG] " + item.name;
            // ---> FIX 3: Ripetuto per i file nella radice principale <---
            else if (type == LibraryType::Sound && !rawFullPath.endsWith(".json", Qt::CaseInsensitive)) displayText = "[AUDIO] " + item.name;

            fileItem->setText(0, displayText);
            fileItem->setToolTip(0, item.filePath);

            int roleOffset = 0;
            if (type == LibraryType::Surface) roleOffset = 0;
            else if (type == LibraryType::Texture) roleOffset = 1;
            else if (type == LibraryType::Motion) roleOffset = 2;
            else if (type == LibraryType::Sound) roleOffset = 3;

            fileItem->setData(0, Qt::UserRole + roleOffset, listIndex);
            tree->addTopLevelItem(fileItem);
        }
    }
    tree->sortItems(0, Qt::AscendingOrder);
}

QTreeWidgetItem* LibraryManager::getOrCreateSubCategory(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name)
{
    int childCount = (parent) ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* item = (parent) ? parent->child(i) : tree->topLevelItem(i);
        if (item->text(0) == name) return item;
    }

    QTreeWidgetItem* newItem = new QTreeWidgetItem();
    newItem->setText(0, name);
    newItem->setData(0, Qt::UserRole, QVariant());

    if (parent) parent->addChild(newItem);
    else tree->addTopLevelItem(newItem);

    newItem->setExpanded(false);
    return newItem;
}

LibraryItem LibraryManager::parseJson(const QString &filePath, LibraryType type)
{
    LibraryItem d;
    d.filePath = filePath;
    d.type = type;
    d.name = QFileInfo(filePath).baseName();

    // Seconda guardia, oltre a quella in loadFromDirectory: parseJson e' privata
    // ma resta l'unico punto che legge davvero, e un domani potrebbe essere
    // chiamata da un percorso che non passa dal filtro. Il costo e' una stat().
    if (isDatalessFile(filePath)) { d.name = ""; return d; }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return d;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull()) return d;
    QJsonObject root = doc.object();
    QString jsonType = root["type"].toString();

    // --- MOTION PARSING ---
    if (type == LibraryType::Motion) {
        if (jsonType != "motion") { d.name = ""; return d; }

        parseSceneCommon(root, d);

        if (root.contains("equations")) {
            QJsonObject eq = root["equations"].toObject();
            d.x = eq["x"].toString(); d.y = eq["y"].toString(); d.z = eq["z"].toString(); d.w = eq["p"].toString();
            d.explicitW = eq["explicitW"].toString();
            d.explicitU = eq["explicitU"].toString();
            d.explicitV = eq["explicitV"].toString();
            d.defU = eq["defU"].toString();
            d.defV = eq["defV"].toString();
            d.defW = eq["defW"].toString();
        }
        if (root.contains("scriptCode")) {
            d.isScript = true;
            d.scriptCode = root["scriptCode"].toString();
        }
        if (root.contains("colors")) {
            parseSurfaceColor(root["colors"].toObject(), d);
            d.hasCustomColors = true;
        }
        if (root.contains("angles")) {
            QJsonObject a = root["angles"].toObject();
            d.startOmega = a["omega"].toDouble(0.0);
            d.startPhi   = a["phi"].toDouble(0.0);
            d.startPsi   = a["psi"].toDouble(0.0);
            d.restoreAngles = true;
        }

        // ------------------------------------------------------------------
        // SOLO RECORD: moti, path, texture, sfondo, suono.
        // I default sono quelli con cui applyMotionExample legge oggi le stesse
        // chiavi direttamente dal file: quando il load passera' da qui (tappa 3
        // dello "stato unico della scena") non deve cambiare nulla.
        // ------------------------------------------------------------------
        if (root.contains("speeds")) {
            QJsonObject s = root["speeds"].toObject();
            d.speedNut = s["nutation"].toDouble(); d.speedPrec = s["precession"].toDouble(); d.speedSpin = s["spin"].toDouble();
            d.speedOmega = s["omega"].toDouble(); d.speedPhi = s["phi"].toDouble(); d.speedPsi = s["psi"].toDouble();
            d.speedPath3D = s["path3D"].toInt();   // chiave assente -> 0, come il load
            d.speedPath4D = s["path4D"].toInt();
        }
        if (root.contains("path4D")) {
            QJsonObject p4 = root["path4D"].toObject();
            d.path4D_x = p4["x"].toString();
            d.path4D_y = p4["y"].toString();
            d.path4D_z = p4["z"].toString();
            d.path4D_w = p4["w"].toString();
            d.path4D_alpha = p4["alpha"].toString();
            d.path4D_beta  = p4["beta"].toString();
            d.path4D_gamma = p4["gamma"].toString();
        }
        if (root.contains("path3D")) {
            QJsonObject p3 = root["path3D"].toObject();
            d.path3D_x = p3["x"].toString();
            d.path3D_y = p3["y"].toString();
            d.path3D_z = p3["z"].toString();
            d.path3D_roll = p3["roll"].toString();
        }
        // Vista dei due path: i record col solo "pathMode" (formato storico) la
        // applicano a entrambi; senza nessuna delle due chiavi, Tangent (0).
        if (root.contains("pathMode")) {
            d.pathMode4D = root["pathMode"].toInt();
            d.pathMode3D = root.contains("pathMode3D") ? root["pathMode3D"].toInt() : d.pathMode4D;
        }
        // Moto camera da riavviare. "none" resta "none": lo interpreta il load.
        d.activeMotion = root["activeMotion"].toString();
        d.observer4D = (float)root["observer4D"].toDouble(4.0);
        // Il record porta DUE messaggi: quello della scena (hintText, comune) e
        // quello della sua TEXTURE. I record salvati prima hanno solo il primo.
        if (root.contains("textureHintText")) {
            d.textureHintText = root["textureHintText"].toString();
            d.textureHintSeconds = (float)root["textureHintSeconds"].toDouble(6.0);
        }
        if (root.contains("background")) {
            QJsonObject bg = root["background"].toObject();
            d.bgTextureEnabled = bg["enabled"].toBool();
            d.bgTextureCode = bg["code"].toString();
            if (bg.contains("color")) d.bgColor = bg["color"].toString();
            if (bg.contains("col1")) d.bgCol1 = bg["col1"].toString();
            if (bg.contains("col2")) d.bgCol2 = bg["col2"].toString();
            d.bgLibName = bg.value("libName").toString().trimmed();
            d.bgHintText = bg.value("hintText").toString().trimmed();
            d.bgHintSeconds = (float)bg.value("hintSeconds").toDouble(6.0);
            if (bg.contains("zoom")) d.bgZoom = bg["zoom"].toDouble(1.0);
            if (bg.contains("pan_x")) d.bgPanX = bg["pan_x"].toDouble(0.0);
            if (bg.contains("pan_y")) d.bgPanY = bg["pan_y"].toDouble(0.0);
            if (bg.contains("rotation")) d.bgRotation = bg["rotation"].toDouble(0.0);
            // Assente nei record salvati prima: sfondo fisso.
            const QString sky = bg.value("skyMode").toString().trimmed();
            if (!sky.isEmpty()) d.bgSkyMode = sky;
        }
        if (root.contains("texture")) {
            QJsonObject tex = root["texture"].toObject();
            d.textureEnabled = tex["enabled"].toBool();
            if (tex.contains("displacement")) {
                d.displacementCode = tex["displacement"].toString();
            }
            if (tex.contains("zoom")) d.zoom = tex["zoom"].toDouble(1.0);
            if (tex.contains("pan_x")) d.panX = tex["pan_x"].toDouble(0.0);
            if (tex.contains("pan_y")) d.panY = tex["pan_y"].toDouble(0.0);
            if (tex.contains("rotation")) d.rotation = tex["rotation"].toDouble(0.0);

            // Colori
            if (tex.contains("col1") && tex.contains("col2")) {
                d.texColor1 = tex["col1"].toString();
                d.texColor2 = tex["col2"].toString();
            }

            // Codice (Texture Script o IMG path)
            if (tex.contains("code")) {
                d.textureCode = tex["code"].toString();
                // Importante: se c'è codice o path immagine, è "custom"
                d.isTextureCustom = !d.textureCode.isEmpty();
            }
            // Ancora in libreria della texture (focus anche a codice cambiato).
            d.textureLibName = tex.value("libName").toString().trimmed();
        }
        // Ancora del suono: vuota nei record salvati prima che esistesse.
        d.soundLibName = root.value("soundLibName").toString().trimmed();

        return d;
    }

    // --- SOUND ---
    if (type == LibraryType::Sound) {
        if (root.contains("code")) {
            d.isScript = true;
            d.scriptCode = root["code"].toString();
        }
        return d;
    }

    // --- TEXTURE ---
    if (type == LibraryType::Texture) {
        if (root.contains("equations") || root.contains("scriptCode") || jsonType == "motion") { d.name = ""; return d; }
        if (root.contains("isImplicitMode")) {
            d.isImplicitMode = root["isImplicitMode"].toBool();
        }
        // Messaggio opzionale in sovrimpressione, stesse chiavi dei rami Motion e
        // Surface. Serve alle texture che usano le costanti A..F/S: gli slider si
        // sbloccano da soli (updateConstantsUIState legge il codice), ma nulla
        // dice a COSA servono in QUESTA texture -- l'hint e' il posto dove
        // scriverlo, com'e' gia' per le superfici.
        if (root.contains("hintText")) {
            d.hintText = root["hintText"].toString();
            d.hintSeconds = (float)root["hintSeconds"].toDouble(6.0);
        }
        if (root.contains("displacement")) {
            d.displacementCode = root["displacement"].toString();
        }
        if (root.contains("code")) {
            d.scriptCode = root["code"].toString();
            d.textureCode = d.scriptCode;
            d.isTextureCustom = true; d.isImage = false;

            // Una texture-immagine viene salvata come JSON col path della PNG nel
            // tag "//IMG:<path>" in cima al code. Va riconosciuta qui, altrimenti
            // isImage resta false -> al load il ramo immagine non parte, la
            // protezione anti-cambio-modo salta e la scena collassa in RM default.
            QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatch imgMatch = imgRe.match(d.scriptCode);
            if (imgMatch.hasMatch()) {
                QString imgPath = imgMatch.captured(1).trimmed();
                if (!imgPath.isEmpty()) {
                    d.isImage = true;
                    d.imagePath = imgPath;
                }
            }

            if (root.contains("zoom")) d.zoom = root["zoom"].toDouble(1.0);
            if (root.contains("pan_x")) d.panX = root["pan_x"].toDouble(0.0);
            if (root.contains("pan_y")) d.panY = root["pan_y"].toDouble(0.0);
            if (root.contains("rotation")) d.rotation = root["rotation"].toDouble(0.0);
            if (root.contains("color1") && root.contains("color2")) {
                d.hasCustomColors = true;
                d.color1 = root["color1"].toString(); d.color2 = root["color2"].toString();
                d.texColor1 = d.color1; d.texColor2 = d.color2;
            }
        }
        return d;
    }

    // --- SURFACE ---
    else {
        // Controllo di sicurezza sul tipo
        if (jsonType == "custom_texture" || jsonType == "motion") { d.name = ""; return d; }

        parseSceneCommon(root, d);

        // Colore di sfondo: assente nelle Shape salvate col grigio di default.
        if (root.contains("background")) {
            const QJsonObject bg = root["background"].toObject();
            if (bg.contains("color")) d.bgColor = bg["color"].toString();
        }

        // Caso 1: È uno SCRIPT
        if (root.contains("scriptCode")) {
            d.isScript = true;
            d.scriptCode = root["scriptCode"].toString();

            // Carichiamo anche i parametri UI di backup se presenti
            if (root.contains("equations")) {
                QJsonObject eq = root["equations"].toObject();
                d.x = eq["x"].toString();
                d.y = eq["y"].toString();
                d.z = eq["z"].toString();
                d.w = eq["p"].toString();
            }
            // Mappa di visualizzazione custom di uno script metrico (Flamm ecc.)
            if (root.contains("metricDisplayMap")) {
                QJsonObject map = root["metricDisplayMap"].toObject();
                d.hasMetricMap = true;
                d.metricMapX = map["x"].toString();
                d.metricMapY = map["y"].toString();
                d.metricMapZ = map["z"].toString();
                d.metricMapP = map["p"].toString();
            }
        }
        // Caso 2: È una SUPERFICIE PARAMETRICA
        else if (root.contains("equations")) {
            d.isScript = false;
            QJsonObject eq = root["equations"].toObject();
            d.x = eq["x"].toString();
            d.y = eq["y"].toString();
            d.z = eq["z"].toString();
            d.w = eq["p"].toString();

            // --- AGGIUNTA FONDAMENTALE PER I VINCOLI ---
            d.explicitW = eq["explicitW"].toString();
            d.explicitU = eq["explicitU"].toString();
            d.explicitV = eq["explicitV"].toString();
            d.defU = eq["defU"].toString();
            d.defV = eq["defV"].toString();
            d.defW = eq["defW"].toString();
        }
        else { d.name = ""; return d; }

        // COLORE + TRASPARENZA della superficie. Questo ramo prima IGNORAVA del
        // tutto "colors": ogni superficie si ricaricava verde di default e opaca.
        // applySurfaceExample riapplica d.color1/d.alpha dopo il reset.
        if (root.contains("colors")) {
            parseSurfaceColor(root["colors"].toObject(), d);
            d.hasCustomColors = !d.color1.isEmpty();
        }

        if (root.contains("angles")) {
            // Preset Nuovi
            QJsonObject a = root["angles"].toObject();
            d.startOmega = a["omega"].toDouble(0.0);
            d.startPhi   = a["phi"].toDouble(0.0);
            d.startPsi   = a["psi"].toDouble(0.0);
            d.restoreAngles = true;
        } else {
            // Fallback per Preset Vecchi
            d.startOmega = root["omega"].toDouble(0.0);
            d.startPhi   = root["phi"].toDouble(0.0);
            d.startPsi   = root["psi"].toDouble(0.0);
            if (d.startOmega != 0.0 || d.startPhi != 0.0 || d.startPsi != 0.0) {
                d.restoreAngles = true;
            }
        }
        return d;
    }
}

DeletionBackup LibraryManager::softDelete(int index, LibraryType type)
{
    DeletionBackup backup;
    backup.isValid = false; // Default: azione fallita

    QList<LibraryItem> *targetList = nullptr;
    if (type == LibraryType::Surface) targetList = &m_surfaces;
    else if (type == LibraryType::Texture) targetList = &m_textures;
    else if (type == LibraryType::Motion) targetList = &m_motions;
    else if (type == LibraryType::Sound) targetList = &m_sounds;

    // Controlli di sicurezza standard
    if (!targetList || index < 0 || index >= targetList->size()) return backup;

    // Recuperiamo l'elemento
    LibraryItem item = (*targetList)[index];

    if (item.filePath.startsWith(":")) {
        return backup;
    }

    backup.data = item;
    backup.originalPath = backup.data.filePath;

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // --- MOBILE LOGIC (iOS/Android): Spostamento nel cestino interno ---
    QSettings settings;
    QString trashDir = settings.value("libraryRootPath").toString() + "/.trash";
    QDir().mkpath(trashDir);

    QString fileName = QFileInfo(backup.originalPath).fileName();
    QString internalTrashPath = trashDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_del_" + fileName;

    if (QFile::rename(backup.originalPath, internalTrashPath)) {
        backup.isValid = true;
        backup.backupPath = internalTrashPath;
        targetList->removeAt(index);
    }
#else
    // --- DESKTOP LOGIC: Cestino di sistema originale ---
    QString pathInTrash;
    if (QFile::moveToTrash(backup.originalPath, &pathInTrash)) {
        backup.isValid = true;
        backup.backupPath = pathInTrash;
        targetList->removeAt(index);
    }
#endif

    return backup;
}

bool LibraryManager::restore(const DeletionBackup &backup)
{
    if (!backup.isValid) return false;

    if (QFile::exists(backup.originalPath)) {
        QFile::remove(backup.originalPath);
    }

    QFile file(backup.backupPath);

    if (file.rename(backup.originalPath)) {
        if (backup.data.type == LibraryType::Surface) m_surfaces.append(backup.data);
        else if (backup.data.type == LibraryType::Texture) m_textures.append(backup.data);
        else if (backup.data.type == LibraryType::Motion) m_motions.append(backup.data);
        else if (backup.data.type == LibraryType::Sound) m_sounds.append(backup.data);
        return true;
    }
    return false;
}

bool LibraryManager::moveFile(const QString &oldPath, const QString &newFolder)
{
    QFile file(oldPath);
    if (!file.exists()) return false;
    QString fileName = QFileInfo(oldPath).fileName();
    QString newPath = newFolder + "/" + fileName;
    if (newPath == oldPath) return false;
    if (QFile::exists(newPath)) return false;
    return file.rename(newPath);
}

// ==========================================================
// SERIALIZZAZIONE: LibraryItem -> JSON (inverso di parseJson)
// ==========================================================
//
// Traduzione PURA: nessuna lettura di interfaccia o motore. Lo stato arriva gia'
// fotografato da PresetSerializer::capture*State, che prende le decisioni
// (script o equazioni, angoli 4D azzerati fuori dal Cross Section, audio e tag
// //IMG: dentro il codice della texture...). Qui restano solo le regole del
// FORMATO: quali chiavi esistono e quando si scrivono. Le superfici e i record
// hanno formati diversi (equazioni ridotte nel ramo script, colore r/g/b contro
// nome, texture per-mesh solo nei record): li distingue d.type.
//
// I numeri: dove il vecchio Save scriveva "(double)float" il campo e' float
// (conversione esatta); dove scriveva un double calcolato (alpha, luce) il
// campo e' double. Cosi' un file risalvato resta identico byte per byte.

void LibraryManager::writeParametricLimits(const LibraryItem &d, QJsonObject &limits)
{
    // Numero sempre, forma testuale solo quando e' una formula (vedi
    // PresetSerializer::captureParametricLimits).
    const struct { const char *key; float value; const QString &expr; } l[] = {
        { "uMin", d.uMin, d.uMinExpr }, { "uMax", d.uMax, d.uMaxExpr },
        { "vMin", d.vMin, d.vMinExpr }, { "vMax", d.vMax, d.vMaxExpr },
        { "wMin", d.wMin, d.wMinExpr }, { "wMax", d.wMax, d.wMaxExpr },
    };
    for (const auto &f : l) {
        limits[f.key] = f.value;
        if (!f.expr.isEmpty()) limits[QString(f.key) + "Expr"] = f.expr;
    }
}

void LibraryManager::writeSpaceLimits(const LibraryItem &d, QJsonObject &limits)
{
    const struct { const char *key; float value; const QString &expr; } l[] = {
        { "xMin", d.xMin, d.xMinExpr }, { "xMax", d.xMax, d.xMaxExpr },
        { "yMin", d.yMin, d.yMinExpr }, { "yMax", d.yMax, d.yMaxExpr },
        { "zMin", d.zMin, d.zMinExpr }, { "zMax", d.zMax, d.zMaxExpr },
    };
    for (const auto &f : l) {
        limits[f.key] = f.value;
        if (!f.expr.isEmpty()) limits[QString(f.key) + "Expr"] = f.expr;
    }
}

namespace {

QJsonObject meshPartsJson(const LibraryItem &d, bool withTextures, bool *anyCustom)
{
    // Solo cio' che la parte personalizza: una parte che eredita non scrive
    // nulla. Le superfici NON portano texture per-mesh (e nemmeno quella
    // globale): la texture sta nel ramo records/, che salva la scena intera.
    QJsonArray arr;
    *anyCustom = false;
    for (const MeshPart &mp : d.meshParts) {
        QJsonObject o;
        if (mp.hasCustomColor()) {
            o["r"] = (double)mp.colorR;
            o["g"] = (double)mp.colorG;
            o["b"] = (double)mp.colorB;
            *anyCustom = true;
        }
        if (mp.alpha >= 0.0f)          { o["alpha"] = (double)mp.alpha; *anyCustom = true; }
        if (mp.lightIntensity >= 0.0f) { o["light"] = (double)mp.lightIntensity; *anyCustom = true; }
        if (mp.hasCustomRenderMode)    { o["mode"] = mp.renderMode; *anyCustom = true; }
        if (mp.wfStepU > 0)            { o["wfU"] = mp.wfStepU; *anyCustom = true; }
        if (mp.wfStepV > 0)            { o["wfV"] = mp.wfStepV; *anyCustom = true; }
        if (withTextures) {
            if (mp.hasCustomTexture) {
                o["texCode"] = mp.textureCode;
                o["texOn"]   = mp.textureEnabled;
                if (!mp.textureLibName.isEmpty()) o["texLibName"] = mp.textureLibName;
                *anyCustom = true;
            }
            if (mp.hasCustomTexColors()) {
                o["texC1r"] = (double)mp.texCol1R;
                o["texC1g"] = (double)mp.texCol1G;
                o["texC1b"] = (double)mp.texCol1B;
                o["texC2r"] = (double)mp.texCol2R;
                o["texC2g"] = (double)mp.texCol2G;
                o["texC2b"] = (double)mp.texCol2B;
                *anyCustom = true;
            }
            if (mp.hasCustomTexTransform()) {
                o["texZoom"] = (double)mp.texZoom;
                o["texPanX"] = (double)mp.texPanX;
                o["texPanY"] = (double)mp.texPanY;
                o["texRot"]  = (double)mp.texRotation;
                *anyCustom = true;
            }
        }
        if (mp.hasCustomDomain) {
            o["uMin"] = (double)mp.uMin;
            o["uMax"] = (double)mp.uMax;
            o["vMin"] = (double)mp.vMin;
            o["vMax"] = (double)mp.vMax;
            *anyCustom = true;
        }
        arr.append(o);
    }
    QJsonObject holder;
    holder["meshParts"] = arr;
    return holder;
}

} // namespace

QJsonObject LibraryManager::toJson(const LibraryItem &d)
{
    const bool record = (d.type == LibraryType::Motion);
    QJsonObject root;
    root["name"] = d.name;
    root["type"] = record ? "motion" : "surface";

    // --- Ray Marching: i due sotto-tab hanno editor separati, si scrivono
    // entrambi piu' quale era attivo.
    root["isImplicitMode"] = d.isImplicitMode;
    if (d.isImplicitMode) {
        root["implicitEquation"] = d.implicitEq;
        root["implicitUsesCrossSection"] = d.usesCrossSection;
        root["crossSectionEquation"] = d.crossSectionEq;
    }

    // --- Numero di copie (campo Meshes): solo se diverso da 1, cosi' i preset
    // di una mesh sola restano identici.
    if (!d.meshCount.trimmed().isEmpty() && d.meshCount.trimmed() != QLatin1String("1"))
        root["meshCount"] = d.meshCount.trimmed();

    // --- Tubo (sotto-tab Tubes): la curva asse e lo spessore.
    if (d.isTube) {
        QJsonObject tube;
        tube["x"] = d.tubeX;
        tube["y"] = d.tubeY;
        tube["z"] = d.tubeZ;
        tube["p"] = d.tubeP;
        tube["thickness"] = d.tubeThickness;
        root["tube"] = tube;
    }

    // --- Geometria: script o equazioni.
    if (!record && d.isScript) {
        // Superficie da script: equazioni ridotte a x/y/z/p vuoti.
        if (!d.scriptCode.trimmed().isEmpty()) root["scriptCode"] = d.scriptCode;
        QJsonObject eq; eq["x"] = ""; eq["y"] = ""; eq["z"] = ""; eq["p"] = "";
        root["equations"] = eq;
    } else {
        QJsonObject eq;
        eq["x"] = d.x; eq["y"] = d.y; eq["z"] = d.z; eq["p"] = d.w;
        eq["explicitU"] = d.explicitU; eq["explicitV"] = d.explicitV; eq["explicitW"] = d.explicitW;
        eq["defU"] = d.defU; eq["defV"] = d.defV; eq["defW"] = d.defW;
        root["equations"] = eq;
        // Record: lo script si scrive ACCANTO alle equazioni (script metrico:
        // i campi portano la display map).
        if (record && d.isScript) root["scriptCode"] = d.scriptCode;
    }

    QJsonObject geo;
    geo["u0"] = d.geoU0; geo["v0"] = d.geoV0; geo["w0"] = d.geoW0;
    geo["du"] = d.geoDU; geo["dv"] = d.geoDV; geo["dw"] = d.geoDW;
    geo["conform"] = d.geoConform;
    root["geodesic"] = geo;

    if (!record && d.hasMetricMap) {
        QJsonObject map;
        map["x"] = d.metricMapX; map["y"] = d.metricMapY;
        map["z"] = d.metricMapZ; map["p"] = d.metricMapP;
        root["metricDisplayMap"] = map;
    }

    QJsonObject constants;
    constants["A"] = d.a; constants["B"] = d.b; constants["C"] = d.c;
    constants["D"] = d.d; constants["E"] = d.e; constants["F"] = d.f;
    constants["S"] = d.s;
    root["constants"] = constants;

    if (!d.discreteConstants.isEmpty()) {
        QJsonObject disc;
        for (auto it = d.discreteConstants.constBegin(); it != d.discreteConstants.constEnd(); ++it)
            disc[it.key()] = QJsonArray{ it->first, it->second };
        root["discreteConstants"] = disc;
    }

    QJsonObject limits;
    writeParametricLimits(d, limits);
    writeSpaceLimits(d, limits);
    root["limits"] = limits;
    root["steps"] = d.steps;

    QJsonObject colors;
    if (record) {
        colors["surfColor"] = d.color1;
    } else {
        colors["r"] = d.surfaceColor.redF();
        colors["g"] = d.surfaceColor.greenF();
        colors["b"] = d.surfaceColor.blueF();
    }
    colors["alpha"] = d.alpha;
    root["colors"] = colors;

    if (record) {
        QJsonObject p4;
        p4["x"] = d.path4D_x; p4["y"] = d.path4D_y; p4["z"] = d.path4D_z; p4["w"] = d.path4D_w;
        p4["alpha"] = d.path4D_alpha; p4["beta"] = d.path4D_beta; p4["gamma"] = d.path4D_gamma;
        root["path4D"] = p4;
        QJsonObject p3;
        p3["x"] = d.path3D_x; p3["y"] = d.path3D_y; p3["z"] = d.path3D_z; p3["roll"] = d.path3D_roll;
        root["path3D"] = p3;
    }

    // Messaggi in sovrimpressione: chiave assente se non c'e' nulla da dire.
    if (!d.hintText.isEmpty()) {
        root["hintText"] = d.hintText;
        root["hintSeconds"] = (double)d.hintSeconds;
    }
    if (record && !d.textureHintText.isEmpty()) {
        root["textureHintText"] = d.textureHintText;
        root["textureHintSeconds"] = (double)d.textureHintSeconds;
    }

    if (record) {
        root["pathMode"] = d.pathMode4D;
        root["pathMode3D"] = d.pathMode3D;
        root["activeMotion"] = d.activeMotion;

        QJsonObject tex;
        tex["enabled"] = d.textureEnabled;
        tex["zoom"] = (double)d.zoom;
        tex["pan_x"] = (double)d.panX;
        tex["pan_y"] = (double)d.panY;
        tex["rotation"] = (double)d.rotation;
        tex["col1"] = d.texColor1;
        tex["col2"] = d.texColor2;
        tex["code"] = d.textureCode;
        if (d.isImplicitMode) tex["displacement"] = d.displacementCode;
        if (!d.textureLibName.isEmpty()) tex["libName"] = d.textureLibName;
        root["texture"] = tex;
        if (!d.soundLibName.isEmpty()) root["soundLibName"] = d.soundLibName;

        QJsonObject speeds;
        speeds["nutation"] = (double)d.speedNut;
        speeds["precession"] = (double)d.speedPrec;
        speeds["spin"] = (double)d.speedSpin;
        speeds["omega"] = (double)d.speedOmega;
        speeds["phi"] = (double)d.speedPhi;
        speeds["psi"] = (double)d.speedPsi;
        speeds["path3D"] = d.speedPath3D;
        speeds["path4D"] = d.speedPath4D;
        root["speeds"] = speeds;
    }

    // Angoli 4D: si scrivono sempre (restoreAngles decide solo la lettura dei
    // preset molto vecchi, senza la chiave).
    QJsonObject angles;
    angles["omega"] = (double)d.startOmega;
    angles["phi"] = (double)d.startPhi;
    angles["psi"] = (double)d.startPsi;
    root["angles"] = angles;
    // Piano di sezione lungo p: solo nel sotto-tab Cross Section.
    if (d.isImplicitMode && d.usesCrossSection)
        root["crossSectionP"] = (double)d.crossSectionP;

    if (d.hasCamera3D) {
        QJsonObject cam;
        cam["x"] = (double)d.camX; cam["y"] = (double)d.camY; cam["z"] = (double)d.camZ;
        cam["rot_w"] = (double)d.rotW; cam["rot_x"] = (double)d.rotX;
        cam["rot_y"] = (double)d.rotY; cam["rot_z"] = (double)d.rotZ;
        cam["yaw"] = (double)d.camYaw; cam["pitch"] = (double)d.camPitch; cam["roll"] = (double)d.camRoll;
        root["camera3D"] = cam;
        if (record) root["observer4D"] = (double)d.observer4D;
    }

    if (!record && !d.bgColor.isEmpty()
        && QColor(d.bgColor).name() != defaultBackgroundColor().name()) {
        // Shape: dello sfondo solo il colore (la texture di sfondo e' dei
        // record), e solo se non e' il grigio di default.
        QJsonObject bg;
        bg["color"] = d.bgColor;
        root["background"] = bg;
    }
    if (record) {
        QJsonObject bg;
        bg["color"] = d.bgColor;
        bg["enabled"] = d.bgTextureEnabled;
        bg["code"] = d.bgTextureCode;
        bg["col1"] = d.bgCol1;
        bg["col2"] = d.bgCol2;
        if (!d.bgLibName.isEmpty()) bg["libName"] = d.bgLibName;
        if (!d.bgHintText.isEmpty()) {
            bg["hintText"] = d.bgHintText;
            bg["hintSeconds"] = (double)d.bgHintSeconds;
        }
        bg["skyMode"] = d.bgSkyMode;
        bg["zoom"] = (double)d.bgZoom;
        bg["pan_x"] = (double)d.bgPanX;
        bg["pan_y"] = (double)d.bgPanY;
        bg["rotation"] = (double)d.bgRotation;
        root["background"] = bg;
    }

    root["lightingMode"] = d.lightingMode;
    root["lightIntensity"] = d.lightIntensity;
    root["fillLight"] = (double)d.fillLight;
    // Bordo: solo se c'e', e solo in parametrico (in Ray Marching non esiste).
    // Senza bordo la chiave manca, come nei preset precedenti: i file salvati
    // restano identici.
    if (d.borderThickness > 0.0f && !d.isImplicitMode) {
        QJsonObject b;
        b["thickness"] = (double)d.borderThickness;
        b["r"] = d.borderColor.redF();
        b["g"] = d.borderColor.greenF();
        b["b"] = d.borderColor.blueF();
        b["mode"] = d.borderMode;
        b["wireAlong"] = d.borderWireAlong;
        b["wireAround"] = d.borderWireAround;
        // Texture: solo se il bordo ne ha una.
        if (!d.borderTexCode.trimmed().isEmpty()) {
            auto color = [](const QColor &c) {
                return QJsonArray{ c.redF(), c.greenF(), c.blueF() };
            };
            QJsonObject t;
            t["code"] = d.borderTexCode;
            t["enabled"] = d.borderTexEnabled;
            t["col1"] = color(d.borderTexCol1);
            t["col2"] = color(d.borderTexCol2);
            t["zoom"] = (double)d.borderTexZoom;
            t["panX"] = (double)d.borderTexPanX;
            t["panY"] = (double)d.borderTexPanY;
            t["rotation"] = (double)d.borderTexRotation;
            if (!d.borderTexLibName.isEmpty()) t["libName"] = d.borderTexLibName;
            if (!d.borderTexHintText.trimmed().isEmpty()) {
                t["hintText"] = d.borderTexHintText.trimmed();
                t["hintSeconds"] = (double)d.borderTexHintSeconds;
            }
            b["texture"] = t;
        }
        root["border"] = b;
    }
    root["use4DLighting"] = d.use4DLighting;
    // renderMode e' gia' nella codifica del file (in RM: +10 = Shell).
    root["renderMode"] = d.renderMode;
    if (d.isImplicitMode) {
        root["shellThickness"] = (double)d.shellThickness;
        root["hybridMarcher"] = d.hybridMarcher;
    }
    root["projectionMode"] = d.projectionMode;
    root["cameraFov"] = (double)d.cameraFov;
    root["fov3D"] = (double)d.fov3D;
    root["fov4D"] = (double)d.fov4D;

    QJsonObject wf;
    wf["uStep"] = d.wireframeUStep;
    wf["vStep"] = d.wireframeVStep;
    root["wireframe"] = wf;

    bool anyCustom = false;
    const QJsonObject parts = meshPartsJson(d, /*withTextures*/ record, &anyCustom);
    if (anyCustom) root["meshParts"] = parts["meshParts"];
    if (d.meshScopeAll) root["meshScopeAll"] = true;
    if (d.hasAllDomain) {
        root["allUMin"] = (double)d.allUMin;
        root["allUMax"] = (double)d.allUMax;
        root["allVMin"] = (double)d.allVMin;
        root["allVMax"] = (double)d.allVMax;
    }
    return root;
}
