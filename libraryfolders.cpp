// libraryfolders.cpp - La cartella della libreria sul disco. Vedi libraryfolders.h.
#include "libraryfolders.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_MACOS
#include <sys/xattr.h>   // getxattr: vedi isCloudSynced
#endif
#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {
#if defined(Q_OS_ANDROID)
static void notifyAndroidMediaStore(const QString& filePath) {
    QJniEnvironment env;
    jstring jFilePath = env->NewStringUTF(filePath.toUtf8().constData());
    jobjectArray pathsArray = env->NewObjectArray(1, env->FindClass("java/lang/String"), jFilePath);

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "android/media/MediaScannerConnection",
        "scanFile",
        "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;Landroid/media/MediaScannerConnection$OnScanCompletedListener;)V",
        context.object(),
        pathsArray,
        nullptr,
        nullptr
    );

    env->DeleteLocalRef(pathsArray);
    env->DeleteLocalRef(jFilePath);
}
#endif

// La copia di un file di fabbrica e' da fare? Se il file su disco esiste ed e'
// diverso, e il ripristino e' forzato, si chiede all'utente (Yes/No, anche "a
// tutti": overwriteState lo ricorda per i file successivi).
bool fileNeedsCopy(const QString& src, const QString& dst,
                   bool forceRestore, bool isDeleted, int* overwriteState, QWidget *dialogParent)
{
    bool needsCopy = false;

    // --- CONTROLLO ESISTENZA E CONTENUTO ---
    if (!QFile::exists(dst)) {
        if (!isDeleted || forceRestore) needsCopy = true;
    } else if (forceRestore) {
        QFile srcFile(src);
        QFile dstFile(dst);
        if (srcFile.open(QIODevice::ReadOnly) && dstFile.open(QIODevice::ReadOnly)) {
            if (srcFile.readAll() != dstFile.readAll()) {

                if (overwriteState && *overwriteState == 1) {
                    needsCopy = true; // Yes To All
                } else if (overwriteState && *overwriteState == 2) {
                    needsCopy = false; // No To All
                } else {
                    // Chiediamo all'utente
                    QMessageBox msgBox(dialogParent);
                    msgBox.setWindowTitle("Modified Preset Detected");
                    msgBox.setText(QString("The preset '%1' has been modified.\nDo you want to overwrite it with the factory default?").arg(QFileInfo(dst).fileName()));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll);
                    msgBox.setDefaultButton(QMessageBox::No);

                    int ret = msgBox.exec();
                    if (ret == QMessageBox::Yes) {
                        needsCopy = true;
                    } else if (ret == QMessageBox::YesToAll) {
                        needsCopy = true;
                        if (overwriteState) *overwriteState = 1;
                    } else if (ret == QMessageBox::NoToAll) {
                        needsCopy = false;
                        if (overwriteState) *overwriteState = 2;
                    } else {
                        needsCopy = false; // No
                    }
                }
            }
        }
    }

    return needsCopy;
}
}

QString LibraryFolders::root() {
#if defined(Q_OS_ANDROID)
    return "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#elif defined(Q_OS_IOS)
    // Percorso live dal sistema operativo, così non scade mai
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    return QSettings().value("libraryRootPath").toString();
#endif
}

namespace {
struct BranchEntry { LibraryFolders::Branch branch; const char *key; const char *name; };
const BranchEntry kBranches[] = {
    { LibraryFolders::Branch::Surfaces, "pathSurfaces", "surfaces" },
    { LibraryFolders::Branch::Textures, "pathTextures", "textures" },
    { LibraryFolders::Branch::Sounds,   "pathSounds",   "sounds"   },
    { LibraryFolders::Branch::Records,  "pathRecords",  "records"  },
};
}

QString LibraryFolders::branchDir(Branch branch)
{
    for (const BranchEntry &e : kBranches)
        if (e.branch == branch)
            return QSettings().value(QLatin1String(e.key),
                                     root() + QLatin1Char('/') + QLatin1String(e.name)).toString();
    return QString();
}

