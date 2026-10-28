#include "videorecorder.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "surfaceengine.h"
#include "synthesizer.h"
#include "audiocontroller.h"
#include "nativevideoencoder.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QSharedPointer>
#include <QTimer>
#include <QMessageBox>
#include <QApplication>
#include <QMediaPlayer>
#include <QDebug>
#include <QPainter>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#ifdef Q_OS_IOS
// Sopprime il menu di modifica sul campo nome file: consuma i
// QContextMenuEvent (long-press/fine lente del plugin iOS). Accettare
// l'evento evita anche il callout di fallback del plugin (vivo su iPad).
// Copia gemella in presetserializer.cpp (namespace anonimo, vedi commento la').
namespace {
class NoEditMenuFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::ContextMenu) {
            ev->accept();
            return true;
        }
        return QObject::eventFilter(obj, ev);
    }
};
} // namespace
#endif

class MobileVideoSaveDialog : public QDialog {
public:
    QSpinBox* spinDuration;
    QSpinBox* spinFps;
    QComboBox* comboRes;
    QComboBox* comboEngine;
    QLineEdit* nameEdit;
#if !defined(Q_OS_IOS)
    QComboBox* comboFormat;
#endif

    MobileVideoSaveDialog(const QString& startDir, int defDur, int defFps, int defRes, QWidget* parent = nullptr)
        : QDialog(parent), currentDir(startDir) {
        setWindowTitle("Export Settings & Save");

        // RIMOSSO setMinimumSize(320, 500)! Ora blocchiamo solo la larghezza, lasciando
        // l'altezza libera di restringersi e adattarsi in landscape.
        setMinimumWidth(320);

        // Layout principale: divide l'area scorrevole dai bottoni in fondo
        QVBoxLayout* mainDialogLayout = new QVBoxLayout(this);
        mainDialogLayout->setContentsMargins(0, 0, 0, 0); // Massimizza lo spazio

        // --- CREAZIONE DELL'AREA DI SCORRIMENTO ---
        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame); // Rimuove bordi antiestetici

        // Widget contenitore che andrà DENTRO l'area di scorrimento
        QWidget* scrollContent = new QWidget(scrollArea);
        QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);

        // --- 1. IMPOSTAZIONI VIDEO (Dentro la Scrollbar) ---
        QFormLayout* form = new QFormLayout();
        spinDuration = new QSpinBox(scrollContent);
        spinDuration->setRange(1, 86400);
        spinDuration->setValue(defDur);
        spinDuration->setStyleSheet("padding: 8px; font-size: 16px;");
        form->addRow("Duration (sec):", spinDuration);

        spinFps = new QSpinBox(scrollContent);
        spinFps->setRange(24, 120);
        spinFps->setValue(defFps > 120 ? 120 : defFps);
        spinFps->setStyleSheet("padding: 8px; font-size: 16px;");
        form->addRow("FPS (24-120):", spinFps);

        comboRes = new QComboBox(scrollContent);
        // "Current View Size" e non "Monitor Default": su mobile non c'e' un
        // monitor, e la voce non e' comunque la risoluzione del display. E' la
        // dimensione della SOLA vista 3D (glWidget->size() * devicePixelRatio,
        // vedi la cattura FBO piu' sotto): esclude dock e barre, e sui telefoni
        // il devicePixelRatio (tipicamente 3) la porta comunque nell'ordine del 2K.
        comboRes->addItems({
            "Current View Size",
            "1080p Full HD (1920x1080)",
            "1440p 2K (2560x1440)",
            "2160p 4K (3840x2160)"
        });
        int safeRes = (defRes >= 0 && defRes < comboRes->count()) ? defRes : 1;
        comboRes->setCurrentIndex(safeRes);
        comboRes->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Resolution:", comboRes);

#if !defined(Q_OS_IOS)
        comboFormat = new QComboBox(scrollContent);
        // BMP per primo = default: la compressione PNG a 4K è proibitiva su mobile
        // (misurato ~2.1s/frame vs ~30ms con BMP). PNG resta solo per chi vuole
        // risparmiare spazio su disco accettando l'export molto più lento.
        comboFormat->addItems({
            "BMP (Uncompressed - Faster)",
            "PNG (Lossless - Saves Disk Space)"
        });
        comboFormat->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Frame Format:", comboFormat);
#endif

        comboEngine = new QComboBox(scrollContent);
        comboEngine->addItems({
            "Native Resolution (FBO - Max Quality, Slower)",
            "Screen Upscale (Standard, Faster)"
        });
        comboEngine->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Render Engine:", comboEngine);

        scrollLayout->addLayout(form);

        // --- 2. NAVIGATORE CARTELLE (Dentro la Scrollbar) ---
        pathLabel = new QLabel(currentDir.absolutePath(), scrollContent);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 12px; color: gray; margin-top: 10px;");
        scrollLayout->addWidget(pathLabel);

        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("Name:", scrollContent));
        nameEdit = new QLineEdit("NewVideo", scrollContent);
        nameEdit->setStyleSheet("padding: 10px; font-size: 16px;");
#ifdef Q_OS_IOS
        // Come nel MobileSaveDialog dei preset: niente menu di modifica sul
        // campo nome file (il long-press per posizionare il cursore lo faceva
        // apparire bloccando la digitazione). La lente resta attiva. Il lavoro
        // vero lo fa NoEditMenuFilter; l'hint resta come dichiarazione
        // d'intento per il plugin.
        nameEdit->setInputMethodHints(nameEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        nameEdit->installEventFilter(new NoEditMenuFilter(nameEdit));
#endif
        nameLayout->addWidget(nameEdit);
        connect(nameEdit, &QLineEdit::returnPressed, nameEdit, &QLineEdit::clearFocus);
        scrollLayout->addLayout(nameLayout);

        listWidget = new QListWidget(scrollContent);
        listWidget->setStyleSheet("QListWidget::item { padding: 18px; border-bottom: 1px solid #ddd; font-size: 16px; }");

        // Disattiviamo le scrollbar interne per evitare sovrapposizioni
        listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        scrollLayout->addWidget(listWidget);

        // Chiude il pacchetto e lo inserisce nell'area di scorrimento
        scrollContent->setLayout(scrollLayout);
        scrollArea->setWidget(scrollContent);
        mainDialogLayout->addWidget(scrollArea);

        // --- 3. BOTTONI SALVATAGGIO (FUORI dalla scrollbar, sempre visibili) ---
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setContentsMargins(10, 10, 10, 10); // Margine per dare respiro ai tasti
        QPushButton* cancelBtn = new QPushButton("Cancel", this);
        QPushButton* saveBtn = new QPushButton("Save", this);
        cancelBtn->setAutoDefault(false);
        saveBtn->setAutoDefault(false);
        cancelBtn->setStyleSheet("padding: 12px; font-size: 16px;");
        saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold; color: #cc0000;");
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);

        mainDialogLayout->addLayout(btnLayout);

        // --- LOGICA BOTTONI E SELEZIONE ---
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            QString finalPath = getSelectedPath();
            if (QFile::exists(finalPath)) {
                QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                                          "Overwrite File?",
                                                                          "A video with this name already exists in this folder.\nDo you want to overwrite it?",
                                                                          QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::No) return;
            }
            accept();
        });

        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString data = item->data(Qt::UserRole).toString();

            if (data == "..") {
                currentDir.cdUp();
                refreshList();
            } else if (data.startsWith("DIR|")) {
                // È una cartella: navighiamo all'interno
                currentDir.cd(data.mid(4));
                refreshList();
            } else if (data.startsWith("FILE|")) {
                // È un file video: copiamo il suo nome nella casella di input!
                QString fileName = data.mid(5);
                QFileInfo fi(fileName);
                nameEdit->setText(fi.completeBaseName()); // Prende il nome senza il ".mp4"
            }
        });

        refreshList();
    }

    QString getSelectedPath() const {
        QString name = nameEdit->text();
        if (!name.endsWith(".mp4", Qt::CaseInsensitive)) name += ".mp4";
        return currentDir.absoluteFilePath(name);
    }