// IL RAMO DI UN PERCORSO. I salvataggi lo cercavano come SOTTOSTRINGA
// ("/surfaces/" nel percorso, maiuscole comprese): una cartella "Surfaces"
// dentro records/ faceva rifiutare il Save Record ("must be saved exclusively
// in the 'Records' folder"). Qui conta dove sta il percorso: dentro quale
// radice di ramo, la piu' profonda se piu' d'una lo contiene. Per ogni ramo
// valgono sia la cartella scelta con Change Folder (pathSurfaces, ...) sia
// <radice>/<ramo>: una chiave rimasta su una sottocartella non lo fa sparire.
// Maiuscole ignorate: il disco del Mac non le distingue, e i rami storici
// possono averle ("Surfaces").
LibraryFolders::Branch LibraryFolders::branchOf(const QString &path)
{
    const QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString rootDir = root();
    QSettings settings;
    Branch best = Branch::None;
    qsizetype bestLen = -1;
    for (const BranchEntry &e : kBranches) {
        QStringList dirs;
        if (!rootDir.isEmpty()) dirs << rootDir + QLatin1Char('/') + QLatin1String(e.name);
        const QString chosen = settings.value(QLatin1String(e.key)).toString();
        if (!chosen.isEmpty()) dirs << chosen;
        for (const QString &raw : dirs) {
            const QString d = QDir::cleanPath(raw);
            const bool inside = p.compare(d, Qt::CaseInsensitive) == 0
                                || p.startsWith(d + QLatin1Char('/'), Qt::CaseInsensitive);
            if (inside && d.size() > bestLen) {
                best = e.branch;
                bestLen = d.size();
            }
        }
    }
    return best;
}

// CARTELLA DENTRO UN SERVIZIO DI SINCRONIZZAZIONE (iCloud Drive, Dropbox...)?
//
// Serve a dirlo PRIMA di installarci la libreria. Una libreria che vive li'
// funziona finche' i file restano sul disco, ma il servizio li rimuove quando
// serve spazio ("evict") lasciando dei segnaposto: da quel momento i preset non
// sono leggibili senza riscaricarli, e isDatalessFile li salta per non bloccare
// l'applicazione (vedi librarymanager.cpp). MISURATO su una libreria in
// ~/Documents con iCloud attivo: 55 file smaterializzati sono diventati 204 e
// poi 434 nel giro di un'ora, senza alcuna azione dell'utente.
//
// Attenzione: l'attributo sta SOLO sulla radice del dominio sincronizzato
// (~/Documents, ~/Desktop), NON sulle sottocartelle -- verificato con xattr:
// ~/Documents lo porta, ~/Documents/presets no. Va quindi risalita la catena
// dei genitori, altrimenti la cartella che l'utente sceglie davvero (una
// sottocartella) risulterebbe sempre "locale" e l'avviso non comparirebbe mai.
//
// Fuori da macOS non si fa nulla: il caso e' quello di iCloud Drive e dei
// file provider di sistema.
bool LibraryFolders::isCloudSynced(const QString &path)
{
#ifdef Q_OS_MACOS
    // Si risale la catena come STRINGA, non con QDir::cdUp(): su un percorso che
    // non esiste ancora cdUp() fallisce e la risalita si fermerebbe al primo
    // passo. MISURATO: ~/Documents/presets/nonesiste/ancora veniva dato per
    // "locale" pur essendo dentro Documents -- ed e' proprio il caso del primo
    // avvio, dove l'utente indica una cartella da creare.
    QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!candidate.isEmpty() && candidate != QLatin1String("/")) {
        // getxattr con size 0 chiede solo se l'attributo c'e': niente buffer e
        // nessuna lettura del valore, che non ci interessa. Su un percorso
        // inesistente fallisce e basta, quindi non serve un exists() a monte.
        const QByteArray raw = QFile::encodeName(candidate);
        if (::getxattr(raw.constData(), "com.apple.file-provider-domain-id",
                       nullptr, 0, 0, 0) >= 0)
            return true;

        const int slash = candidate.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0) break;
        candidate.truncate(slash);
    }
    return false;
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool LibraryFolders::isLibraryRoot(const QDir &dir)
{
    if (!dir.exists()) return false;

    // entryList (non exists("surfaces")): il confronto va fatto sui nomi REALI
    // su disco, per riconoscere anche i rami con la maiuscola.
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    static const QStringList branches = {
        QStringLiteral("surfaces"), QStringLiteral("textures"),
        QStringLiteral("records"),  QStringLiteral("sounds")
    };
    for (const QString &entry : entries) {
        if (branches.contains(entry.toLower())) return true;
    }
    return false;
}