private:
    void refreshList() {
        listWidget->clear();

        // 1. Aggiungiamo sempre il tasto per tornare indietro
        QListWidgetItem* upItem = new QListWidgetItem("📁 .. (Up)", listWidget);
        upItem->setData(Qt::UserRole, "..");

        // =========================================================
        // METODO SICURO A DUE PASSAGGI (Anti-Bug per filtri Mobile)
        // =========================================================

        // FASE A: Recuperiamo SOLO LE CARTELLE (azzerando i filtri testuali)
        currentDir.setNameFilters(QStringList());
        QFileInfoList dirs = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo& info : dirs) {
            QListWidgetItem* dirItem = new QListWidgetItem("📁 " + info.fileName(), listWidget);
            dirItem->setData(Qt::UserRole, "DIR|" + info.fileName());
        }

        // FASE B: Recuperiamo SOLO I FILE VIDEO (applicando il filtro)
        currentDir.setNameFilters(QStringList() << "*.mp4" << "*.mov" << "*.avi" << "*.mkv");
        QFileInfoList files = currentDir.entryInfoList(QDir::Files, QDir::Name);

        for (const QFileInfo& info : files) {
            QListWidgetItem* fileItem = new QListWidgetItem("🎞️ " + info.fileName(), listWidget);
            fileItem->setData(Qt::UserRole, "FILE|" + info.fileName());
            fileItem->setForeground(Qt::gray);
        }

        // Pulizia finale: riazzeriamo il filtro per non corrompere la navigazione futura
        currentDir.setNameFilters(QStringList());

        // =========================================================

        pathLabel->setText(currentDir.absolutePath());

        // Espansione dinamica per disattivare la doppia scrollbar
        int itemHeight = 55;
        int totalListHeight = listWidget->count() * itemHeight;
        listWidget->setMinimumHeight(qMax(150, totalListHeight));
    }

    QDir currentDir;
    QLabel* pathLabel;
    QListWidget* listWidget;
};
#endif

#if defined(Q_OS_IOS)
// Rileva un frame di RUMORE (grabFramebuffer che a volte, su iOS, restituisce il
// color buffer non renderizzato -> pixel RGB casuali a schermo intero). Un frame
// REALE ha ampie zone correlate (sfondo/superfici lisce): pixel adiacenti quasi
// uguali. Il rumore ha pixel indipendenti: ~ogni coppia adiacente salta di molto
// su tutti i canali. Campioniamo delle coppie orizzontali sparse e misuriamo la
// frazione di "salti forti": se e' altissima (impossibile in un frame vero, dove
// lo sfondo domina) il frame e' rumore e va scartato. frame: Format_RGB32.
static bool frameLooksLikeNoise(const QImage &frame)
{
    if (frame.isNull() || frame.width() < 4 || frame.height() < 4)
        return false; // non decidibile: non scartare

    const int rowStep = qMax(1, frame.height() / 200); // ~200 righe campionate
    const int colStep = 4;                              // coppie ogni 4 px
    const int jumpThreshold = 40;                       // salto "forte" per canale (0..255)

    qint64 pairs = 0;
    qint64 strongJumps = 0;

    for (int y = 0; y < frame.height(); y += rowStep) {
        const QRgb *line = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        for (int x = 0; x + 1 < frame.width(); x += colStep) {
            const QRgb a = line[x];
            const QRgb b = line[x + 1];
            const int dr = qAbs(qRed(a)   - qRed(b));
            const int dg = qAbs(qGreen(a) - qGreen(b));
            const int db = qAbs(qBlue(a)  - qBlue(b));
            ++pairs;
            if (dr > jumpThreshold && dg > jumpThreshold && db > jumpThreshold)
                ++strongJumps;
        }
    }

    if (pairs == 0) return false;
    // Nel rumore vero i "salti forti su tutti i canali" sono la stragrande
    // maggioranza; in un frame reale sono rari (bordi netti a parte). Soglia alta
    // (0.85) per non scartare MAI un frame legittimo, anche molto dettagliato.
    return (double)strongJumps / (double)pairs > 0.85;
}
#endif

VideoRecorder::VideoRecorder(MainWindow *mainWindow, QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow)
{
}

void VideoRecorder::toggleRecord()
{
    // --- GESTIONE TASTO STOP ---
    if (m_mainWindow->m_isRecording) {
        m_mainWindow->m_stopRecordingRequested = true;
        return;
    }

    // ==============================================================
    // 1. SALVATAGGIO STATO E STOP IMMEDIATO (FIX CONGELAMENTO E GEODETICO)
    // ==============================================================
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    // NB: niente snapshot del moto GO — il loop legge isRotationMotionRunning()
    // a ogni frame (stato vivo), cosi' GO/STOP premuti durante il REC agiscono.

    // Stato VERO del clock geometria, non il testo del master button: il master
    // dice "STOP" se QUALSIASI modulo e' in moto (es. solo sfondo animato), e
    // con la geometria ferma il vecchio criterio la faceva RIPARTIRE a fine REC.
    bool wasTimeAnimating = m_mainWindow->ui->glWidget->isSurfaceAnimating();
    if (wasTimeAnimating) {
        // Congela t durante i dialoghi di setup; riacceso prima del loop di
        // registrazione (vedi sotto), cosi' il freeze per-modulo vede lo stato
        // reale e la geometria in moto anima nel video invece di restare
        // inchiodata al frame pre-REC.
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
    }

    // ---> FIX: GESTIONE FLUSSO GEODETICO <---
    QTimer* geoAnimTimer = m_mainWindow->findChild<QTimer*>("geoAnimTimer");
    bool wasGeoAnimating = false;
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        wasGeoAnimating = true;
        geoAnimTimer->stop(); // Fermiamo i calcoli asincroni
    }

    // Il loop avanza il flusso solo se il suo timer era acceso (wasGeoAnimating),
    // con la stessa funzione del vivo (advanceGeodesicFlowBy). Qui c'era anche
    // una regola ricostruita dai testi (equazioni con U/V/W, campi geodetici
    // pieni, linguetta parametrica) che nessuno usava.
    double startGeoTime = m_mainWindow->m_geoTime;
    // ----------------------------------------

    // Fermiamo tutto PRIMA di aprire finestre di dialogo
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);
    m_mainWindow->ui->glWidget->stopAllTimers();
    m_mainWindow->ui->glWidget->pauseMotion();

    // Helper per ripristinare lo stato
    auto restoreState = [this, wasAnimating, wasPath4D, wasPath3D, wasTimeAnimating, wasGeoAnimating, geoAnimTimer, startGeoTime]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) {
            m_mainWindow->m_paths->start(CameraPaths::Path4D);
            m_mainWindow->ui->btnDeparture->setText("STOP");
        }
        if (wasPath3D) {
            m_mainWindow->m_paths->start(CameraPaths::Path3D);
            m_mainWindow->ui->btnDeparture3D->setText("STOP");
        }
        if (wasTimeAnimating) {
            m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
            m_mainWindow->ui->glWidget->startAnimationTimer();
        }
        if (wasGeoAnimating && geoAnimTimer) {
            // Il loop ha avanzato geoTime per i frame del video: lo schermo
            // torna al tempo pre-REC, come rotazioni (setRotation4D in coda a
            // toggleRecord) e path (il loop non muta i tempi dei path).
            m_mainWindow->m_geoTime = startGeoTime;
            geoAnimTimer->start(); // Riavviamo l'asincronia per la normale visualizzazione
        }
    };

    // ==============================================================
    // 2. INPUT UTENTE E PERCORSI SALVATAGGIO
    // ==============================================================
    QSettings settings;
    // DICHIARIAMO TUTTE LE VARIABILI QUI AL SICURO, GLOBALI PER LA FUNZIONE!
    int seconds = 10;
    int fps = 60;
    int resIndex = 1;
    int targetWidth = -1;
    int targetHeight = -1;
    bool useFBO = false;
    bool usePng = false;

    QString rootPath;
#if defined(Q_OS_ANDROID)
    rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#elif defined(Q_OS_IOS)
    rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    rootPath = settings.value("libraryRootPath").toString();
    if (rootPath.isEmpty()) rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#endif

    QString defaultVideoDir = rootPath + "/renders";
    QDir().mkpath(defaultVideoDir);
    QString userSelectedFile;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // --- ANDROID e IOS: FINESTRA UNIFICATA MOBILE ---
    int defDur = settings.value("lastRecDuration", 10).toInt();
    int defFps = settings.value("lastRecFPS", 60).toInt();
    int defRes = settings.value("lastRecResIndex", 1).toInt();

    MobileVideoSaveDialog pathDialog(defaultVideoDir, defDur, defFps, defRes, m_mainWindow);
    if (pathDialog.exec() != QDialog::Accepted) {
        restoreState();
        return;
    }

    seconds = pathDialog.spinDuration->value();
    fps = pathDialog.spinFps->value();
    resIndex = pathDialog.comboRes->currentIndex();
    userSelectedFile = pathDialog.getSelectedPath();
    useFBO = (pathDialog.comboEngine->currentIndex() == 0);

#if defined(Q_OS_IOS)
    // iOS non espone la scelta del formato: i frame temporanei sono sempre RAW
    // (pixel grezzi), copiati con un memcpy nel CVPixelBuffer dall'encoder nativo.
    usePng = false;
#else
    usePng = (pathDialog.comboFormat->currentIndex() == 1); // idx 0 = BMP (default), 1 = PNG
#endif