// RADICE DELLA LIBRERIA da una cartella indicata dall'utente in un pannello.
//
// Chi sceglie puo' ragionevolmente indicare due cose diverse, e questa funzione
// e' l'UNICO punto che decide quale delle due ha in mano:
//  - la radice della libreria vera e propria (dentro c'e' gia' almeno uno dei
//    quattro rami): si prende COM'E'. Appendere qui creava "presets/presets",
//    con i preset di fabbrica installati nel livello annidato e i rami
//    originali lasciati vuoti -- la libreria che l'utente guardava non era
//    quella che l'app usava;
//  - la cartella che la CONTIENE: si scende in "presets" SOLO se quella
//    sottocartella e' a sua volta una radice valida (vedi sotto).
//
// LA CARTELLA SCELTA VINCE, SEMPRE, tranne in un caso preciso.
// Qui si scendeva in una qualunque sottocartella di nome "presets" senza
// verificare che cosa fosse: bastava che esistesse. Una cartella di lavoro, un
// residuo, un backup -- diventava la radice della libreria pur non essendo
// stata scelta da nessuno. Con la radice dirottata, setupDefaultFolders creava
// li' i quattro rami e syncResources ci reinstallava i preset di
// fabbrica: la libreria risultava piena ma difforme da quella che l'utente
// vedeva nel Finder, allo stesso percorso. MISURATO: scegliendo una cartella
// che conteneva una "presets" residua, la libreria e' finita in
// .../presets/records/presets -- annidata dentro un RAMO della libreria vera.
// Ora si scende solo in una "presets" che e' gia' una libreria: nel caso
// legittimo (l'utente indica il contenitore di una libreria esistente) il
// comportamento non cambia, in tutti gli altri la cartella scelta si prende
// com'e'.
//
// Il test dei rami e' CASE-INSENSITIVE: con il confronto esatto una libreria
// con "Surfaces" maiuscolo non veniva riconosciuta come radice e finiva
// annidata: proprio il caso che questa funzione deve impedire.
//
// Il nome della sottocartella si legge DA DISCO con entryList, non si indovina:
// i due punti che la creano non concordano ("Presets" in onAddRepositoryClicked,
// "presets" in setupDefaultFolders) e su APFS -- non case-sensitive --
// QDir::exists("Presets") risponde true anche per "presets". Chiedendo per nome
// si salverebbe un percorso con la maiuscola sbagliata: innocuo per il
// filesystem, non per i bookmark di sandbox ne' per i confronti fra stringhe.
QString LibraryFolders::resolveRoot(const QString &pickedDir)
{
    const QString clean = QDir::cleanPath(pickedDir);
    if (clean.isEmpty()) return clean;

    const QDir dir(clean);

    // 1. La cartella scelta E' GIA' una libreria: si prende com'e'.
    if (isLibraryRoot(dir)) return clean;

    // 2. Dentro c'e' una "presets" che e' A SUA VOLTA una libreria: e' il caso
    //    legittimo del contenitore, si scende. Il nome viene dal disco.
    const QStringList hits = dir.entryList(QStringList() << QStringLiteral("presets"),
                                           QDir::Dirs | QDir::NoDotAndDotDot);
    if (!hits.isEmpty()) {
        const QString candidate = clean + "/" + hits.first();
        if (isLibraryRoot(QDir(candidate))) return candidate;
        // Esiste ma NON e' una libreria: non e' stata scelta da nessuno e non
        // si adotta. Si ricade sulla cartella scelta, qui sotto.
    }

    // 3. Nessuna libreria in vista: si crea una sottocartella "presets" e la
    //    libreria va li' dentro. I quattro rami li crea setupDefaultFolders.
    //
    // I RAMI NON VANNO SPARSI NELLA CARTELLA SCELTA. Prima si restituiva
    // `clean`, quindi scegliendo ~/Projects si ottenevano ~/Projects/surfaces,
    // /textures, /records, /sounds -- quattro cartelle dal nome generico
    // rovesciate in una cartella di lavoro, senza niente che le tenesse insieme
    // ne' le riconducesse a questa applicazione. MISURATO scegliendo ~/Projects.
    //
    // La regola precedente ("la libreria si installa NELLA cartella indicata")
    // nasceva per impedire le librerie ANNIDATE (presets dentro presets), ma
    // quel rischio riguardava i comandi che CAMBIANO cartella -- e quelli oggi
    // non passano piu' di qui: "Change Library Folder..." scrive la radice e
    // basta (non chiama setupDefaultFolders e rifiuta le cartelle che non sono
    // gia' librerie), "Change Folder for <Ramo>..." scrive una sola chiave.
    // Restano i due punti che INSTALLANO davvero, dove creare il contenitore e'
    // la cosa giusta. I casi 1 e 2 qui sopra continuano a coprire l'annidamento:
    // una cartella che e' gia' una libreria si prende com'e'.
    return clean + QStringLiteral("/presets");
}