#else
    // --- DESKTOP (Mac, Windows, Linux): FINESTRE CLASSICHE ---
    int defaultDuration = settings.value("lastRecDuration", 10).toInt();
    bool ok;
    seconds = QInputDialog::getInt(m_mainWindow, "Render Video", "Video duration (seconds):", defaultDuration, 1, 86400, 1, &ok);
    if (!ok) { restoreState(); return; }

    int defaultFPS = settings.value("lastRecFPS", 60).toInt();

#if defined(Q_OS_MACOS)
    int maxFps = 84;
    QString fpsPrompt = "FPS (24-84) [QuickTime slo-mo limit]:";
#else
    int maxFps = 120;
    QString fpsPrompt = "FPS (24-120):";
#endif

    if (defaultFPS > maxFps) defaultFPS = maxFps;

    fps = QInputDialog::getInt(m_mainWindow, "Frame Rate", fpsPrompt, defaultFPS, 24, maxFps, 1, &ok);
    if (!ok) { restoreState(); return; }

    // "Current View Size" come nel dialogo mobile: e' la sola vista 3D, non il
    // monitor ne' la finestra (era "Monitor Default (Current Window Size)").
    QStringList resolutions = {
        "Current View Size",
        "1080p Full HD (1920x1080)",
        "1440p 2K (2560x1440)",
        "2160p 4K (3840x2160)"
    };
    int defaultResIndex = settings.value("lastRecResIndex", 1).toInt();
    QString selectedRes = QInputDialog::getItem(m_mainWindow, "Video Resolution", "Select export resolution:", resolutions, defaultResIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    resIndex = resolutions.indexOf(selectedRes);

    // Formato — BMP per primo = default (PNG a 4K è proibitivo: ~2.1s/frame).
    QStringList formats = {
        "BMP (Uncompressed - Faster)",
        "PNG (Lossless - Saves Disk Space)"
    };
    int defaultFormatIndex = settings.value("lastRecFormat", 0).toInt();
    QString selectedFormat = QInputDialog::getItem(m_mainWindow, "Frame Format", "Select temporary frame format:", formats, defaultFormatIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    int formatIndex = formats.indexOf(selectedFormat);
    settings.setValue("lastRecFormat", formatIndex);
    usePng = (formatIndex == 1); // idx 0 = BMP (default), 1 = PNG

    // FBO
    QStringList engineModes = {
        "Native Resolution (FBO - Max Quality, Slower)",
        "Screen Upscale (Standard, Faster)"
    };
    int defaultEngineIndex = settings.value("lastRecEngine", 0).toInt();
    QString selectedEngine = QInputDialog::getItem(m_mainWindow, "Render Engine", "Select rendering engine:", engineModes, defaultEngineIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    int engineIndex = engineModes.indexOf(selectedEngine);
    settings.setValue("lastRecEngine", engineIndex);
    useFBO = (engineIndex == 0);

    // Salvataggio File
    QString lastVideoDir = settings.value("lastVideoDir", defaultVideoDir).toString();
    if (!QDir(lastVideoDir).exists()) lastVideoDir = defaultVideoDir;
    // QFileDialog ISTANZIATO (non lo static getSaveFileName) per poter impostare
    // setDefaultSuffix: cosi' il dialogo completa il nome con ".mp4" PRIMA del suo
    // controllo di sovrascrittura. Con lo static il check avveniva sul testo
    // digitato: "NewVideo1" senza estensione non esiste -> nessun avviso, e il
    // ".mp4" aggiunto DOPO faceva sovrascrivere NewVideo1.mp4 in silenzio (mentre
    // il default prefillato "NewVideo.mp4", estensione gia' nel campo, avvisava).
    QFileDialog saveDlg(m_mainWindow, "Save MP4 Video", lastVideoDir, "MP4 Video (*.mp4)");
    saveDlg.setAcceptMode(QFileDialog::AcceptSave);
    saveDlg.setFileMode(QFileDialog::AnyFile);
    saveDlg.setOption(QFileDialog::DontUseNativeDialog, true);
    saveDlg.setDefaultSuffix("mp4");
    saveDlg.selectFile("NewVideo.mp4");
    if (saveDlg.exec() != QDialog::Accepted || saveDlg.selectedFiles().isEmpty()) { restoreState(); return; }
    userSelectedFile = saveDlg.selectedFiles().first();
    if (!userSelectedFile.endsWith(".mp4", Qt::CaseInsensitive)) userSelectedFile += ".mp4";
    settings.setValue("lastVideoDir", QFileInfo(userSelectedFile).absolutePath());
#endif

    // --- SALVATAGGIO IMPOSTAZIONI E CALCOLO RISOLUZIONE (COMUNE) ---
    settings.setValue("lastRecDuration", seconds);
    settings.setValue("lastRecFPS", fps);
    settings.setValue("lastRecResIndex", resIndex);

    if (resIndex == 1) { targetWidth = 1920; targetHeight = 1080; }
    else if (resIndex == 2) { targetWidth = 2560; targetHeight = 1440; }
    else if (resIndex == 3) { targetWidth = 3840; targetHeight = 2160; }

    // ==============================================================
    // 3. PREPARAZIONE REGISTRAZIONE
    // ==============================================================
    m_mainWindow->m_isRecording = true;
    // Sopprime il watchdog di performance durante l'export (rendering frame-by-frame
    // = intervalli lentissimi per costruzione, non falso allarme da carico GPU).
    m_mainWindow->ui->glWidget->setRecordingActive(true);
    m_mainWindow->m_stopRecordingRequested = false;
    m_mainWindow->ui->glWidget->setUpdatesEnabled(false);

    // I moti fermati per i dialoghi RIPARTONO qui, sotto il clock virtuale:
    // timer attivi e stato VERO (bottoni, esclusivita', handler, predicati
    // funzionano normalmente, e i comandi al volo durante il REC entrano nel
    // video), ma i tick live sono no-op — con m_isRecording gia' true e il
    // clock esterno attivo, ad avanzare il tempo e' SOLO il loop qui sotto.
    m_mainWindow->ui->glWidget->setExternalClockActive(true);
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();

    // ---> BLOCCA LO SPEGNIMENTO DELLO SCHERMO <---
#if defined(Q_OS_IOS)
    NativeVideoEncoder::setKeepScreenOn(true);
#elif defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                const int FLAG_KEEP_SCREEN_ON = 128; // Flag di sistema Android
                window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            }
        }
    });
#endif
    // STOP AUDIO E ATTIVAZIONE TEMPO VIRTUALE
    // Il suono stava suonando quando l'utente ha premuto REC? Se l'aveva fermato
    // (Stop Sound), il video deve restare MUTO e a fine registrazione il suono
    // NON va riavviato: si registra quello che si vede e si sente. Catturato
    // PRIMA dello stopAll qui sotto, che azzera lo stato di riproduzione.
    const bool wasSoundPlaying = m_mainWindow->m_audioController &&
                                 m_mainWindow->m_audioController->isPlaying();
    m_mainWindow->m_audioController->stopAll();

    if (m_mainWindow->ui->glWidget) {
        // Fine dialoghi: il clock geometria torna allo stato che aveva al REC
        // (spento sopra solo per congelare lo schermo durante il setup). Durante
        // il REC gli orologi li avanza il loop (advanceClocksBy), non il render.
        if (wasTimeAnimating) {
            m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
            // Qui c'era stopAllTimers(), che spegneva ANCHE rotationTimer.
            // Quel timer non e' solo un clock: e' lo STATO letto da
            // isRotationMotionRunning(), il predicato con cui il loop decide se
            // chiamare advanceRotationsBy. Spento, le rotazioni non avanzavano
            // piu' — superficie immobile nel video E a schermo — ma SOLO sui
            // preset con geometria animata (t nelle equazioni), perche' solo
            // quelli entrano in questo ramo: da qui l'intermittenza "stesso
            // preset, a volte si muove e a volte no".
            //
            // Il clock shader live e' gia' fermo da stopAllTimers() a inizio
            // funzione e non e' stato riacceso: non serve rifermarlo qui.
        }

        // Da qui gli orologi (geometria, texture, sfondo, fasce) li avanza SOLO
        // il loop, col dt del frame: il video parte dal tempo che lo schermo
        // mostra ora e scorre alla stessa velocita'. Un modulo fermato
        // dall'utente resta fermo perche' advanceClocksBy non lo tocca -- non
        // serve piu' congelarlo e ricomporlo a fine REC.
        // Prima: tempo del recorder da 0.001 in m_manualTime E negli orologi,
        // sommati dallo shader -> video ripartito da zero e a velocita' doppia.
        m_mainWindow->ui->glWidget->setClocksDrivenByRecorder(true);
    }

    // Feedback visivo
    m_mainWindow->m_btnRec->setText("STOP");
    m_mainWindow->m_btnRec->setStyleSheet("QPushButton { color: white; background-color: red; font-weight: bold; border-radius: 4px; }");

    int totalFrames = seconds * fps;
    float timeStep = 1.0f / (float)fps;
    // Tick/secondo REALI dei timer path (l'intervallo e' in ms): il passo per
    // frame deve riprodurre la velocita' vista a schermo. Il vecchio 30.0f
    // nominale (vs 33.3 reali) rendeva i path ~11% piu' lenti nel video.
    float fpsScale4D = (1000.0f / (float)m_mainWindow->m_paths->interval(CameraPaths::Path4D)) / (float)fps;
    float fpsScale3D = (1000.0f / (float)m_mainWindow->m_paths->interval(CameraPaths::Path3D)) / (float)fps;

#ifdef Q_OS_ANDROID
    // Android: Usa la cache interna dell'app in modo che FFmpeg (C nativo) abbia i permessi
    QString targetPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    QString targetPath = rootPath + "/renders";
#endif

    QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_mainWindow->m_recFolder = targetPath + "/Temp_Render_" + timeStamp;
#if defined(Q_OS_MACOS)
    // macOS: la cartella la creiamo SOLO se serve davvero (colonna sonora
    // procedurale). I frame non passano piu' dal disco — vanno dal QImage
    // direttamente nell'encoder — quindi su clip muta resterebbe una cartella
    // vuota per tutto l'export. La crea, quando serve, ensureRecFolder().
#else
    if (!QDir(m_mainWindow->m_recFolder).exists()) {
        QDir().mkpath(m_mainWindow->m_recFolder);
    }
#endif

    // Crea la cartella temporanea al primo uso reale. Su macOS l'unico uso e' la
    // colonna sonora; altrove la cartella e' gia' stata creata qui sopra e questa
    // e' una no-op difensiva.
    auto ensureRecFolder = [this]() {
        if (!QDir(m_mainWindow->m_recFolder).exists())
            QDir().mkpath(m_mainWindow->m_recFolder);
    };

    // Il loop avanza direttamente lo stato VERO (i tempi dei path in
    // CameraPaths, omega/phi/psi), con la stessa semantica del tick live
    // (t += velocita' corrente): il recorder sostituisce l'OROLOGIO, non lo
    // stato. Cosi' velocita' cambiate al volo entrano in modo continuo, il
    // Reset (che azzera i tempi dei path e la posa) agisce sul video come a
    // schermo, e a fine REC il live prosegue da dove il video e' finito.

    // UI Feedback
    m_mainWindow->m_statusLabel->clear();
    m_mainWindow->m_renderProgress->setRange(0, totalFrames);
    m_mainWindow->m_renderProgress->setValue(0);
    m_mainWindow->m_renderProgress->setVisible(true);
    m_mainWindow->m_renderProgress->setFormat("%v / %m (%p%)");
    m_mainWindow->m_renderProgress->setAlignment(Qt::AlignCenter);

    // Equations e Surfaces restano bloccati: cambiare superficie/equazioni a
    // meta' REC invalida lo stato catturato prima del loop. I dock 3D/4D invece
    // restano VIVI E FUNZIONANTI, toggle compresi (GO, Departure, Reset, FOV,
    // velocita', vista): il loop non lavora su snapshot ma legge lo STATO VIVO
    // a ogni frame — i timer dei moti restano attivi (i tick sono no-op
    // durante il REC: guardia m_isRecording nei tick path, clock esterno nel
    // rotationTimer) e ad avanzare il tempo e' solo il loop. Ogni comando al
    // volo passa dagli handler NORMALI (esclusivita', handoff, testi bottone)
    // ed entra nel video come a schermo.
    m_mainWindow->ui->dockEquations->setEnabled(false);
    m_mainWindow->ui->dockSurfaces->setEnabled(false);

    int actualFramesRendered = 0;

#if defined(Q_OS_IOS)
    // Dimensioni (pari) dei frame RAW scritti su iOS: catturate al primo frame e
    // passate all'encoder, che con i RAW non può dedurle dal file (è grezzo).
    int iosFrameW = 0, iosFrameH = 0;
#endif

#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // ==============================================================
    // MODALITÀ PIPE (solo Desktop, solo clip MUTA)
    // --------------------------------------------------------------
    // Se non c'è audio, evitiamo del tutto i frame su disco: avviamo FFmpeg PRIMA
    // del loop e gli mandiamo ogni frame raw via stdin (-f rawvideo). Niente
    // scrittura+rilettura di migliaia di BMP/PNG (su 4K/60fps sono molti GB).
    // Quando c'è audio l'encoder lo muxa in coda al loop, quindi lì restiamo sul
    // collaudato percorso a file. Su iOS/Android il flusso resta quello a file.
    // NB: la presenza dello script sonoro non basta — se l'utente aveva FERMATO
    // il suono prima del REC (wasSoundPlaying=false) la clip e' muta per scelta.
    bool willHaveAudio = false;
    if (wasSoundPlaying) {
        // Stessa sorgente del player (sceneAudioSource): il video deve trovare
        // il suono che sta suonando, non cercarlo per conto suo.
        const QString preCode = m_mainWindow->sceneAudioSource();

        if (preCode.contains("mainSound") && m_mainWindow->m_audioController) {
            willHaveAudio = true;
        } else {
            QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatch m = musicRe.match(preCode);
            if (m.hasMatch()) willHaveAudio = QFile::exists(m.captured(1).trimmed());
        }
    }

#if defined(Q_OS_MACOS)
    // macOS: MAI la pipe. L'encoder nativo AVFoundation (nativevideoencoder_mac.mm)
    // prende il posto di ffmpeg su TUTTO macOS, con o senza audio: sotto sandbox
    // App Store non si possono lanciare eseguibili fuori dal bundle, e tenere due
    // strade (ffmpeg per il DMG, nativa per lo Store) lascerebbe proprio il ramo
    // dello Store come il meno collaudato. Il percorso e' lo stesso della pipe —
    // frame in memoria, niente file su disco — solo con l'encoder in-process.
    // L'audio si muxa alla chiusura, in NativeVideoEncoder::end().
    const bool usePipe = false;
    const bool useNativeMac = true;
    bool nativeMacStarted = false;   // begin() riuscito: da chiudere a fine loop
    int nativeW = 0, nativeH = 0;    // fissate al primo frame, come per la pipe

    // --- COLONNA SONORA PRODOTTA PRIMA DEL LOOP (obbligatorio, non un'ottimizzazione) ---
    // AVAssetWriter bilancia l'interleaving fra TUTTI gli input dichiarati: una
    // traccia audio dichiarata e lasciata vuota blocca readyForMoreMediaData del
    // VIDEO al primo frame, e l'export va in deadlock. L'audio quindi deve poter
    // entrare INSIEME ai frame, il che vuol dire averlo pronto prima di begin().
    // Si puo' fare: il bouncing offline dipende solo dalla durata, non dai frame.
    QString macAudioFile;
    if (willHaveAudio) {
        const QString preCode = m_mainWindow->sceneAudioSource();

        if (preCode.contains("mainSound") && m_mainWindow->m_audioController) {
            // Audio procedurale: lo renderizziamo alla durata NOMINALE richiesta.
            // Se l'utente ferma il REC prima, il taglio alla durata reale lo fa
            // NativeVideoEncoder::end(), che riceve actualSeconds.
            ensureRecFolder();
            const QString rawPath = m_mainWindow->m_recFolder + "/soundtrack.raw";
            const int nominalSecs = (int)ceil((double)totalFrames / (double)fps);
            if (m_mainWindow->m_audioController->saveSynthToRawFile(rawPath, nominalSecs)) {
                // L'encoder legge via AVAsset: serve un contenitore, non il raw.
                // Stessa intestazione WAV (44.1k, stereo, float32) del ramo iOS.
                const QString wavPath = m_mainWindow->m_recFolder + "/soundtrack.wav";
                QFile rawFile(rawPath);
                QFile wavFile(wavPath);
                if (rawFile.open(QIODevice::ReadOnly) && wavFile.open(QIODevice::WriteOnly)) {
                    QByteArray rawData = rawFile.readAll();
                    QDataStream out(&wavFile);
                    out.setByteOrder(QDataStream::LittleEndian);
                    out.writeRawData("RIFF", 4);
                    out << (quint32)(36 + rawData.size());
                    out.writeRawData("WAVE", 4);
                    out.writeRawData("fmt ", 4);
                    out << (quint32)16 << (quint16)3 << (quint16)2 << (quint32)44100;
                    out << (quint32)(44100 * 2 * 4) << (quint16)(2 * 4) << (quint16)32;
                    out.writeRawData("data", 4);
                    out << (quint32)rawData.size();
                    wavFile.write(rawData);
                    rawFile.close();
                    wavFile.close();
                    macAudioFile = wavPath;
                }
            }
        } else {
            // File esterno (//MUSIC:): gia' un contenitore, si usa com'e'.
            QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatch m = musicRe.match(preCode);
            if (m.hasMatch()) {
                const QString p = m.captured(1).trimmed();
                if (QFile::exists(p)) macAudioFile = p;
            }
        }
    }
    // Se la preparazione e' fallita, la clip esce muta: meglio che in deadlock.
    const bool macWithAudio = !macAudioFile.isEmpty();