void LibraryFolders::syncResources(const QString &resourcePath, const QString &diskPath,
                                   bool forceRestore, int *overwriteState, QWidget *dialogParent)
{
    QDir diskDir(diskPath);

    if (!diskDir.exists()) {
        diskDir.mkpath(".");
    }

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // =========================================================
    // VERSIONE MOBILE (iOS/Android)
    // =========================================================
    QDirIterator it(resourcePath, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString src = it.next();

        QString relativePath = src.mid(resourcePath.length());
        if (relativePath.startsWith("/")) relativePath = relativePath.mid(1);

        QString dst = diskDir.absoluteFilePath(relativePath);
        QString dstDir = QFileInfo(dst).absolutePath();

        if (!QDir(dstDir).exists()) QDir().mkpath(dstDir);

        QString deletedPath = dst + ".deleted";
        if (forceRestore && QFile::exists(deletedPath)) QFile::remove(deletedPath);

        bool isDeleted = QFile::exists(deletedPath);
        bool needsCopy = fileNeedsCopy(src, dst, forceRestore, isDeleted, overwriteState, dialogParent);

        if (needsCopy) {
            if (QFileInfo(src).fileName().startsWith("._")) continue;

            QFile inFile(src);
            if (inFile.open(QIODevice::ReadOnly)) {
                QFile outFile(dst);

                if (outFile.exists()) {
                    outFile.setPermissions(QFile::WriteOwner | QFile::WriteUser);
                    outFile.remove();
                }

                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(inFile.readAll());
                    outFile.close();
#if defined(Q_OS_ANDROID)
                    notifyAndroidMediaStore(dst);
#endif
                }
                inFile.close();
            }
        }
    }

#else
    // =========================================================
    // VERSIONE DESKTOP ORIGINALE
    // =========================================================
    QDir resDir(resourcePath);

    for (const QString &filename : resDir.entryList(QDir::Files)) {
        QString src = resourcePath + "/" + filename;
        QString dst = diskDir.absoluteFilePath(filename);
        QString deletedPath = dst + ".deleted";

        if (forceRestore && QFile::exists(deletedPath)) {
            QFile::remove(deletedPath);
        }

        bool isDeleted = QFile::exists(deletedPath);
        bool needsCopy = fileNeedsCopy(src, dst, forceRestore, isDeleted, overwriteState, dialogParent);

        if (needsCopy) {
            if (filename.startsWith("._")) continue;

            if (QFile::exists(dst)) {
                QFile::setPermissions(dst, QFile::WriteOwner | QFile::WriteUser);
                QFile::remove(dst);
            }

            if (QFile::copy(src, dst)) {
                QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
            }
        }
    }

    // GESTIONE SOTTOCARTELLE (Nota l'aggiunta di overwriteState)
    for (const QString &dirName : resDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString subResPath = resourcePath + "/" + dirName;
        QString subDiskPath = diskPath + "/" + dirName;

        syncResources(subResPath, subDiskPath, forceRestore, overwriteState, dialogParent);
    }
#endif
}