#else
    const bool usePipe = !willHaveAudio;
    const bool useNativeMac = false;
#endif
    QProcess *pipeProcess = nullptr; // valorizzato al primo frame (serve W/H note)
    int pipeW = 0, pipeH = 0;

    // Localizza l'eseguibile ffmpeg (stessa logica del ramo a file più sotto).
    auto findFfmpeg = []() -> QString {
        QString prog = QStandardPaths::findExecutable("ffmpeg");
        if (prog.isEmpty()) {
            QStringList candidates;
#ifdef Q_OS_WIN
            candidates << "C:/ffmpeg/bin/ffmpeg.exe" << "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
#else
            candidates << "/opt/homebrew/bin/ffmpeg" << "/usr/local/bin/ffmpeg" << "/usr/bin/ffmpeg";
#endif
            for (const QString &path : candidates)
                if (QFile::exists(path)) { prog = path; break; }
        }
        return prog;
    };
#endif

    // Vero rendering FBO: fissiamo il color buffer offscreen alla risoluzione di
    // export UNA volta (non per-frame, per evitare flicker/ricostruzioni ripetute),
    // così ogni frame è renderizzato nativamente a piena risoluzione e
    // getFrameForVideo lo cattura senza upscale (fix wireframe sfocato).
    // Per la voce a dimensione corrente (target -1: "Current View Size") usiamo
    // la dimensione nativa del widget.
    bool fboCapture = useFBO;
    if (fboCapture) {
        int capW = targetWidth, capH = targetHeight;
        if (capW <= 0 || capH <= 0) {
            QSize nat = m_mainWindow->ui->glWidget->size() *
                        m_mainWindow->ui->glWidget->devicePixelRatio();
            capW = nat.width(); capH = nat.height();
        }
        m_mainWindow->ui->glWidget->beginHiResCapture(capW, capH);
    }

    // ==============================================================
    // 4. CICLO RENDERING
    // ==============================================================
    for (int i = 0; i < totalFrames; i++) {

        if (i % 5 == 0) {
            QApplication::processEvents();
        }

        if (m_mainWindow->m_stopRecordingRequested) {
            break;
        }

        // ====================================================

        // STATO VIVO, non snapshot: i predicati sono quelli del tick live e i
        // toggle (GO/Departure/Reset) agiscono a meta' REC come a schermo.
        if (m_mainWindow->pathRunning(CameraPaths::Path4D)) {
            // Stesso identico avanzamento e camera del tick live
            // (CameraPaths::advance): al recorder cambia solo quanti tick vale
            // il frame. Niente copie locali di questa logica (divergevano:
            // vista Center diversa, base 4D del Departure ignorata).
            m_mainWindow->m_paths->advance(CameraPaths::Path4D, fpsScale4D);
        }
        else if (m_mainWindow->pathRunning(CameraPaths::Path3D)) {
            m_mainWindow->m_paths->advance(CameraPaths::Path3D, fpsScale3D);
        }
        else if (m_mainWindow->isRotationMotionRunning()) {
            // Stessa identica cinematica del tick live (advanceRotationsBy):
            // al recorder cambia solo il dt, quello del frame virtuale.
            m_mainWindow->ui->glWidget->advanceRotationsBy(timeStep);
        }

        // Flusso geodetico: stessa identica logica del tick live
        // (advanceGeodesicFlowBy), al recorder cambia solo il dt virtuale del
        // frame. Prima il flusso restava CONGELATO nel video: il timer viene
        // fermato all'avvio del REC e il loop non lo avanzava mai. Indipendente
        // dai moti camera (a schermo convivono).
        if (wasGeoAnimating) {
            m_mainWindow->advanceGeodesicFlowBy(timeStep);
        }

        // Orologi di animazione (t di geometria, texture, sfondo, fasce): stessa
        // identica logica del render live (advanceClocksBy), al recorder cambia
        // solo il dt, quello del frame virtuale.
        m_mainWindow->ui->glWidget->advanceClocksBy(timeStep);

        // 1. Estrazione del frame (con o senza FBO)
        QImage frame = m_mainWindow->ui->glWidget->getFrameForVideo(targetWidth, targetHeight, useFBO);
        frame.setDevicePixelRatio(1.0);

        // ==============================================================
        // 2. ALPHA CHANNEL (FONDAMENTALE PER IOS E FBO)
        // ==============================================================
        if (frame.format() != QImage::Format_RGB32) {
            frame = frame.convertToFormat(QImage::Format_RGB32);
        }

        // ==============================================================
        // 3. FORMATO. Con l'FBO il frame e' gia' alla risoluzione di export; con
        // Screen Upscale e una risoluzione fissa lo adatta getFrameForVideo
        // (vista intera, fasce nere): qui ne restava una seconda copia.
        // ==============================================================
        if (!useFBO && (targetWidth <= 0 || targetHeight <= 0)) {
            // Risoluzione nativa: taglia i bordi se dispari (per FFmpeg)
            if (frame.width() % 2 != 0) frame = frame.copy(0, 0, frame.width()-1, frame.height());
            if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height()-1);
        }

        // ==============================================================
        // 4. UNICO SALVATAGGIO FINALE DEL FOTOGRAMMA
        // ==============================================================
        QString fileName; // Dichiariamo la variabile UNA SOLA VOLTA qui per evitare errori

#if defined(Q_OS_IOS)
        // iOS: scriviamo il frame come RAW (Format_RGB32, B,G,R,A in memoria) che
        // l'encoder copia con un memcpy nel CVPixelBuffer 32BGRA, senza più passare
        // da BMP -> UIImage -> CGContext (due conversioni a frame).
        // Croppiamo a dimensioni PARI già qui, così lo stride del .raw combacia con
        // le W/H passate all'encoder (che non può dedurle da un file grezzo).
        if (frame.width() % 2 != 0)  frame = frame.copy(0, 0, frame.width() - 1, frame.height());
        if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height() - 1);

        // Scarta i frame di rumore: su iOS grabFramebuffer a volte restituisce il
        // color buffer non renderizzato (RGB casuale a schermo intero). Non
        // scrivendo il .raw, l'encoder non trova quel frame e lo salta: un
        // fotogramma in meno e' impercettibile, un frame di rumore no. Il check e'
        // PRIMA di fissare iosFrameW/H, cosi' un eventuale primo frame di rumore non
        // detta le dimensioni. Aggiorniamo comunque il progress prima di saltare.
        if (frameLooksLikeNoise(frame)) {
            m_mainWindow->m_renderProgress->setValue(i + 1);
            continue;
        }

        if (iosFrameW == 0) { iosFrameW = frame.width(); iosFrameH = frame.height(); }

        fileName = QString("frame_%1.raw").arg(i, 5, 10, QChar('0'));
        {
            QFile rawOut(m_mainWindow->m_recFolder + "/" + fileName);
            if (rawOut.open(QIODevice::WriteOnly)) {
                const int rowBytes = frame.width() * 4; // RGB32
                for (int y = 0; y < frame.height(); ++y)
                    rawOut.write(reinterpret_cast<const char*>(frame.constScanLine(y)), rowBytes);
                rawOut.close();
            }
        }
#elif !defined(Q_OS_ANDROID)
        // --- DESKTOP ---
#if defined(Q_OS_MACOS)
        if (useNativeMac) {
            // Encoder nativo: il frame va dal QImage al CVPixelBuffer, senza
            // passare dal disco. Stessa disciplina della pipe: al 1o frame
            // fissiamo W/H (pari) e apriamo l'encoder, poi ogni frame deve
            // avere quelle dimensioni.
            if (frame.width() % 2 != 0)  frame = frame.copy(0, 0, frame.width() - 1, frame.height());
            if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height() - 1);

            if (!nativeMacStarted) {
                nativeW = frame.width();
                nativeH = frame.height();
                // macAudioFile e' la colonna sonora GIA' PRONTA su disco (viene
                // preparata sopra, prima del loop): l'encoder la versa insieme
                // ai frame. Passare qui un file inesistente, o dichiarare la
                // traccia e non riempirla, manda l'export in deadlock.
                // Il limite di durata e' quello nominale; end() rifinisce con
                // la durata reale, che uno stop anticipato rende piu' corta.
                const double nominalSecs = (double)totalFrames / (double)fps;
                if (NativeVideoEncoder::begin(userSelectedFile, fps, nativeW, nativeH,
                                              macAudioFile, nominalSecs)) {
                    nativeMacStarted = true;
                } else {
                    // Interrompiamo pulito: il messaggio lo diamo dopo il loop,
                    // leggendo NativeVideoEncoder::lastError().
                    m_mainWindow->m_stopRecordingRequested = true;
                }
            }

            if (nativeMacStarted && frame.width() == nativeW && frame.height() == nativeH) {
                if (!NativeVideoEncoder::appendFrame(frame)) {
                    // Encoder rotto a meta' clip (disco pieno, ...): fermiamo
                    // qui invece di accumulare frame in un writer morto.
                    m_mainWindow->m_stopRecordingRequested = true;
                }
            }
        } else
#endif
        if (usePipe) {
            // Frame raw direttamente nello stdin di FFmpeg: niente file su disco.
            // Le dimensioni devono restare costanti per tutta la clip -> al 1° frame
            // fissiamo W/H (pari) e avviamo l'encoder con -s WxH -pix_fmt bgra.
            if (frame.width() % 2 != 0)  frame = frame.copy(0, 0, frame.width() - 1, frame.height());
            if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height() - 1);

            if (!pipeProcess) {
                pipeW = frame.width();
                pipeH = frame.height();
                QString prog = findFfmpeg();
                if (prog.isEmpty()) {
                    // Niente FFmpeg: interrompiamo pulito (lo segnaliamo dopo il loop).
                    m_mainWindow->m_stopRecordingRequested = true;
                } else {
                    pipeProcess = new QProcess(this);
#if defined(Q_OS_LINUX)
                    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                    env.remove("LD_LIBRARY_PATH");
                    pipeProcess->setProcessEnvironment(env);
#endif
                    QStringList a;
                    a << "-y"
                      << "-f" << "rawvideo"
                      << "-pix_fmt" << "bgra"   // Format_RGB32 = B,G,R,A in memoria
                      << "-s" << QString("%1x%2").arg(pipeW).arg(pipeH)
                      << "-framerate" << QString::number(fps)
                      << "-i" << "-"            // stdin
                      << "-c:v" << "libx264"
                      << "-preset" << "fast"
                      << "-pix_fmt" << "yuv420p"
                      << "-crf" << "18"
                      << "-r" << QString::number(fps)
                      << "-movflags" << "+faststart"
                      << userSelectedFile;
                    pipeProcess->start(prog, a);
                    if (!pipeProcess->waitForStarted(5000)) {
                        delete pipeProcess; pipeProcess = nullptr;
                        m_mainWindow->m_stopRecordingRequested = true;
                    }
                }
            }

            if (pipeProcess && frame.width() == pipeW && frame.height() == pipeH) {
                const int rowBytes = pipeW * 4; // bgra
                for (int y = 0; y < pipeH; ++y) {
                    pipeProcess->write(reinterpret_cast<const char*>(frame.constScanLine(y)), rowBytes);
                    // Drena lo stdin se l'encoder è più lento del rendering, così non
                    // gonfiamo all'infinito il buffer in RAM.
                    if (pipeProcess->bytesToWrite() > 32 * 1024 * 1024)
                        pipeProcess->waitForBytesWritten(-1);
                }
            }
        } else if (usePng) {
            fileName = QString("frame_%1.png").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "PNG");
        } else {
            fileName = QString("frame_%1.bmp").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "BMP");
        }
#else
        // --- ANDROID: sempre percorso a file (ramo linker collaudato) ---
        if (usePng) {
            fileName = QString("frame_%1.png").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "PNG");
        } else {
            fileName = QString("frame_%1.bmp").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "BMP");
        }
#endif

        m_mainWindow->m_renderProgress->setValue(i + 1);
        actualFramesRendered++;
    }

    // ==============================================================
    // 5. RIPRISTINO E PULIZIA
    // ==============================================================
    m_mainWindow->m_isRecording = false;
    // Clock esterno spento INSIEME a m_isRecording: i timer dei moti (rimasti
    // attivi per tutto il REC) riprendono ad avanzare live da qui, proseguendo
    // dallo stato in cui il video e' finito.
    m_mainWindow->ui->glWidget->setExternalClockActive(false);
    // NB: il watchdog NON si riattiva qui. Resta sospeso per tutta la creazione
    // del file: lo riaccende releaseWatchdogAfterEncoding() (vedi sotto).

    // Ripristina il color buffer alla dimensione a schermo (esce dalla cattura FBO).
    // Va fatto SEMPRE, anche su stop anticipato (il loop esce solo con break).
    if (fboCapture) {
        m_mainWindow->ui->glWidget->endHiResCapture();
    }

    m_mainWindow->ui->glWidget->setUpdatesEnabled(true);

#if defined(Q_OS_IOS)
    NativeVideoEncoder::setKeepScreenOn(false);
#elif defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                const int FLAG_KEEP_SCREEN_ON = 128;
                window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            }
        }
    });
#endif

    // >>> DISATTIVAZIONE TEMPO VIRTUALE E RIAVVIO AUDIO <<<
    if (m_mainWindow->ui->glWidget) {
        // Gli orologi tornano al render live e proseguono da dove il video e'
        // arrivato (modello "live-through", come i moti qui sotto). I moduli
        // fermi non si sono mai mossi: niente da ricomporre.
        m_mainWindow->ui->glWidget->setClocksDrivenByRecorder(false);
    }

    // Riavvia il suono SOLO se stava suonando quando l'utente ha premuto REC:
    // un suono fermato a mano (Stop Sound) deve restare fermo anche dopo la
    // registrazione. (Chiamata privata di MainWindow grazie alla friend class.)
    if (wasSoundPlaying) m_mainWindow->onRunSoundClicked();
    // ==============================================================

    m_mainWindow->m_btnRec->setText("REC");
    m_mainWindow->m_btnRec->setStyleSheet("QPushButton { color: red; font-weight: bold; }");

    m_mainWindow->ui->dockEquations->setEnabled(true);
    m_mainWindow->ui->dockSurfaces->setEnabled(true);

    // FINE REC, modello "live-through": NESSUN ripristino dei moti — lo stato
    // corrente (timer, testi bottone, tempi dei path, posa 4D, geoTime) e'
    // quello che l'utente ha costruito coi comandi al volo, e lo schermo
    // prosegue da dove il video e' finito (i timer non sono mai stati fermati:
    // era fermo solo il loro OROLOGIO). restoreState resta per i soli percorsi
    // di annullamento pre-loop, dove il ripristino completo e' ancora giusto.
    //
    // Qui si riavviano solo i clock live che il loop pilotava direttamente:
    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }
    if (wasGeoAnimating && geoAnimTimer) {
        geoAnimTimer->start(); // geoTime prosegue dal punto raggiunto nel video
    }
    m_mainWindow->ui->glWidget->update();

    // WATCHDOG SOSPESO FINO A FILE CREATO.
    // La creazione del file (FFmpeg o AVFoundation) e' il momento di carico
    // massimo della macchina: i frame resi nel frattempo sono lenti perche'
    // l'encoder contende CPU/GPU/disco, non perche' la scena sia troppo pesante.
    // Misurarli faceva comparire il popup di rallentamento proprio a fine
    // registrazione sulle scene pesanti — un falso positivo, visto che la stessa
    // scena in esecuzione normale non lo attiva mai. Su iPhone il popup usciva
    // anche a moti FERMI: la prova che il carico e' dell'encoder, non
    // dell'animazione. Quindi non si toccano i moti (ripartono qui sopra come
    // sempre): si tiene solo il watchdog cieco finche' il file non e' chiuso.
    // Da chiamare UNA volta al termine di OGNI percorso di encoding (pipe
    // desktop, QProcess desktop/Android, nativo iOS), e precisamente DOPO la
    // QMessageBox di esito: il dialogo e' MODALE, quindi blocca il thread finche'
    // l'utente non lo chiude. Rilasciando prima, la finestra di grazia (500 ms)
    // si consumava tutta a dialogo aperto e il primo frame reso dopo la chiusura
    // veniva misurato — un dtMs pari a quanto era rimasto aperto l'avviso. Il
    // popup ricompariva percio' DOPO il "Finished!". Rilasciando dopo, la grazia
    // parte da quando la macchina ricomincia davvero a rendere.
    // NB: catturata PER VALORE e copiabile — il ramo QProcess (desktop con audio
    // e Android) e' asincrono: toggleRecord() ritorna prima che l'encoder finisca
    // e la lambda sopravvive nelle connect(). Il flag "gia' riattivato" e' quindi
    // uno shared_ptr e non una locale sullo stack, che a quel punto non esiste piu'.
    auto watchdogReleased = QSharedPointer<bool>::create(false);
    auto releaseWatchdogAfterEncoding = [this, watchdogReleased]() {
        if (*watchdogReleased) return;
        *watchdogReleased = true;
        // Riattiva il watchdog. La finestra di grazia aperta qui dentro copre il
        // transitorio successivo, ora che l'encoder non contende piu' la macchina.
        m_mainWindow->ui->glWidget->setRecordingActive(false);
    };

    // RETE DI SICUREZZA: la sospensione dev'essere garantita TEMPORANEA. Nel ramo
    // QProcess asincrono la riattivazione dipende da un segnale (finished /
    // errorOccurred): se per qualunque motivo non arrivasse, il watchdog
    // resterebbe cieco per il resto della sessione — cioe' disattivato anche
    // fuori dalla registrazione, che e' esattamente cio' che non deve accadere.
    // Questo timer chiude comunque la finestra; se il segnale arriva prima, il
    // flag rende la chiamata un no-op. Molto generoso di proposito: e' l'ULTIMA
    // difesa contro un watchdog cieco per sempre, non un limite di durata. Deve
    // sopravvivere sia a un encoding lungo e lento, sia a una QMessageBox di esito
    // lasciata aperta a lungo dall'utente (il rilascio avviene dopo di essa: se il
    // timer scattasse a dialogo aperto, il frame post-chiusura tornerebbe misurato
    // e il falso positivo si riaprirebbe).
    QTimer::singleShot(30 * 60 * 1000, m_mainWindow->ui->glWidget, releaseWatchdogAfterEncoding);

    m_mainWindow->m_statusLabel->setText("Generating MP4... please wait.");
    m_mainWindow->m_renderProgress->setVisible(false);

#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // ==============================================================
    // 6-bis. FINALIZZAZIONE PIPE (Desktop, clip MUTA)
    // I frame sono già stati spinti nello stdin di FFmpeg durante il loop:
    // chiudiamo lo stream e aspettiamo che chiuda il file. Niente file su disco,
    // niente audio (per definizione di usePipe), quindi qui finisce tutto.
    // ==============================================================
    if (usePipe) {
        m_mainWindow->m_isProcessingVideo = true;
        bool ok = false;
        QString errLog;

        if (pipeProcess) {
            pipeProcess->closeWriteChannel();           // EOF su stdin -> FFmpeg chiude
            pipeProcess->waitForFinished(-1);
            ok = (pipeProcess->exitStatus() == QProcess::NormalExit &&
                  pipeProcess->exitCode() == 0);
            if (!ok) errLog = QString::fromUtf8(pipeProcess->readAllStandardError()).right(500);
            pipeProcess->deleteLater();
            pipeProcess = nullptr;
        } else {
            // Pipe mai avviata: ffmpeg non trovato o avvio fallito (già segnalato
            // via stopRecordingRequested). Niente da finalizzare.
            errLog = "FFmpeg not found or failed to start.";
        }

        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();

        // La cartella temp esiste ancora (vuota di frame): rimuoviamola comunque.
        QDir tempDir(m_mainWindow->m_recFolder);
        if (tempDir.exists()) tempDir.removeRecursively();

        if (ok)
            QMessageBox::information(m_mainWindow, "Finished!", "Video successfully saved:\n" + userSelectedFile);
        else
            QMessageBox::warning(m_mainWindow, "Encoding Error", "FFmpeg failed.\n\n" + errLog);

        // DOPO la modale (vedi nota "watchdog sospeso"): il dialogo e' bloccante,
        // quindi la grazia deve partire da quando l'utente lo chiude.
        releaseWatchdogAfterEncoding();
        return;
    }
#endif

    // ==============================================================
    // 6. GENERAZIONE VIDEO (FFMPEG)
    // ==============================================================
    float actualSeconds = (float)actualFramesRendered / (float)fps;

    // 1. Tutto il codice che puo' portare la musica: la stessa sorgente del player.
    const QString currentCode = m_mainWindow->sceneAudioSource();

    QString audioFile = "";
    bool hasAudio = false;
    bool isRaw = false;

    // 2. SCENARIO A: Audio Procedurale (GPU GLSL Synth)
    // Gate wasSoundPlaying su entrambi gli scenari (stesso criterio di
    // willHaveAudio): suono fermato dall'utente prima del REC = clip muta.
    if (wasSoundPlaying && currentCode.contains("mainSound") && m_mainWindow->m_audioController) {
        ensureRecFolder();   // primo (e su macOS unico) uso reale della cartella
        audioFile = m_mainWindow->m_recFolder + "/soundtrack.raw";

        // Bouncing Offline (Molto più veloce del tempo reale)
        if (m_mainWindow->m_audioController->saveSynthToRawFile(audioFile, (int)ceil(actualSeconds))) {
            hasAudio = true;
            isRaw = true;
        }
    }
    // 3. SCENARIO B: File Esterno (MP3/WAV)
    else if (wasSoundPlaying) {
        QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
        QRegularExpressionMatch musicMatch = musicRe.match(currentCode);
        if (musicMatch.hasMatch()) {
            audioFile = musicMatch.captured(1).trimmed();
            hasAudio = QFile::exists(audioFile);
            isRaw = false;
        }
    }

// Da qui alla fine del blocco: SOLO le piattaforme che usano ffmpeg (Windows,
// Linux, Android). Su iOS e macOS l'encoder e' AVFoundation e questi argomenti
// non servono a nessuno: tenerli fuori dalla guardia li faceva compilare come
// codice morto, con il rischio (tipico di questo file) che qualcuno li creda
// ancora in uso. La preparazione dell'audio sopra, invece, e' COMUNE a tutti.
#if !defined(Q_OS_IOS) && !defined(Q_OS_MACOS)

#if defined(Q_OS_ANDROID)
    // ANDROID: FFmpeg scrive in un file temporaneo isolato
    QString videoFileName = m_mainWindow->m_recFolder + "/temp_video.mp4";
#else
    // DESKTOP: FFmpeg scrive DIRETTAMENTE nel file di destinazione finale
    QString videoFileName = userSelectedFile;
#endif

    // Diciamo a FFmpeg di leggere i file in base alla scelta che abbiamo fatto
    QString inputPattern = m_mainWindow->m_recFolder + (usePng ? "/frame_%05d.png" : "/frame_%05d.bmp");

    QStringList arguments;

    // 1. INPUT VIDEO
    arguments << "-y" << "-framerate" << QString::number(fps) << "-i" << inputPattern;

    // 2. INPUT AUDIO (Loop infinito che poi taglieremo)
    if (hasAudio) {
        arguments << "-stream_loop" << "-1";

        if (isRaw) arguments << "-f" << "f32le" << "-ar" << "44100" << "-ac" << "2" << "-i" << audioFile;
        else arguments << "-i" << audioFile;
    }

    // 3. IMPOSTAZIONI OUTPUT VIDEO
    // libx264 -crf 18 su TUTTE le piattaforme: qualità (quasi lossless visivo)
    // prioritaria sul tempo. L'encoder HW (h264/hevc_mediacodec) su Android era
    // ~10x più veloce ma lavora a bitrate (no CRF) -> qualità inferiore, scartato.
    arguments << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
              << "-c:v" << "libx264"
              << "-preset" << "fast"
              << "-pix_fmt" << "yuv420p"
              << "-crf" << "18"
              << "-r" << QString::number(fps) // Forza il contenitore MP4 ai tuoi FPS esatti
              << "-vframes" << QString::number(actualFramesRendered) // LA GHIGLIOTTINA: Solo N fotogrammi, vietato inventarne!
              << "-movflags" << "+faststart";

    // 4. IMPOSTAZIONI OUTPUT AUDIO (E chiusura)
    if (hasAudio) {
        arguments << "-c:a" << "aac" << "-b:a" << "192k";
        arguments << "-t" << QString::number(actualSeconds, 'f', 3); // Taglia l'audio in eccesso
    } else {
        arguments << "-t" << QString::number(actualSeconds, 'f', 3);
    }

    arguments << videoFileName;

    // ==============================================================
    // 6. GENERAZIONE VIDEO (FFMPEG VIA QPROCESS PER DESKTOP E ANDROID)
    // ==============================================================
    m_mainWindow->m_isProcessingVideo = true;
    QProcess *ffmpegProcess = new QProcess(this);

// 1. FIX FONDAMENTALE: Applichiamo la rimozione solo su vero Linux Desktop, non su Android!
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    ffmpegProcess->setProcessEnvironment(env);
#endif

    connect(ffmpegProcess, &QProcess::errorOccurred, this, [this, ffmpegProcess, releaseWatchdogAfterEncoding](QProcess::ProcessError err){
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QString errorMsg = (err == QProcess::FailedToStart)
                               ? "The FFmpeg executable does not have permission to start or has been corrupted.\nOS Block: W^X Violation."
                               : "Error while running FFmpeg.";
        QMessageBox::critical(m_mainWindow, "Video Creation Error", errorMsg);

        // DOPO la modale (vedi nota "watchdog sospeso"): il dialogo e' bloccante,
        // quindi la grazia deve partire da quando l'utente lo chiude.
        releaseWatchdogAfterEncoding();

        ffmpegProcess->deleteLater();
    });

    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, videoFileName, userSelectedFile, ffmpegProcess, releaseWatchdogAfterEncoding](int exitCode, QProcess::ExitStatus status){
                m_mainWindow->m_isProcessingVideo = false;
                m_mainWindow->m_statusLabel->clear();

                if (status == QProcess::NormalExit && exitCode == 0) {

#if defined(Q_OS_ANDROID)
            // Copia fisica sicura (Byte per Byte come in MainWindow)
            QFile inFile(videoFileName);
            if (inFile.open(QIODevice::ReadOnly)) {
                QFile outFile(userSelectedFile);
                if (outFile.exists()) outFile.remove();
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(inFile.readAll());
                    outFile.close();

                    // FORZIAMO L'AGGIORNAMENTO DEL MEDIASTORE (Appare subito in Galleria!)
                    QJniEnvironment env;
                    jstring jFilePath = env->NewStringUTF(userSelectedFile.toUtf8().constData());
                    jobjectArray pathsArray = env->NewObjectArray(1, env->FindClass("java/lang/String"), jFilePath);
                    QJniObject context = QNativeInterface::QAndroidApplication::context();
                    QJniObject::callStaticMethod<void>(
                        "android/media/MediaScannerConnection", "scanFile",
                        "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;Landroid/media/MediaScannerConnection$OnScanCompletedListener;)V",
                        context.object(), pathsArray, nullptr, nullptr
                        );
                    env->DeleteLocalRef(pathsArray);
                    env->DeleteLocalRef(jFilePath);
                }
                inFile.close();
            }
#endif

            // Pulizia profonda e totale dei file temporanei
            QDir tempDir(m_mainWindow->m_recFolder);
            if (tempDir.exists()) {
                tempDir.removeRecursively();
            }

            QMessageBox::information(m_mainWindow, "Finished!", "Video successfully saved:\n" + userSelectedFile);
        } else {
            QString log = ffmpegProcess->readAllStandardError();
            QMessageBox::warning(m_mainWindow, "Encoding Error",
                                 "FFmpeg exited with an error.\nExit code: " + QString::number(exitCode) +
                                     "\n\nLog:\n" + log.right(500));
        }

        // DOPO la modale (vedi nota "watchdog sospeso"): il dialogo e' bloccante,
        // quindi la grazia deve partire da quando l'utente lo chiude.
        releaseWatchdogAfterEncoding();

        ffmpegProcess->deleteLater();
    });

    QString program;

#if defined(Q_OS_ANDROID)
    // Su Android peschiamo il finto .so estratto nella cartella nativa dell'app
    program = QCoreApplication::applicationDirPath() + "/libffmpeg_exec.so";

    if (!QFile::exists(program)) {
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QMessageBox::critical(m_mainWindow, "Missing FFmpeg", "Cannot find static FFmpeg binary at:\n" + program);
        releaseWatchdogAfterEncoding(); // dopo la modale, come gli altri percorsi
        return;
    }

    // 2. LA MAGIA: Insegniamo al linker dove trovare libc++_shared.so (nella cartella dell'app!)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LD_LIBRARY_PATH", QCoreApplication::applicationDirPath());
    ffmpegProcess->setProcessEnvironment(env);

    // IL TRUCCO MAGICO: Linker di sistema manuale
    QStringList linkerArgs;
    linkerArgs << program << arguments;
    ffmpegProcess->start("/system/bin/linker64", linkerArgs);

#else
    // Logica originale Desktop
    program = QStandardPaths::findExecutable("ffmpeg");
    if (program.isEmpty()) {
        QStringList candidates;
#ifdef Q_OS_WIN
        candidates << "C:/ffmpeg/bin/ffmpeg.exe" << "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
#else
        candidates << "/opt/homebrew/bin/ffmpeg" << "/usr/local/bin/ffmpeg" << "/usr/bin/ffmpeg";
#endif
        for(const QString &path : candidates) {
            if (QFile::exists(path)) { program = path; break; }
        }
    }

    if (program.isEmpty()) {
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QMessageBox::critical(m_mainWindow, "Missing FFmpeg", "Cannot find 'ffmpeg'.\nMake sure it is installed and in your PATH.");
        releaseWatchdogAfterEncoding(); // dopo la modale, come gli altri percorsi
        return;
    }

    ffmpegProcess->start(program, arguments);
#endif // Q_OS_ANDROID / Desktop

#else // Q_OS_IOS || Q_OS_MACOS
    // ==============================================================
    // VERSIONE APPLE (AVFoundation Nativo con Audio Multiplexing)
    // iOS: i frame sono su disco (.raw), l'encoder li legge in createMP4.
    // macOS: i frame sono GIA' dentro l'encoder (appendFrame nel loop), qui
    //        resta solo da muxare l'audio e chiudere il file.
    // La preparazione dell'audio (raw -> WAV) e' identica sulle due: sta qui
    // una volta sola, non duplicata per piattaforma.
    // ==============================================================
    m_mainWindow->m_statusLabel->setText("Generating MP4... please wait.");
    QApplication::processEvents();

    QString finalAudioFile = "";

    // NUOVO: Se c'è audio grezzo (.raw), gli costruiamo attorno un'intestazione WAV!
    if (hasAudio) {
        if (isRaw) {
            QString wavPath = m_mainWindow->m_recFolder + "/soundtrack.wav";
            QFile rawFile(audioFile);
            QFile wavFile(wavPath);
            if (rawFile.open(QIODevice::ReadOnly) && wavFile.open(QIODevice::WriteOnly)) {
                QByteArray rawData = rawFile.readAll();
                QDataStream out(&wavFile);
                out.setByteOrder(QDataStream::LittleEndian);

                // Scrittura Header WAV (44.1kHz, Stereo, 32-bit Float)
                out.writeRawData("RIFF", 4);
                out << (quint32)(36 + rawData.size());
                out.writeRawData("WAVE", 4);
                out.writeRawData("fmt ", 4);
                out << (quint32)16 << (quint16)3 << (quint16)2 << (quint32)44100;
                out << (quint32)(44100 * 2 * 4) << (quint16)(2 * 4) << (quint16)32;
                out.writeRawData("data", 4);
                out << (quint32)rawData.size();

                wavFile.write(rawData);
                rawFile.close();
                wavFile.close();
                finalAudioFile = wavPath;
            }
        } else {
            // È già un mp3 o wav
            finalAudioFile = audioFile;
        }
    }

    // Usa il percorso scelto dall'utente tramite la finestra di dialogo
    QString finalSafePath = userSelectedFile;

    bool success = false;
    QString failDetail;

#if defined(Q_OS_IOS)
    // Le dimensioni (già pari) sono quelle catturate al primo frame RAW scritto.
    int videoW = iosFrameW;
    int videoH = iosFrameH;

    // Passiamo finalAudioFile alla funzione iOS
    success = (videoW > 0 && videoH > 0) &&
              NativeVideoEncoder::createMP4(m_mainWindow->m_recFolder, finalSafePath, fps, videoW, videoH, finalAudioFile);
#else
    // macOS: qui non resta nulla da assemblare. Video E audio sono gia' dentro
    // l'encoder, versati insieme durante il loop (l'audio DEVE avanzare coi
    // frame, vedi nativevideoencoder.h); end() chiude solo il contenitore,
    // tagliando l'audio alla durata realmente renderizzata.
    // finalAudioFile qui non si usa: e' il percorso del ramo iOS, che assembla
    // a posteriori dai .raw su disco.
    m_mainWindow->m_isProcessingVideo = true;
    if (nativeMacStarted) {
        success = NativeVideoEncoder::end();
        if (!success) failDetail = NativeVideoEncoder::lastError();
    } else {
        // begin() non e' mai riuscito (o zero frame): niente da chiudere.
        failDetail = NativeVideoEncoder::lastError();
        if (failDetail.isEmpty()) failDetail = "The video encoder did not start.";
    }
    m_mainWindow->m_isProcessingVideo = false;
#endif

    m_mainWindow->m_statusLabel->clear();

    if (success) {
        QDir tempDir(m_mainWindow->m_recFolder);
        if (tempDir.exists()) tempDir.removeRecursively();
        QMessageBox::information(m_mainWindow, "Finished!", "Video successfully saved:\n" + finalSafePath);
    } else {
        // La cartella temp (audio di lavoro) va via comunque: su macOS non
        // contiene frame, su iOS i .raw non servono piu'.
        QDir tempDir(m_mainWindow->m_recFolder);
        if (tempDir.exists()) tempDir.removeRecursively();
        QMessageBox::warning(m_mainWindow, "Encoding Error",
                             failDetail.isEmpty() ? QString("Failed to assemble the video.")
                                                  : QString("Failed to assemble the video.\n\n") + failDetail);
    }

    // DOPO la modale (vedi nota "watchdog sospeso"): il dialogo e' bloccante,
    // quindi la grazia deve partire da quando l'utente lo chiude.
    releaseWatchdogAfterEncoding();
#endif
}
