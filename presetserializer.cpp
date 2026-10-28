#include "presetserializer.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "libraryfileoperations.h"
#include "audiocontroller.h"
#include "uistylemanager.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMessageBox>
#include <QTimer>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QDir>

// --- INIZIO CUSTOM MOBILE DIALOG ---
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QMessageBox>

#ifdef Q_OS_IOS
// Sopprime il menu di modifica sul campo nome file dei dialoghi di
// salvataggio: consuma i QContextMenuEvent (long-press/fine lente del plugin
// iOS). ACCETTARE l'evento e' essenziale: il fallback nativo del plugin
// (callout UIMenuController, vivo su iPad) scatta solo se l'evento NON viene
// accettato. ImhNoEditMenu da solo NON basta (provato su device 2026-07-18:
// il plugin non consulta l'hint in questo percorso).
// (namespace anonimo: una copia gemella vive in videorecorder.cpp)
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

class MobileSaveDialog : public QDialog {
public:
    // navFloor: cartella oltre la quale ".. (Up)" NON deve salire (tipicamente la
    // radice del tipo su cui si e' tappato: surfaces/textures/records/sounds).
    // Vuota = nessun limite.
    MobileSaveDialog(const QString& title, const QString& startDir, const QString& defaultFileName,
                     QWidget* parent = nullptr, const QString& navFloor = QString())
        : QDialog(parent), currentDir(startDir) {
        if (!navFloor.isEmpty())
            m_navFloor = QDir(navFloor).absolutePath();
        setWindowTitle(title);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        pathLabel = new QLabel(currentDir.absolutePath(), this);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 12px; color: gray;");
        mainLayout->addWidget(pathLabel);

        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("Name:", this));
        nameEdit = new QLineEdit(defaultFileName, this);
        nameEdit->setStyleSheet("padding: 10px; font-size: 16px;");
#ifdef Q_OS_IOS
        // Niente menu di modifica sul campo nome file: il long-press per
        // posizionare il cursore faceva apparire il menu, che poi bloccava la
        // digitazione. Lente e posizionamento restano attivi (ImhNoTextHandles
        // ucciderebbe anche quelli, NON usarlo). Il lavoro vero lo fa
        // NoEditMenuFilter (l'hint da solo non basta, vedi commento della
        // classe); l'hint resta come dichiarazione d'intento per il plugin.
        nameEdit->setInputMethodHints(nameEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        nameEdit->installEventFilter(new NoEditMenuFilter(nameEdit));
        // Il filtro copre il QContextMenuEvent, ma NON il connect globale su
        // QInputMethod::visibleChanged (mainwindow), che ripresenta il menu su
        // qualunque editor con selezione alla chiusura della tastiera.
        nameEdit->setProperty("noEditMenu", true);
#endif
        nameLayout->addWidget(nameEdit);
        mainLayout->addLayout(nameLayout);

        listWidget = new QListWidget(this);
        listWidget->setStyleSheet("QListWidget::item { padding: 18px; border-bottom: 1px solid #ddd; font-size: 16px; }");
        listWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        mainLayout->addWidget(listWidget, 1);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton("Cancel", this);
        QPushButton* saveBtn = new QPushButton("Save", this);
        saveBtn->setObjectName("mobileSaveBtn");

        // ---> FIX 1: Impedisce ai pulsanti di intercettare il tasto Invio chiudendo il dialogo
        cancelBtn->setAutoDefault(false);
        saveBtn->setAutoDefault(false);

        cancelBtn->setStyleSheet("padding: 12px; font-size: 16px;");
        saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold;");
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);
        mainLayout->addLayout(btnLayout);

        // ---> FIX 2: Alla pressione di Invio, toglie solo il focus (su iOS la tastiera sparisce)
        connect(nameEdit, &QLineEdit::returnPressed, this, [this]() {
            nameEdit->clearFocus();
        });

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, &MobileSaveDialog::onSaveClicked);

        // Se l'utente cambia il nome digitando, resettiamo il tasto "Save"
        connect(nameEdit, &QLineEdit::textChanged, this, [this]() {
            QPushButton* btn = this->findChild<QPushButton*>("mobileSaveBtn");
            if (btn && btn->text() != "Save") {
                btn->setText("Save");
                btn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold;");
            }
        });

        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString itemName = item->data(Qt::UserRole).toString();
            QString itemType = item->data(Qt::UserRole + 1).toString();

            if (itemType == "DIR") {
                if (itemName == "..") {
                    // NON salire in una cartella che non si riesce a enumerare.
                    // Su mobile (sandbox iOS/Android) salendo troppo si arriva a
                    // directory di cui l'OS nega il listing: entryInfoList torna
                    // VUOTA, non compaiono sottocartelle e si resta INTRAPPOLATI
                    // (nulla da toccare per ridiscendere). Salire e' concesso solo
                    // se la cartella genitore e' leggibile; altrimenti si resta.
                    if (!canNavigateUp()) return;
                    currentDir.cdUp();
                }
                else currentDir.cd(itemName);
                refreshList();
            }
            else if (itemType == "FILE") {
                nameEdit->setText(itemName);
            }
        });

        refreshList();

        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            QRect screenGeom = screen->availableGeometry();
            int w = screenGeom.width() * 0.98;
            int h = screenGeom.height() * 0.92;
            this->resize(w, h);
            this->move(screenGeom.center() - this->rect().center());
        }

        mainLayout->setContentsMargins(8, 8, 8, 8);
        mainLayout->setSpacing(6);
        mainLayout->setStretch(2, 1);
    }

    QString getSelectedPath() const {
        QString name = nameEdit->text();
        if (!name.endsWith(".json", Qt::CaseInsensitive)) name += ".json";
        return currentDir.absoluteFilePath(name);
    }

private:
    // Si puo' salire di livello solo se la cartella genitore esiste, e' DIVERSA
    // da quella corrente (a filesystem root cdUp() non muove nulla) ed e'
    // effettivamente leggibile: cosi' non si finisce in una directory sandbox
    // che l'OS non lascia enumerare, da cui non si ridiscende piu'.
    bool canNavigateUp() const {
        // Non salire OLTRE il floor (radice del tipo su cui si e' tappato): se la
        // cartella corrente E' gia' il floor, l'up e' vietato.
        if (!m_navFloor.isEmpty()
            && currentDir.absolutePath() == m_navFloor)
            return false;

        QDir probe = currentDir;
        const QString currentName = probe.dirName();      // cartella in cui siamo
        const QString before = probe.absolutePath();
        if (!probe.cdUp()) return false;                  // gia' alla radice
        if (probe.absolutePath() == before) return false; // cdUp() non ha mosso
        const QFileInfo parentInfo(probe.absolutePath());
        if (!parentInfo.exists() || !parentInfo.isReadable()) return false;
        // Test decisivo su sandbox iOS/Android: isReadable() puo' mentire (i
        // permessi POSIX sembrano ok ma l'OS nega l'enumerazione). Consideriamo
        // il genitore navigabile SOLO se riusciamo a ri-vedere da li' la cartella
        // corrente tra le sue sottocartelle. Se il listing e' bloccato non la
        // ritroviamo -> non saliamo, cosi' non restiamo intrappolati.
        if (currentName.isEmpty()) return true;           // parent = filesystem root
        const QStringList siblings = probe.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        return siblings.contains(currentName);
    }

    void refreshList() {
        listWidget->clear();
        // QDir fa cache del contenuto: dopo aver navigato (o se le sottocartelle
        // sono state create dopo la costruzione del QDir) l'enumerazione puo'
        // risultare stantia/vuota. refresh() la forza a rileggere dal disco.
        currentDir.refresh();

        // Mostra ".. (Up)" solo se salire e' davvero possibile, altrimenti la
        // riga sarebbe una trappola (tap che non fa nulla o che intrappola).
        if (canNavigateUp()) {
            QListWidgetItem* upItem = new QListWidgetItem("📁 .. (Up)", listWidget);
            upItem->setData(Qt::UserRole, "..");
            upItem->setData(Qt::UserRole + 1, "DIR");
        }

        QFileInfoList dirs = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& dir : dirs) {
            QListWidgetItem* dirItem = new QListWidgetItem("📁 " + dir.fileName(), listWidget);
            dirItem->setData(Qt::UserRole, dir.fileName());
            dirItem->setData(Qt::UserRole + 1, "DIR");
        }

        QFileInfoList files = currentDir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            QListWidgetItem* fileItem = new QListWidgetItem("📄 " + file.fileName(), listWidget);
            fileItem->setData(Qt::UserRole, file.fileName());
            fileItem->setData(Qt::UserRole + 1, "FILE");
        }

        pathLabel->setText(currentDir.absolutePath());
    }

    void onSaveClicked() {
        QString rawName = nameEdit->text().trimmed();
        if (rawName.endsWith(".json", Qt::CaseInsensitive)) rawName.chop(5);
        if (rawName.isEmpty()) { nameEdit->setFocus(); return; }   // niente nome -> non salva

        QString finalPath = getSelectedPath();
        QFileInfo checkFile(finalPath);

        if (checkFile.exists()) {
            QPushButton* saveBtn = this->findChild<QPushButton*>("mobileSaveBtn");
            if (saveBtn && saveBtn->text() != "Overwrite?") {
                saveBtn->setText("Overwrite?");
                saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold; color: white; background-color: #d9534f; border-radius: 5px;");
                return;
            }
        }
        accept();
    }

    QDir currentDir;
    QString m_navFloor;   // path assoluto oltre cui non si sale (vuoto = nessun limite)
    QLabel* pathLabel;
    QListWidget* listWidget;
    QLineEdit* nameEdit;
};
#endif

// Cartella valida come punto di partenza di un dialog di salvataggio: esiste su
// disco e NON e' una risorsa qrc. Il check startsWith(":") e' necessario perche'
// QDir(":/...").exists() risponde true anche per le risorse (item builtin della
// libreria), ma i dialog non possono navigarle: il QFileDialog ripiegherebbe in
// silenzio sull'ultima cartella visitata.
static bool isUsableStartDir(const QString &dir)
{
    return !dir.isEmpty() && !dir.startsWith(":") && QDir(dir).exists();
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
// QFileDialog ISTANZIATO (non lo static getSaveFileName) per poter impostare
// setDefaultSuffix: cosi' il dialogo completa il nome con l'estensione PRIMA
// del suo controllo di sovrascrittura. Con lo static il check avveniva sul
// testo digitato: "Pippo" senza estensione non esiste -> nessun avviso, e il
// ".json" aggiunto DOPO dal chiamante sovrascriveva Pippo.json in silenzio.
// Stesso bug (e stesso fix) degli mp4 in videorecorder.cpp.
static QString getSaveFileNameWithSuffix(QWidget* parent, const QString& title,
                                         const QString& startPath, const QString& filter,
                                         const QString& suffix)
{
    QFileDialog dlg(parent, title);
    const QFileInfo fi(startPath);
    if (fi.isDir()) {
        dlg.setDirectory(startPath);
    } else {
        dlg.setDirectory(fi.absolutePath());
        dlg.selectFile(fi.fileName());
    }
    dlg.setNameFilter(filter);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    dlg.setDefaultSuffix(suffix);
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty())
        return QString();
    return dlg.selectedFiles().first();
}
#endif

PresetSerializer::PresetSerializer(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

// COLORE GLOBALE DELLA SUPERFICIE, letto dal MOTORE.
//
// E' l'unica copia: MainWindow ne teneva una (m_currentSurfaceColor), tolta
// perche' handleColorChange ci scriveva dentro anche mentre si colorava una
// MESH (il setColor successivo instrada il valore nella parte, ma la copia
// restava col colore della fascia). Salvata come colore globale, una superficie
// multi-mesh con una sola fascia colorata si riapriva TUTTA di quel colore.
//
// Il motore invece tiene i due stati separati: red/green/blue restano il colore
// globale qualunque cosa si faccia sulle parti, che vivono nei loro MeshPart.
// Trasparenza GLOBALE dal motore, per la stessa ragione di globalSurfaceColor
// qui sotto: lo slider in ambito "Mesh" mostra l'alpha della mesh selezionata,
// e salvarlo la faceva diventare l'alpha della superficie -- che le mesh senza
// valore proprio poi ereditano al reload (Hopf Tori Mesh Colors: 1 -> 0.45,
// trovato dal test di andata e ritorno). Arrotondata ai centesimi come lo
// slider, cosi' i preset gia' salvati non cambiano di un byte.
double PresetSerializer::globalSurfaceAlpha() const
{
    if (!m_mainWindow->ui->glWidget) return 1.0;
    return qRound(m_mainWindow->ui->glWidget->globalAlpha() * 100.0f) / 100.0;
}

QColor PresetSerializer::globalSurfaceColor() const
{
    if (!m_mainWindow->ui->glWidget) return QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    m_mainWindow->ui->glWidget->globalColor(r, g, b);
    return QColor::fromRgbF(r, g, b);
}

// ==========================================================
// DIALOGO DELL'HINT (suggerimento in sovrimpressione)
// ==========================================================
//
// L'hintText e' l'unico contenuto di una scena senza UI di editing: si poteva
// solo EREDITARE dal preset caricato, mai correggere dall'app. Da li' il difetto
// classico: si sposta una costante (es. il rilievo passa da A a F) e il
// messaggio continua a nominare quella vecchia, perche' nessuno lo riscrive.
// L'unico rimedio era editare il JSON a mano.
//
// Il campo appare quindi QUI, al salvataggio, invece che stabilmente
// nell'interfaccia: e' il momento in cui si sta gia' descrivendo la scena, e non
// costa spazio nel resto dell'app.
//
// Precompilato con l'hint corrente: chi non deve cambiare nulla conferma e
// basta, e soprattutto un preset che gia' aveva un messaggio non lo perde per
// il solo fatto di essere risalvato (era il difetto gemello, gia' corretto nei
// serializzatori: la chiave veniva letta al load ma scartata al save).
// Campo VUOTO = nessun hint: e' cosi' che se ne toglie uno.
//
// Ritorna false se l'utente annulla: il salvataggio va abortito, non salvato
// senza hint.
// textureHint: secondo campo, per i soli RECORD. Una scena registrata ha due
// sorgenti di costanti indipendenti -- la superficie (o il suo script) e la
// texture -- e servono due messaggi: con uno solo, quello del rilievo occupava
// il campo della scena e la superficie restava senza spiegazione, con due slider
// accesi e uno solo descritto. nullptr = tipo a messaggio singolo (superficie,
// texture), che mostra il solo campo principale.
// bgHint: terzo campo, per i soli record che HANNO uno sfondo -- il suo
// messaggio, separato dagli altri due per la stessa ragione. Il chiamante lo passa
// solo quando serve, cosi' un record senza sfondo non mostra un campo inutile.
// Un dialogo unico e non due in fila: e' un solo salvataggio, e merita una sola
// finestra da confermare.
static bool askSceneHint(QWidget* parent, QString* hintText, const QString& what,
                         QString* textureHint = nullptr, QString* bgHint = nullptr,
                         QString* borderHint = nullptr)
{
    if (!hintText) return true;

    QDialog dlg(parent);
    dlg.setWindowTitle(QString("Hint for this %1").arg(what));

    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QLabel* info = new QLabel(
        QString("Optional message shown over the scene when this %1 is loaded.\n"
                "Typically it says which slider does what "
                "(e.g. \"Slider F: relief frequency\").\n"
                "Leave it empty for no message.").arg(what), &dlg);
    info->setWordWrap(true);
    lay->addWidget(info);

    // Etichette solo quando i campi sono piu' d'uno: con uno solo sarebbero rumore.
    if (textureHint || bgHint || borderHint) lay->addWidget(new QLabel("Shape:", &dlg));

    // QPlainTextEdit e non QLineEdit: il messaggio puo' andare A CAPO, e
    // showSceneHint lo rende con setTextFormat(PlainText) proprio perche' un
    // '\n' spezzi la riga dove vuole l'autore invece che sulla larghezza della
    // finestra. Con un campo a riga singola quell'a capo non era digitabile:
    // l'Invio confermava il dialogo (era il tasto di default) e il testo
    // restava una riga sola.
    QPlainTextEdit* edit = new QPlainTextEdit(*hintText, &dlg);
    edit->setStyleSheet("padding: 8px;");
    // Due righe di altezza: quanto basta a far vedere che l'a capo si puo'
    // fare, senza rubare spazio al dialogo.
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 3 + 16);
    edit->selectAll();
#ifdef Q_OS_IOS
    // Stesse guardie del campo nome di MobileSaveDialog: senza, il long-press
    // apre il menu di modifica e poi blocca la digitazione.
    edit->setInputMethodHints(edit->inputMethodHints() | Qt::ImhNoEditMenu);
    edit->setProperty("noEditMenu", true);
#endif
    lay->addWidget(edit);

    // Secondo campo: il messaggio della TEXTURE di questa scena. Precompilato
    // come il primo, cosi' un record risalvato non lo perde.
    QPlainTextEdit* texEdit = nullptr;
    if (textureHint) {
        lay->addWidget(new QLabel("Texture:", &dlg));

        texEdit = new QPlainTextEdit(*textureHint, &dlg);
        texEdit->setStyleSheet("padding: 8px;");
        texEdit->setFixedHeight(texEdit->fontMetrics().lineSpacing() * 3 + 16);
#ifdef Q_OS_IOS
        texEdit->setInputMethodHints(texEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        texEdit->setProperty("noEditMenu", true);
#endif
        lay->addWidget(texEdit);
    }

    // Campo del BORDO, costruito come il secondo.
    QPlainTextEdit* borderEdit = nullptr;
    if (borderHint) {
        lay->addWidget(new QLabel("Border:", &dlg));

        borderEdit = new QPlainTextEdit(*borderHint, &dlg);
        borderEdit->setStyleSheet("padding: 8px;");
        borderEdit->setFixedHeight(borderEdit->fontMetrics().lineSpacing() * 3 + 16);
#ifdef Q_OS_IOS
        borderEdit->setInputMethodHints(borderEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        borderEdit->setProperty("noEditMenu", true);
#endif
        lay->addWidget(borderEdit);
    }

    // Ultimo campo: il messaggio dello SFONDO, costruito come il secondo.
    QPlainTextEdit* bgEdit = nullptr;
    if (bgHint) {
        lay->addWidget(new QLabel("Background:", &dlg));

        bgEdit = new QPlainTextEdit(*bgHint, &dlg);
        bgEdit->setStyleSheet("padding: 8px;");
        bgEdit->setFixedHeight(bgEdit->fontMetrics().lineSpacing() * 3 + 16);
#ifdef Q_OS_IOS
        bgEdit->setInputMethodHints(bgEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        bgEdit->setProperty("noEditMenu", true);
#endif
        lay->addWidget(bgEdit);
    }

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* cancel = new QPushButton("Cancel", &dlg);
    QPushButton* ok     = new QPushButton("Save", &dlg);
    // NESSUN tasto di default: l'Invio deve andare A CAPO nel messaggio, non
    // confermare il dialogo. Con setDefault(true) su Save il testo non poteva
    // avere piu' di una riga. Si conferma col tasto Save (o Cmd+Invio, che
    // QDialog accetta comunque).
    cancel->setAutoDefault(false);
    ok->setAutoDefault(false);
    ok->setDefault(false);
    cancel->setStyleSheet("padding: 8px 18px;");
    ok->setStyleSheet("padding: 8px 18px; font-weight: bold;");
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);

    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(ok,     &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.setMinimumWidth(420);
    if (dlg.exec() != QDialog::Accepted) return false;

    *hintText = edit->toPlainText().trimmed();
    if (textureHint && texEdit) *textureHint = texEdit->toPlainText().trimmed();
    if (bgHint && bgEdit) *bgHint = bgEdit->toPlainText().trimmed();
    if (borderHint && borderEdit) *borderHint = borderEdit->toPlainText().trimmed();
    return true;
}

// ==========================================================
// CATTURA DELLO STATO DELLA SCENA (tappa 2 dello "stato unico della scena")
// ==========================================================
//
// Il Save e' diviso in due: capture*State fotografa la scena in un LibraryItem
// (qui, perche' serve l'accesso a interfaccia e motore) e LibraryManager::toJson
// lo traduce in JSON, simmetrico a parseJson. Tutte le DECISIONI di contenuto
// stanno qui (script o equazioni, angoli 4D azzerati fuori dal Cross Section,
// audio e tag //IMG: nel codice delle texture); toJson conosce solo il formato.

// Limiti u/v/w: il numero sempre, la forma testuale solo quando il campo
// contiene davvero una formula ("2*A"), cosi' sopravvive al giro
// salva/ricarica invece di congelarsi nel valore del momento. Un numero puro
// non e' una formula: nessun Expr, e i preset senza costanti restano identici.
void PresetSerializer::captureParametricLimits(LibraryItem &d)
{
    const struct { QLineEdit *edit; float *value; QString *expr; } fields[] = {
        { m_mainWindow->ui->uMinEdit, &d.uMin, &d.uMinExpr }, { m_mainWindow->ui->uMaxEdit, &d.uMax, &d.uMaxExpr },
        { m_mainWindow->ui->vMinEdit, &d.vMin, &d.vMinExpr }, { m_mainWindow->ui->vMaxEdit, &d.vMax, &d.vMaxExpr },
        { m_mainWindow->ui->wMinEdit, &d.wMin, &d.wMinExpr }, { m_mainWindow->ui->wMaxEdit, &d.wMax, &d.wMaxExpr },
    };
    for (const auto &f : fields) {
        const QString raw = m_mainWindow->lineText(f.edit).trimmed();
        *f.value = m_mainWindow->parseLimitField(raw);
        bool isPlainNumber = false;
        QString normalized = raw;
        normalized.replace(',', '.');
        normalized.toFloat(&isPlainNumber);
        *f.expr = (!raw.isEmpty() && !isPlainNumber) ? raw : QString();
    }
}

// Taglio x/y/z del Ray Marching: stessa regola dei limiti u/v/w, ma il campo
// vuoto vale "nessun taglio" (il default largo), non zero.
void PresetSerializer::captureSpaceLimits(LibraryItem &d)
{
    const struct { QLineEdit *edit; float def; float *value; QString *expr; } fields[] = {
        { m_mainWindow->ui->lineXMin, -1000.0f, &d.xMin, &d.xMinExpr }, { m_mainWindow->ui->lineXMax, 1000.0f, &d.xMax, &d.xMaxExpr },
        { m_mainWindow->ui->lineYMin, -1000.0f, &d.yMin, &d.yMinExpr }, { m_mainWindow->ui->lineYMax, 1000.0f, &d.yMax, &d.yMaxExpr },
        { m_mainWindow->ui->lineZMin, -1000.0f, &d.zMin, &d.zMinExpr }, { m_mainWindow->ui->lineZMax, 1000.0f, &d.zMax, &d.zMaxExpr },
    };
    for (const auto &f : fields) {
        const QString raw = m_mainWindow->lineText(f.edit).trimmed();
        *f.value = raw.isEmpty() ? f.def : m_mainWindow->parseLimitField(raw);
        bool isPlainNumber = false;
        QString normalized = raw;
        normalized.replace(',', '.');
        normalized.toFloat(&isPlainNumber);
        *f.expr = (!raw.isEmpty() && !isPlainNumber) ? raw : QString();
    }
}

// Cio' che superfici e record catturano allo stesso modo.
void PresetSerializer::captureCommonState(LibraryItem &d)
{
    MainWindow *mw = m_mainWindow;
    GLWidget *gl = mw->ui->glWidget;

    // --- Ray Marching: i due sotto-tab hanno editor SEPARATI (vedi CLAUDE.md).
    // Salvare solo lineEquation scriveva l'equazione del ramo 3D anche quando la
    // superficie visibile era quella del Cross Section: al ricaricamento
    // compariva la sfera di default. Si scrivono entrambi i rami piu' quale era
    // attivo, cosi' il load sa cosa ripristinare.
    d.isImplicitMode = (mw->implicitMode());
    d.implicitEq = mw->m_scene.rm.equation;
    d.usesCrossSection = d.isImplicitMode && mw->crossSectionTab();
    d.crossSectionEq = mw->m_scene.rm.crossSection;

    // TUBO: la geometria e' la curva del sotto-tab Tubes; le equazioni di
    // Surface (nascoste) non sono della scena e si scrivono vuote.
    d.isTube = mw->tubesShown();
    if (d.isTube) {
        d.tubeX = mw->m_scene.tube.x;
        d.tubeY = mw->m_scene.tube.y;
        d.tubeZ = mw->m_scene.tube.z;
        d.tubeP = mw->m_scene.tube.p;
        d.tubeThickness = mw->m_scene.tube.thickness;
    }
    d.x = d.isTube ? QString() : mw->m_scene.eq.x;
    d.y = d.isTube ? QString() : mw->m_scene.eq.y;
    d.z = d.isTube ? QString() : mw->m_scene.eq.z;
    d.w = d.isTube ? QString() : mw->m_scene.eq.p;
    d.explicitU = mw->m_scene.eq.explicitU;
    d.explicitV = mw->m_scene.eq.explicitV;
    d.explicitW = mw->m_scene.eq.explicitW;
    d.defU = mw->m_scene.eq.u;
    d.defV = mw->m_scene.eq.v;
    d.defW = mw->m_scene.eq.w;

    d.geoU0 = mw->m_scene.eq.geoU;
    d.geoV0 = mw->m_scene.eq.geoV;
    d.geoW0 = mw->m_scene.eq.geoW;
    d.geoDU = mw->m_scene.eq.geoDU;
    d.geoDV = mw->m_scene.eq.geoDV;
    d.geoDW = mw->m_scene.eq.geoDW;
    d.geoConform = mw->m_scene.eq.conform;

    // Le costanti si leggono dai CAMPI TESTO (lineA..lineS), non dagli slider.
    // Gli slider sono interi centesimali (value()/100), quindi troncano ogni
    // valore con piu' di 2 decimali o < 0.005 -> es. A=0.005 diventava 0,
    // salvando una costante SBAGLIATA. I campi testo sono la source-of-truth
    // (piena precisione, espressioni a cascata); resolveCascadeConstants(false)
    // li risolve esattamente come evaluateCascade, senza toccare il testo.
    const MainWindow::CascadeConstants kc = mw->resolveCascadeConstants(false);
    d.a = kc.a; d.b = kc.b; d.c = kc.c; d.d = kc.d; d.e = kc.e; d.f = kc.f; d.s = kc.s;

    // Costanti discrete SENZA script: stessa semantica di "A := int(2,6);" ma
    // dichiarata dal preset (le direttive := vivono solo nello scriptCode).
    d.discreteConstants.clear();
    for (auto it = mw->m_scene.discreteConsts.constBegin(); it != mw->m_scene.discreteConsts.constEnd(); ++it)
        d.discreteConstants.insert(it.key(), qMakePair(it->lo, it->hi));

    captureParametricLimits(d);
    captureSpaceLimits(d);

    d.steps = mw->m_scene.steps;
    d.meshCount = mw->m_scene.meshCount;

    // Colore e trasparenza GLOBALI dal motore, mai dai controlli: in ambito
    // "Mesh" mostrano la mesh selezionata (vedi globalSurfaceColor/Alpha).
    d.surfaceColor = globalSurfaceColor();
    d.color1 = d.surfaceColor.name();
    d.hasCustomColors = true;
    d.alpha = globalSurfaceAlpha();

    d.lightingMode = mw->m_scene.lightingMode4D;
    // Luce GLOBALE dal motore, che ne tiene l'unica copia (lo slider ne e' la
    // vista, anche in ambito "Mesh"). Ai centesimi come lo slider, come l'alpha.
    d.lightIntensity = qRound(gl->globalLightIntensity() * 100.0f) / 100.0;
    // Luce di riempimento (dock Renderer). Senza, lo slider non tornava mai
    // indietro: il valore restava quello della scena precedente.
    d.fillLight = gl->fillLight();
    // Bordo (dock Renderer): il motore ne tiene l'unica copia.
    {
        const GLWidget::BorderStyle b = gl->borderStyle();
        d.borderThickness = b.radius;
        d.borderColor = b.color;
        d.borderMode = b.mode;
        d.borderWireAlong = b.wireAlong;
        d.borderWireAround = b.wireAround;
        d.borderTexCode = b.texCode;
        d.borderTexEnabled = b.texEnabled;
        d.borderTexCol1 = b.texCol1;
        d.borderTexCol2 = b.texCol2;
        d.borderTexZoom = b.texZoom;
        d.borderTexPanX = b.texPan.x();
        d.borderTexPanY = b.texPan.y();
        d.borderTexRotation = b.texRotation;
        d.borderTexLibName = b.texLibName;
        d.borderTexHintText = b.texHint;
        d.borderTexHintSeconds = b.texHintSeconds;
    }
    d.use4DLighting = gl->is4DActive();
    if (d.isImplicitMode) {
        // Shell/Solid da implicitShellSelected(), il punto unico che legge lo
        // stato (e' lui che i gate e il Run consultano); nel file la codifica e'
        // composita: +10 = Shell.
        d.renderMode = mw->m_scene.renderMode + (mw->implicitShellSelected() ? 10 : 0);
    } else {
        d.renderMode = mw->m_scene.renderMode;
    }
    // Spessore del guscio: dipende da come e' scritta l'equazione, quindi e' un
    // parametro della superficie. Marcher: quale dei due radio (Fast/Precise).
    d.shellThickness = gl->shellThickness();
    d.hybridMarcher = gl->hybridMarcher();
    d.projectionMode = gl->projectionMode;
    // cameraFov = chiave legacy (unico FOV applicato); fov3D/fov4D allineati.
    d.cameraFov = gl->cameraFov();
    d.fov3D = mw->m_scene.fov;
    d.fov4D = mw->m_scene.fov;

    // Densita' wireframe A SCHERMO in questo momento, non il default.
    d.hasWireframe = true;
    d.wireframeUStep = gl->getWireframeUStep();
    d.wireframeVStep = gl->getWireframeVStep();

    // ASPETTO PER-MESH: le parti cosi' come sono nel motore; toJson ne scrive
    // solo cio' che e' personalizzato.
    if (SurfaceEngine *eng = gl->getEngine()) {
        d.meshParts = eng->meshPartsForSave();   // anche le mesh oltre il numero attuale
        // AMBITO All/Mesh. A mesh SINGOLA e' "All" per definizione (e' cio' che
        // decide il load, applyPendingMeshScope) e il radio non si guarda: puo'
        // essere ancora quello della superficie precedente quando il load non
        // rigenera la griglia (script metrici: Kerr dopo Hopf Tori).
        d.meshScopeAll = eng->getMeshPartCount() <= 1 || mw->meshScopeAll();
        // Dominio dell'ambito "All": solo se impostato.
        d.hasAllDomain = eng->hasAllDomain();
        if (d.hasAllDomain) eng->allDomain(d.allUMin, d.allUMax, d.allVMin, d.allVMax);
    }

    // STATO 4D: azzerato in Ray Marching SOLO fuori dal Cross Section. Lo zero
    // risale a quando il ray marching non leggeva omega/phi/psi; nel Cross
    // Section invece sono lo STATO PRINCIPALE (decidono quale sezione
    // dell'ipersuperficie 4D si vede, %CROSS_SECTION_P%).
    const bool keep4D = !d.isImplicitMode || d.usesCrossSection;
    d.restoreAngles = true;
    d.startOmega = keep4D ? gl->getOmega() : 0.0f;
    d.startPhi   = keep4D ? gl->getPhi()   : 0.0f;
    d.startPsi   = keep4D ? gl->getPsi()   : 0.0f;
    // Traslazione del piano di sezione lungo p: l'altra meta' dello stato 4D
    // del Cross Section (toJson la scrive solo li').
    d.crossSectionP = gl->crossSectionP();

    // Telecamera 3D. Yaw/pitch EFFETTIVI, non i campi grezzi: in modalita' path
    // la vista passa da lookAt(pos, pathTarget) e i campi grezzi restano al
    // valore di prima -- il record si riapriva con la direzione sbagliata.
    d.hasCamera3D = true;
    const QVector3D camPos = gl->getCameraPos();
    d.camX = camPos.x(); d.camY = camPos.y(); d.camZ = camPos.z();
    const QQuaternion rot = gl->getRotationQuat();
    d.rotW = rot.scalar(); d.rotX = rot.x(); d.rotY = rot.y(); d.rotZ = rot.z();
    d.camYaw = gl->getEffectiveCameraYaw();
    d.camPitch = gl->getEffectiveCameraPitch();
    d.camRoll = gl->getCameraRoll();

    // Suggerimento in sovrimpressione: non ha UI di editing, si riscrive quello
    // caricato (senza, ogni Save lo perdeva).
    d.hintText = mw->m_currentHintText;
    d.hintSeconds = mw->m_currentHintSeconds;
}

LibraryItem PresetSerializer::captureSurfaceState(const QString &name)
{
    MainWindow *mw = m_mainWindow;
    LibraryItem d;
    d.name = name;
    d.type = LibraryType::Surface;
    captureCommonState(d);
    // Il colore di sfondo si salva anche nelle Shape (la texture di sfondo no).
    d.bgColor = mw->m_scene.bgColor.name();

    // Chi comanda la geometria. Script metrico: i campi x/y/z/p non sono vuoti
    // (carta identita' o display map), ma la geometria la definisce lo script:
    // senza questo caso il preset si salvava come parametrico e lo script
    // spariva.
    const QString eqX = d.x.trimmed(), eqY = d.y.trimmed(), eqZ = d.z.trimmed();
    const bool isImplicitScript = d.isImplicitMode && d.implicitEq.contains("// Controlled by Script");
    // Un tubo ha X/Y/Z di Surface vuoti, ma e' uno script solo se la curva la
    // da' lo script (campi della curva vuoti, vedi tubeSceneFromScript).
    const bool isParametricScript = !d.isImplicitMode
                                    && (d.isTube ? mw->tubeSceneFromScript()
                                                 : eqX.isEmpty() && eqY.isEmpty() && eqZ.isEmpty());
    const bool isMetricScript = !d.isImplicitMode && !mw->m_metricScriptBody.trimmed().isEmpty();
    d.isScript = isImplicitScript || isParametricScript || isMetricScript;
    if (d.isScript) {
        d.scriptCode = mw->m_scene.surfaceScriptApplied;
        // Fallback di sicurezza se l'applicato e' sfuggito: lo script com'e' scritto
        if (d.scriptCode.isEmpty() && mw->m_currentScriptMode == 0)
            d.scriptCode = mw->m_scene.surfaceScriptText;
    }

    // Mappa di visualizzazione di uno script metrico: si salva solo se e' una
    // mappa custom (es. Flamm), non la carta identita'.
    d.hasMetricMap = !mw->m_metricScriptBody.trimmed().isEmpty() && mw->metricDisplayMapIsCustom();
    if (d.hasMetricMap) {
        d.metricMapX = d.x; d.metricMapY = d.y; d.metricMapZ = d.z; d.metricMapP = d.w;
    }
    return d;
}

LibraryItem PresetSerializer::captureMotionState(const QString &name, const MotionRunState &run,
                                                 bool includeSound)
{
    MainWindow *mw = m_mainWindow;
    GLWidget *gl = mw->ui->glWidget;
    LibraryItem d;
    d.name = name;
    d.type = LibraryType::Motion;
    captureCommonState(d);

    // Lo script si salva ACCANTO alle equazioni quando e' lui a comandare, o
    // quando e' metrico (i campi portano la carta identita' / display map).
    bool usingEquations = !d.x.trimmed().isEmpty() && d.x.trimmed() != "0";
    if (d.isImplicitMode) usingEquations = !d.implicitEq.contains("// Controlled by Script");
    QString scriptContent = mw->m_scene.surfaceScriptApplied;
    if (scriptContent.isEmpty() && mw->m_currentScriptMode == 0)
        scriptContent = mw->m_scene.surfaceScriptText;
    const bool metricScriptActive = !mw->m_metricScriptBody.trimmed().isEmpty();
    // Tubo: lo script solo se e' lui a dare la curva (campi vuoti).
    const bool tubeScript = d.isTube && mw->tubeSceneFromScript();
    d.isScript = (!d.isTube || tubeScript) && !scriptContent.trimmed().isEmpty()
                 && (!usingEquations || metricScriptActive);
    d.scriptCode = (d.isTube && !tubeScript) ? QString() : scriptContent;

    d.path4D_x = mw->m_scene.path.x;
    d.path4D_y = mw->m_scene.path.y;
    d.path4D_z = mw->m_scene.path.z;
    d.path4D_w = mw->m_scene.path.p;
    d.path4D_alpha = mw->m_scene.path.alpha;
    d.path4D_beta  = mw->m_scene.path.beta;
    d.path4D_gamma = mw->m_scene.path.gamma;
    d.path3D_x = mw->m_scene.path.x3D;
    d.path3D_y = mw->m_scene.path.y3D;
    d.path3D_z = mw->m_scene.path.z3D;
    d.path3D_roll = mw->m_scene.path.roll3D;

    // Il record porta DUE messaggi: quello della scena (hintText, gia' in
    // captureCommonState) e quello della TEXTURE.
    d.textureHintText = mw->m_currentTextureHintText;
    d.textureHintSeconds = mw->m_currentTextureHintSeconds;

    // Vista corrente di ENTRAMBI i path (salvare solo quella 4D faceva
    // ripartire i record 3D sempre in Tangent).
    d.pathMode4D = static_cast<int>(mw->m_scene.pathViewMode4D);
    d.pathMode3D = static_cast<int>(mw->m_scene.pathViewMode3D);
    // Moto camera CORRENTE al salvataggio; se nessuno e' in corsa, l'ULTIMO
    // avviato in sessione (senza fallback si scriveva "none" e il load
    // ricadeva nella sequenza legacy, che fa sempre vincere il path 4D).
    d.activeMotion = run.path3D ? QStringLiteral("path3D")
                   : run.path4D ? QStringLiteral("path4D")
                   : run.rotating ? QStringLiteral("rotation")
                   : !mw->m_scene.lastCameraMotion.isEmpty() ? mw->m_scene.lastCameraMotion
                                                       : QStringLiteral("none");

    // Codice delle texture parametriche: lo SCRIPT (surfaceTextureScript /
    // backgroundTextureScript), cioe' l'editor se le mostra, altrimenti il loro
    // slot. Uno script scritto e non ancora eseguito entra nel record, com'e'
    // sempre stato, qualunque modulo il dock stia mostrando. La cattura non
    // scrive piu' nulla: travasava l'editor nelle copie APPLICATE, e dopo un
    // Save uno script mai eseguito risultava applicato (orologio, costanti e
    // focus in Library lo giudicavano) mentre il motore disegnava il vecchio.

    // Accensione della texture di SUPERFICIE: l'INTENZIONE (m_scene.surfaceTextureState),
    // l'unica copia che cambia solo col checkbox della superficie. Checkbox e
    // motore ne sono viste: il checkbox mostra lo sfondo o la fascia quando si
    // edita quelli, e in WIREFRAME entrambi dicono "spenta" perche' la texture
    // non si disegna. Salvare la vista perdeva la texture: un record salvato in
    // wireframe si riapriva con la texture spenta, e tornando a Phong non
    // ricompariva piu' (dal vivo si'). Trovato dal test degli scenari.
    d.textureEnabled = mw->m_scene.surfaceTextureState;
    // Trasformazione e colori GLOBALI dal motore, non il buffer della vista 2D
    // ne' i picker (che seguono la fascia selezionata).
    d.zoom = gl->globalTexZoom();
    const QVector2D pan = gl->globalTexPan();
    d.panX = pan.x(); d.panY = pan.y();
    d.rotation = gl->globalTexRotation();
    d.texColor1 = gl->globalTexColor1().name();
    d.texColor2 = gl->globalTexColor2().name();

    // --- AUDIO E IMMAGINI DENTRO IL CODICE DELLA TEXTURE ---
    // Blocco audio omesso solo se chi salva l'ha scelto (dialogo "The sound is
    // stopped." in saveMotion, che decide includeSound).
    // soundCode(): un GLSL scritto a mano nel dock Sound si scriveva nudo, in
    // mezzo al codice della texture, che al reload non compilava piu'.
    const QString audioCode = includeSound ? mw->soundCode() : QString();
    const QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
                                     QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression musicRe(R"(^\s*//(MUSIC|SYNTH):.*$\n?)",
                                     QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression tagRe(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)",
                                   QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if (d.isImplicitMode) {
        QString implicitTex = mw->m_scene.rm.texture.trimmed();
        // Via i vecchi tag audio: si riaggiunge quello pulito.
        while (implicitTex.contains(blockRe)) implicitTex.remove(blockRe);
        implicitTex.remove(musicRe);
        implicitTex.remove(tagRe);
        if (!audioCode.isEmpty()) {
            // IL TAG //IMG: RESTA ALLA RIGA 1, l'audio va SOTTO (stessa regola del
            // ramo parametrico). "\n" singolo dopo l'audio: al load la rimozione
            // dell'audio lascerebbe altrimenti una riga vuota fra tag e script.
            const QRegularExpression imgFirstRe(R"(^\s*//IMG:.*$)", QRegularExpression::MultilineOption);
            const QRegularExpressionMatch imgFirst = imgFirstRe.match(implicitTex);
            if (imgFirst.hasMatch()) {
                const QString tag = imgFirst.captured(0).trimmed();
                const QString rest = implicitTex.mid(imgFirst.capturedEnd(0)).trimmed();
                implicitTex = tag + "\n" + audioCode + "\n" + rest;
            } else {
                implicitTex = audioCode + "\n\n" + implicitTex.trimmed();
            }
        }
        d.textureCode = implicitTex;
        d.displacementCode = mw->m_scene.rm.displacement;
    } else {
        QString code = mw->surfaceTextureScript();
        while (code.contains(blockRe)) code.remove(blockRe);
        code.remove(musicRe);
        code.remove(tagRe);
        code = code.trimmed();
        // Il tag //IMG: va SEMPRE ripulito prima: se l'immagine non e' piu' la
        // texture attiva un tag orfano finiva nel record e al reload la Library
        // evidenziava la vecchia immagine.
        code.remove(QRegularExpression(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption));
        code = code.trimmed();
        // Se c'e' un'immagine, il tag //IMG: sta sempre alla riga 1.
        // L'immagine e' quella nel motore (MainWindow::surfaceImagePath).
        if (d.textureEnabled && mw->surfaceHasImage()) {
            QString withImg = "//IMG:" + mw->surfaceImagePath() + "\n";
            if (!audioCode.isEmpty()) withImg += audioCode + "\n\n";
            code = withImg + code;
        } else if (!audioCode.isEmpty()) {
            code = audioCode + "\n\n" + code;
        }
        d.textureCode = code.trimmed();
    }
    // Ancore in libreria (focus nell'albero anche se il codice e' cambiato).
    // Il suono solo se il record porta davvero un audio: un nome senza suono
    // direbbe il falso.
    d.textureLibName = mw->m_scene.textureLibName;
    d.soundLibName = audioCode.isEmpty() ? QString() : mw->m_scene.soundLibName;

    // Velocita'. Quelle 4D (e il path 4D) seguono lo stesso criterio degli
    // angoli: nel Cross Section sono il MOTO del record, fuori sono azzerate.
    const bool keep4D = !d.isImplicitMode || d.usesCrossSection;
    d.speedNut = gl->getNutationSpeed();
    d.speedPrec = gl->getPrecessionSpeed();
    d.speedSpin = gl->getSpinSpeed();
    d.speedOmega = keep4D ? gl->getOmegaSpeed() : 0.0f;
    d.speedPhi   = keep4D ? gl->getPhiSpeed()   : 0.0f;
    d.speedPsi   = keep4D ? gl->getPsiSpeed()   : 0.0f;
    d.speedPath3D = mw->m_scene.pathSpeed3D;
    d.speedPath4D = keep4D ? mw->m_scene.pathSpeed4D : 0;

    d.observer4D = gl->getObserverPos4D();

    // --- SFONDO ---
    d.bgColor = mw->m_scene.bgColor.name();
    d.bgTextureEnabled = gl->isBackgroundTextureEnabled();
    // Il tag //IMG: dello sfondo si ricostruisce dall'immagine nel motore
    // (MainWindow::backgroundImagePath, vuoto con la default): gli
    // script "Animated Images" campionano l'immagine da iChannel0 e il tag che
    // dice QUALE si perde ai Run che riscrivono il codice. Senza "\n" in coda
    // per un'immagine PURA: e' la forma su cui il focus in Library fa match.
    {
        QString bgCode = mw->backgroundTextureScript();
        bgCode.remove(QRegularExpression(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption));
        bgCode = bgCode.trimmed();
        const QString bgImage = mw->backgroundImagePath();
        if (!bgImage.isEmpty()) {
            bgCode = bgCode.isEmpty()
                       ? "//IMG:" + bgImage
                       : "//IMG:" + bgImage + "\n" + bgCode;
        }
        d.bgTextureCode = bgCode;
    }
    d.bgCol1 = mw->m_scene.bgTexColor1.name();
    d.bgCol2 = mw->m_scene.bgTexColor2.name();
    d.bgLibName = mw->m_scene.bgTextureLibName;
    d.bgHintText = mw->m_currentBgTextureHintText;
    d.bgHintSeconds = mw->m_currentBgTextureHintSeconds;
    // Forma dello sfondo: si scrive SEMPRE, anche "fixed" (il load la riapplica).
    d.bgSkyMode = GLWidget::bgSkyModeName(gl->backgroundSkyMode());
    // Inquadratura dello sfondo letta direttamente: prima si commutava il
    // bersaglio della vista 2D avanti e indietro per farla restituire ai
    // getFlat*(), e fuori dalla vista 2D quel giro scriveva sulla mesh
    // selezionata un buffer stantio (vedi 603a0f1).
    d.bgZoom = gl->backgroundZoom();
    const QVector2D bgPan = gl->backgroundPan();
    d.bgPanX = bgPan.x(); d.bgPanY = bgPan.y();
    d.bgRotation = gl->backgroundRotation();
    return d;
}

QJsonObject PresetSerializer::buildSurfaceJson(const QString &name)
{
    return LibraryManager::toJson(captureSurfaceState(name));
}

QJsonObject PresetSerializer::buildMotionJson(const QString &name, const MotionRunState &run,
                                              bool includeSound)
{
    return LibraryManager::toJson(captureMotionState(name, run, includeSound));
}

bool PresetSerializer::refuseEmptyShape()
{
    // Una Shape E' la superficie: da una scena vuota (NEW, o col solo sfondo)
    // si scriveva un file con le equazioni vuote, che riaperto come Shape
    // faceva validare al Run del load equazioni inesistenti ("exactly 2 or 3
    // valid spatial variables"). Lo sfondo si tiene in un Record.
    if (!m_mainWindow->isSceneEmpty()) return false;
    QMessageBox::information(m_mainWindow, "Save Shape",
                             "There is no surface to save as a Shape.\n\n"
                             "To keep the background, save a Record.");
    return true;
}

bool PresetSerializer::refuseEmptyTexture()
{
    // Il bersaglio senza texture (codice e immagine vuoti): si scriveva il
    // segnaposto "// Texture Preset", che riaperto mostrava il gradiente di
    // ripiego dello shader. E' la stessa condizione che spegne il Save della
    // texture (hasSavableTexture); i comandi del menu della Library non la
    // guardavano.
    if (m_mainWindow->hasSavableTexture()) return false;
    const QString target = m_mainWindow->editingBorder()     ? QStringLiteral("border")
                         : m_mainWindow->editingBackground() ? QStringLiteral("background")
                                                             : QStringLiteral("surface");
    QMessageBox::information(m_mainWindow, "Save Texture",
                             QStringLiteral("There is no %1 texture to save: the default one is built in.").arg(target));
    return true;
}

bool PresetSerializer::refuseEmptyRecord()
{
    // Un Record tiene anche lo sfondo e il suono: e' vuota solo la scena senza
    // superficie (NEW), senza sfondo e senza suono. Altrimenti si apriva il
    // dialogo per salvare un file che non conteneva niente.
    MainWindow *mw = m_mainWindow;
    if (mw->isSceneEmpty()
        && mw->m_scene.bgTextureCode.trimmed().isEmpty()
        && mw->m_scene.soundScriptText.trimmed().isEmpty()) {
        QMessageBox::information(mw, "Save Record",
                                 "There is nothing to save as a Record: the scene is empty.");
        return true;
    }
    return false;
}

bool PresetSerializer::refuseEmptySound()
{
    if (!m_mainWindow->m_scene.soundScriptText.trimmed().isEmpty()) return false;
    QMessageBox::information(m_mainWindow, "Save Sound",
                             "There is no sound to save: write one in the Sound editor or pick one from the Library.");
    return true;
}

void PresetSerializer::saveSurface(const QString &suggestedPath)
{
    if (refuseEmptyShape()) return;
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    // UN SOLO dominio di preferenze. Qui c'era `QSettings settings("Repository")`:
    // quel costruttore prende l'ORGANIZATION NAME, non un gruppo, quindi scriveva
    // in un dominio separato -- e "lastFolder" (piu' sotto) finiva la' dentro,
    // mentre saveSurfaceAs scrive la stessa chiave nel dominio globale. Due chiavi
    // omonime in due domini: vinceva quella salvata per ultima, e il dialogo Save
    // ripartiva da una cartella apparentemente casuale.
    QSettings settings;
    QString rootPath = settings.value("libraryRootPath").toString();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Mobile: la radice si ricalcola dal sistema a ogni chiamata, non passa da
    // QSettings, quindi qui un fallback e' corretto e non puo' sbagliare posto.
    if (rootPath.isEmpty()) {
#if defined(Q_OS_ANDROID)
        // Cartella Download pubblica, bypassando la sandbox di Qt
        rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#else
        rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#endif
    }
#else
    // DESKTOP: radice vuota NON si indovina in silenzio.
    //
    // Qui si ricadeva su <Documents>/SurfaceExplorer_Presets: mkpath, open e write
    // riuscivano, la scena risultava salvata e l'utente riceveva la conferma di
    // un salvataggio che non avrebbe trovato dove lo cercava. E' il meccanismo che
    // ha prodotto due librerie in posti diversi (una su Download, una su
    // Documents) con le modifiche che finivano in quella sbagliata: una radice
    // scelta QUI, di nascosto, non e' la stessa che il resto dell'app usa.
    //
    // La libreria si installa da un punto solo -- setupDefaultFolders -- che
    // chiede dove. Fuori sandbox, se l'utente non sceglie, usa la cartella
    // predefinita dichiarandola; SOTTO sandbox rinuncia (nessun fallback e'
    // utilizzabile la'), quindi al ritorno la radice puo' essere ancora vuota: in
    // quel caso non si scrive da nessuna parte e si ripristina lo stato del moto,
    // come per il dialogo annullato piu' sotto.
    if (rootPath.isEmpty()) {
        m_mainWindow->setupDefaultFolders();
        rootPath = QSettings().value("libraryRootPath").toString();
        if (rootPath.isEmpty()) {
            if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
            if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
            return;
        }
    }
#endif

    QString fileName;
    if (!suggestedPath.isEmpty() && suggestedPath.endsWith(".json", Qt::CaseInsensitive)) {
        fileName = suggestedPath;
    } else {
        QString startPath = suggestedPath;

        if (!isUsableStartDir(startPath)) {
            QTreeWidgetItem *selItem = m_mainWindow->getCurrentLibraryItem();
            if (selItem) {
                if (selItem->data(0, Qt::UserRole + 10).isValid()) {
                    startPath = selItem->data(0, Qt::UserRole + 10).toString(); // È una cartella
                } else {
                    startPath = QFileInfo(selItem->toolTip(0)).absolutePath(); // È un file
                }
            }
            // Se selezioni il NODO di categoria "Surfaces" (non una foglia) il
            // path ricavato e' vuoto/non valido (o e' una risorsa ":/" di un
            // item builtin): cadi qui. Il fallback deve puntare a una cartella
            // GARANTITA esistente, altrimenti su iOS il dialog apre una
            // directory inesistente -> lista vuota + salvataggio fallito (su
            // Android non capita perche' rootPath e' un percorso pubblico fisso
            // sempre presente).
            if (!isUsableStartDir(startPath)) {
                startPath = settings.value("lastFolder", rootPath + "/surfaces").toString();
            }
            if (!isUsableStartDir(startPath)) {
                startPath = rootPath + "/surfaces";
            }
        }

        // Garantiamo che la cartella di partenza esista davvero (prima
        // scrittura su iOS: il container puo' non avere ancora /surfaces).
        {
            QString startDir = startPath.endsWith(".json", Qt::CaseInsensitive)
                                   ? QFileInfo(startPath).absolutePath()
                                   : startPath;
            if (!startDir.isEmpty() && !QDir(startDir).exists())
                QDir().mkpath(startDir);
        }

        if (!startPath.endsWith(".json", Qt::CaseInsensitive)) {
            if (!startPath.endsWith("/")) startPath += "/";
            startPath += "NewShape.json";
        }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        MobileSaveDialog dialog("Save Shape", QFileInfo(startPath).absolutePath(), QFileInfo(startPath).completeBaseName(), m_mainWindow, rootPath + "/surfaces");
        if (dialog.exec() != QDialog::Accepted) {
            if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
            if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
            return;
        }
        fileName = dialog.getSelectedPath();
#else
        fileName = getSaveFileNameWithSuffix(m_mainWindow, "Save Shape", startPath, "JSON Files (*.json)", "json");
#endif
    }

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // --- BLOCCO VALIDAZIONE RIGIDA ---
    // Il ramo per posizione (LibraryFolders::branchOf), non per nome: una
    // sottocartella che si chiama come un altro ramo non conta.
    const LibraryFolders::Branch branch = LibraryFolders::branchOf(QFileInfo(fileName).absolutePath());
    if (branch == LibraryFolders::Branch::Records ||
        branch == LibraryFolders::Branch::Textures ||
        branch == LibraryFolders::Branch::Sounds) {
        QMessageBox::warning(m_mainWindow, "Save Blocked",
                             "Operation not allowed.\n\nSurfaces and solid curves must be saved in Shapes (the 'surfaces' folder).\nTo save the whole scene (which contains this surface), use Save Record.");
        return;
    }

    // HINT della scena, come per record e texture (vedi askSceneHint): dopo la
    // scelta del percorso e dopo il blocco di validazione, prima della scrittura.
    if (!askSceneHint(m_mainWindow, &m_mainWindow->m_currentHintText, "surface"))
        return;

    QFileInfo fileInfo(fileName);
    settings.setValue("lastFolder", fileInfo.absolutePath());

    const QJsonObject root = buildSurfaceJson(QFileInfo(fileName).baseName());

    QDir().mkpath(QFileInfo(fileName).absolutePath()); // 1. Crea la cartella se manca
    QFile file(fileName);

    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove(); // 2. Distrugge il file lucchettato da iOS per poterlo ricreare
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        // Il lavoro e' su disco: non c'e' piu' niente da proteggere. Lo legge
        // confirmDiscardUnsaved per capire se il "Save" e' andato a buon
        // fine o se l'utente ha annullato il dialogo (i return anticipati qui
        // sopra lasciano l'esito a false, e il reset viene giustamente sospeso).
        m_mainWindow->markSurfaceSaved();
        m_mainWindow->m_lastSaveSucceeded = true;

        QTimer::singleShot(100, m_mainWindow, [this, fileName]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSurfaces, fileName);
        });
    } else {
        QMessageBox::critical(m_mainWindow, "Error", "Could not write to file.");
    }
}

void PresetSerializer::saveTexture(const QString &path)
{
    if (refuseEmptyTexture()) return;
    const LibraryFolders::Branch branch = LibraryFolders::branchOf(QFileInfo(path).absolutePath());
    if (branch == LibraryFolders::Branch::Surfaces ||
        branch == LibraryFolders::Branch::Records ||
        branch == LibraryFolders::Branch::Sounds) {
        QMessageBox::warning(m_mainWindow, "Save Blocked",
                             "Operation not allowed.\n\nTexture presets must be saved exclusively in the 'Textures' folder.");
        return;
    }

    // HINT della texture: stesso trattamento del record (vedi askSceneHint), ma
    // sulla variabile della TEXTURE, che ha una chiave sua. Sono due messaggi
    // indipendenti proprio perche' possono nominare costanti diverse: la texture
    // d'origine dice la sua (es. "Slider A"), il record che la riusa puo' averla
    // spostata su un'altra costante e dire la propria.
    // Con il bersaglio Background si salva lo SFONDO, e il messaggio e' il suo:
    // prima si chiedeva (e si scriveva nel preset) quello della texture di
    // superficie anche salvando lo sfondo.
    bool isBg = m_mainWindow->editingBackground();
    // Col bersaglio Border si salva la texture del BORDO, col suo messaggio:
    // lo tiene il motore (BorderStyle), quindi si chiede su una copia e la si
    // riscrive dopo la conferma.
    const bool isBorder = m_mainWindow->editingBorder();
    GLWidget *gl = m_mainWindow->ui->glWidget;
    QString borderHint = (isBorder && gl) ? gl->borderTextureHint() : QString();
    float borderHintSecs = (isBorder && gl) ? gl->borderTextureHintSeconds() : 6.0f;
    QString &hintRef  = isBorder ? borderHint
                      : isBg ? m_mainWindow->m_currentBgTextureHintText
                             : m_mainWindow->m_currentTextureHintText;
    float   &hintSecs = isBorder ? borderHintSecs
                      : isBg ? m_mainWindow->m_currentBgTextureHintSeconds
                             : m_mainWindow->m_currentTextureHintSeconds;
    if (!askSceneHint(m_mainWindow, &hintRef, "texture"))
        return;
    if (isBorder && gl) gl->setBorderTextureHint(borderHint, borderHintSecs);

    QJsonObject root;

    QString currentCode;
    bool isImplicit = (m_mainWindow->implicitMode());

    // Il codice BASE (senza tag immagine) in base al modo. Parametrico: lo
    // SCRIPT, come nel Save dei record (vedi surfaceTextureScript); con una
    // fascia selezionata e il dock sulla texture si salva cio' che l'editor
    // mostra, lo script della fascia. Qui prima si travasava l'editor nelle
    // copie APPLICATE (m_scene.surfaceTextureCode / m_scene.bgTextureCode): salvare una
    // texture di fascia la faceva diventare la texture di superficie.
    // Lo SFONDO non dipende dal modo (ha il suo shader): in Ray Marching col
    // bersaglio Background si salvava il campo della texture di superficie.
    if (isBorder) {
        // BORDO: cio' che il dock Script mostra (lo slot del bordo) o, col dock
        // su un altro modulo, il codice nel motore. Sempre parametrico.
        currentCode = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeTexture)
                      ? m_mainWindow->scriptText(m_mainWindow->shownScriptSlot())
                      : (gl ? gl->borderTextureCode() : QString());
        root["isImplicitMode"] = false;
    } else if (isImplicit && !isBg) {
        // Se siamo in Ray Marching salviamo entrambi i campi
        currentCode = m_mainWindow->m_scene.rm.texture;
        root["displacement"] = m_mainWindow->m_scene.rm.displacement;
        root["isImplicitMode"] = true; // Flag fondamentale per il caricamento
    } else {
        if (isBg)
            currentCode = m_mainWindow->backgroundTextureScript();
        else if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeTexture)
            currentCode = m_mainWindow->scriptText(m_mainWindow->shownScriptSlot());
        else
            currentCode = m_mainWindow->surfaceTextureScript();
        root["isImplicitMode"] = false;
    }

    // Tag immagine: va prepeso DOPO aver scelto il codice base, altrimenti (bug
    // storico) i rami isImplicit/parametrico qui sopra ricalcolavano currentCode
    // da zero e BUTTAVANO VIA il //IMG: -> il path immagine non finiva mai nel
    // JSON -> al reload appariva l'ultima immagine caricata, non quella salvata.
    // Prima togliamo eventuali //IMG: gia' presenti nel codice base per evitare
    // duplicati, poi lo rimettiamo pulito in cima.
    // Il bordo: la SUA immagine, dal tag del codice salvato o da quello nel
    // motore (non quella della superficie, che campiona solo in mancanza).
    QString borderImage;
    if (isBorder) {
        borderImage = GLWidget::imagePathInTextureCode(currentCode);
        if (borderImage.isEmpty() && gl)
            borderImage = GLWidget::imagePathInTextureCode(gl->borderTextureCode());
    }
    QRegularExpression imgRe(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption);
    currentCode.remove(imgRe);
    // L'immagine del bersaglio: lo sfondo ha il suo percorso (prima si metteva
    // quella della superficie anche salvando lo sfondo).
    const QString imagePath = isBorder ? borderImage
                            : isBg ? m_mainWindow->backgroundImagePath()
                                   : m_mainWindow->surfaceImagePath();
    if (!imagePath.isEmpty()) {
        currentCode = "//IMG:" + imagePath + "\n" + currentCode.trimmed();
    }

    if (currentCode.trimmed().isEmpty()) currentCode = "// Texture Preset";
    root["code"] = currentCode;

    if (isBorder && gl) {
        // Inquadratura e colori della texture del BORDO.
        const GLWidget::BorderStyle b = gl->borderStyle();
        root["pan_x"] = (double)b.texPan.x();
        root["pan_y"] = (double)b.texPan.y();
        root["zoom"] = (double)b.texZoom;
        root["rotation"] = (double)b.texRotation;
        root["hasCustomColors"] = true;
        root["color1"] = b.texCol1.name();
        root["color2"] = b.texCol2.name();
    } else if (isBg && gl) {
        // Inquadratura e colori dello SFONDO (u_col1/u_col2 della sua copia
        // dell'UBO, come nel blocco "background" dei record). Prima si
        // scrivevano quelli della superficie: riaperto, uno sfondo che usa i
        // colori (Mandelbrot) li cambiava.
        root["pan_x"] = (double)gl->backgroundPan().x();
        root["pan_y"] = (double)gl->backgroundPan().y();
        root["zoom"] = (double)gl->backgroundZoom();
        root["rotation"] = (double)gl->backgroundRotation();
        root["hasCustomColors"] = true;
        root["color1"] = m_mainWindow->m_scene.bgTexColor1.name();
        root["color2"] = m_mainWindow->m_scene.bgTexColor2.name();
    } else if (m_mainWindow->ui->glWidget) {
        QVector2D pan = m_mainWindow->ui->glWidget->getFlatPan();
        root["pan_x"] = (double)pan.x();
        root["pan_y"] = (double)pan.y();
        root["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
        root["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
        root["hasCustomColors"] = true;
        root["color1"] = m_mainWindow->surfaceTexColor(1).name();
        root["color2"] = m_mainWindow->surfaceTexColor(2).name();
    }
    root["type"] = "custom_texture";
    root["name"] = QFileInfo(path).baseName();

    // Suggerimento in sovrimpressione della texture (tipicamente: a cosa serve
    // la costante A..F/S che questo script usa). Non ha UI di editing, quindi si
    // riscrive quello della texture caricata: senza, ogni Save su una texture
    // che ne aveva uno lo perderebbe -- lo stesso difetto gia' corretto per le
    // superfici (~694). Chiave assente se non c'e' nulla da dire, cosi' le
    // texture che non lo usano non cambiano di un byte.
    if (!hintRef.isEmpty()) {
        root["hintText"] = hintRef;
        root["hintSeconds"] = (double)hintSecs;
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(path);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        m_mainWindow->m_currentTexturePresetPath = path;

        // Il lavoro e' su disco: m_lastSaveSucceeded lo dice a
        // confirmDiscardUnsaved.
        //
        // Pulito SOLO il modulo texture: un file texture non contiene le
        // equazioni, quindi dichiarare pulita anche la scena darebbe per
        // salvato un lavoro sulla superficie che non e' stato scritto da
        // nessuna parte -- e il reset successivo lo butterebbe via senza
        // chiedere niente.
        m_mainWindow->markTextureSaved();
        m_mainWindow->m_lastSaveSucceeded = true;

        // Refresh visivo + selezione del file appena salvato nella libreria
        QTimer::singleShot(100, m_mainWindow, [this, path]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeTextures, path);
        });
    }
}

void PresetSerializer::saveMotion(const QString &suggestedPath)
{
    if (refuseEmptyRecord()) return;
    bool wasRotating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);
    bool wasTimeAnimating = false;

    if (m_mainWindow->isAnythingMoving()) {
        wasTimeAnimating = true;
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
        m_mainWindow->ui->glWidget->stopAnimationTimer();
    }

    if (wasRotating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    QSettings settings;
    QString lastDir = settings.value("lastMotionDir", settings.value("lastFolder", QDir::homePath()).toString()).toString();

    QString fileName;
    if (!suggestedPath.isEmpty() && suggestedPath.endsWith(".json", Qt::CaseInsensitive)) {
        fileName = suggestedPath;
    } else {
        QString startPath = suggestedPath;

        const QString recordsRoot = LibraryFolders::root() + "/records";

        if (!isUsableStartDir(startPath)) {
            QTreeWidgetItem *selItem = m_mainWindow->getCurrentLibraryItem();
            if (selItem) {
                if (selItem->data(0, Qt::UserRole + 10).isValid()) {
                    startPath = selItem->data(0, Qt::UserRole + 10).toString(); // È una cartella
                } else {
                    startPath = QFileInfo(selItem->toolTip(0)).absolutePath(); // È un file
                }
            }
            // Selezionando il NODO di categoria (non una foglia) il path e'
            // vuoto/non valido (o risorsa ":/" di un item builtin): il fallback
            // deve puntare a una cartella GARANTITA esistente, altrimenti su iOS
            // il dialog apre una dir inesistente -> lista vuota + salvataggio
            // fallito (vedi saveSurface).
            if (!isUsableStartDir(startPath)) {
                startPath = lastDir;
            }
            if (!isUsableStartDir(startPath)) {
                startPath = recordsRoot;
            }
        }

        // Un record si salva SOLO sotto records/: il tipo e' gia' deciso, non
        // c'e' nessuna scelta di ramo da offrire. Le due sorgenti qui sopra
        // possono pero' puntare altrove -- getCurrentLibraryItem() legge la
        // selezione di QUALUNQUE albero (con una superficie selezionata il
        // dialogo si apriva in surfaces/) e "lastMotionDir" e' una cartella
        // ricordata che puo' non esistere piu' o essere stata spostata. In
        // entrambi i casi il salvataggio sarebbe poi finito nel blocco
        // "Save Blocked" piu' sotto, che rifiuta i percorsi fuori ramo.
        {
            const QString canon = QDir(startPath).absolutePath();
            if (!canon.startsWith(QDir(recordsRoot).absolutePath(), Qt::CaseInsensitive))
                startPath = recordsRoot;
        }

        // Garantiamo che la cartella di partenza esista davvero (prima scrittura
        // su iOS: il container puo' non avere ancora /records).
        {
            QString startDir = startPath.endsWith(".json", Qt::CaseInsensitive)
                                   ? QFileInfo(startPath).absolutePath()
                                   : startPath;
            if (!startDir.isEmpty() && !QDir(startDir).exists())
                QDir().mkpath(startDir);
        }

        if (!startPath.endsWith(".json", Qt::CaseInsensitive)) {
            if (!startPath.endsWith("/")) startPath += "/";
            startPath += "NewMotion.json";
        }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        MobileSaveDialog dialog("Save Record", QFileInfo(startPath).absolutePath(), QFileInfo(startPath).completeBaseName(), m_mainWindow, LibraryFolders::root() + "/records");
        if (dialog.exec() != QDialog::Accepted) {
            if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
            if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
            if (wasTimeAnimating) {
                m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
                m_mainWindow->ui->glWidget->startAnimationTimer();
            }
            return;
        }
        fileName = dialog.getSelectedPath();
#else
        fileName = getSaveFileNameWithSuffix(m_mainWindow, "Save File", startPath, "JSON Files (*.json)", "json");
#endif
    }

    if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // --- BLOCCO VALIDAZIONE RIGIDA ---
    // Il ramo per posizione: col nome, records/Rotations/Parametric/Surfaces
    // passava per il ramo delle superfici e il Save Record veniva rifiutato.
    const LibraryFolders::Branch branch = LibraryFolders::branchOf(QFileInfo(fileName).absolutePath());
    if (branch == LibraryFolders::Branch::Surfaces ||
        branch == LibraryFolders::Branch::Textures ||
        branch == LibraryFolders::Branch::Sounds) {
        QMessageBox::warning(m_mainWindow, "Save Blocked",
                             "Operation not allowed.\n\nRecord presets capture the entire scene (including the surface) and must be saved exclusively in the 'Records' folder.");
        return;
    }

    // HINT della scena: si chiede DOPO che percorso e nome sono decisi e dopo il
    // blocco di validazione, cosi' non si compila un messaggio per un
    // salvataggio che verra' rifiutato o annullato. Cancel qui annulla il
    // salvataggio: e' l'ultima conferma prima della scrittura.
    // Il campo dello sfondo solo se il record HA uno sfondo, o un suo messaggio
    // da poter togliere: altrimenti sarebbe un campo vuoto senza scopo.
    const bool hasBg = !m_mainWindow->m_scene.bgTextureCode.trimmed().isEmpty()
                       || !m_mainWindow->m_currentBgTextureHintText.isEmpty();
    // Il campo del bordo solo se il record ha una texture sul bordo (o un suo
    // messaggio da poter togliere). Il messaggio lo tiene il motore: si chiede
    // su una copia e la si riscrive prima della cattura della scena.
    GLWidget *hintGl = m_mainWindow->ui->glWidget;
    QString borderHint = hintGl ? hintGl->borderTextureHint() : QString();
    const bool hasBorderTex = hintGl && (!hintGl->borderTextureCode().trimmed().isEmpty()
                                         || !borderHint.isEmpty());
    if (!askSceneHint(m_mainWindow, &m_mainWindow->m_currentHintText, "record",
                      &m_mainWindow->m_currentTextureHintText,
                      hasBg ? &m_mainWindow->m_currentBgTextureHintText : nullptr,
                      hasBorderTex ? &borderHint : nullptr))
        return;
    if (hasBorderTex)
        hintGl->setBorderTextureHint(borderHint, hintGl->borderTextureHintSeconds());

    QString saveFolder = QFileInfo(fileName).absolutePath();
    settings.setValue("lastMotionDir", saveFolder);

    // Salvataggio SENZA suono: se il record ha un audio associato ma al momento
    // del Save l'audio e' stoppato (ne' synth ne' player attivi), l'utente puo'
    // salvare il record senza il blocco audio. Rete di sicurezza: chiediamo
    // conferma, cosi' un audio messo in pausa solo per lavorare non viene perso
    // per sbaglio.
    bool includeSound = true;
    if (!m_mainWindow->m_scene.soundScriptText.trimmed().isEmpty() && m_mainWindow->m_audioController
        && !m_mainWindow->m_audioController->isPlaying()) {
        QMessageBox box(m_mainWindow);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle("Save Record");
        box.setText("The sound is stopped.");
        box.setInformativeText("Do you want to save the record without sound?");
        QPushButton *withoutBtn = box.addButton("Save without sound", QMessageBox::AcceptRole);
        QPushButton *withBtn    = box.addButton("Keep the sound", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(withoutBtn);

        // Tasti larghi: "Save without sound" chiede 169px a 15pt contro i 108
        // imposti dal foglio globale, e veniva tagliata alle due estremita'.
        // 140 perche' e' l'etichetta piu' lunga dell'app (soglia misurata 135).
        // Anche su Android: l'app e' sempre in landscape, quindi un box piu'
        // largo non e' un problema.
        UiStyleManager::widenMessageBoxButtons(&box, 140);

        box.exec();

        if (box.clickedButton() == withoutBtn) {
            includeSound = false;   // omette il blocco audio dalla reiniezione
        } else if (box.clickedButton() != withBtn) {
            return;                 // Annulla: animazioni/timer gia' ripristinati sopra
        }
        // "Includi il suono": includeSound resta true (comportamento classico)
    }

    const QJsonObject root = buildMotionJson(QFileInfo(fileName).baseName(),
                                             MotionRunState{wasRotating, wasPath4D, wasPath3D},
                                             includeSound);

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        // Il lavoro e' su disco: come in saveSurface, lo legge
        // confirmDiscardUnsaved per sapere se il "Save" e' riuscito.
        //
        // Pulito TUTTO: un record contiene l'intera scena -- superficie,
        // texture, suono, camera e rotazioni -- quindi salvandolo e' su disco
        // anche il lavoro dei moduli. Dichiarando pulita la sola scena l'avviso
        // sarebbe ricomparso subito dopo per una texture gia' salvata dentro il
        // record.
        m_mainWindow->markSceneClean();
        m_mainWindow->m_lastSaveSucceeded = true;

        QTimer::singleShot(100, m_mainWindow, [this, fileName]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeMotions, fileName);
        });
    }
}

void PresetSerializer::saveScript()
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    auto resumeTimers = [&]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    };

    QString content = m_mainWindow->scriptText(m_mainWindow->shownScriptSlot());
    if (content.trimmed().isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Warning", "Editor is empty.");
        resumeTimers();
        return;
    }

    bool isSurface = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSurface);
    bool isSound   = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound);

    QSettings settings;
    QString settingsKey = isSurface ? "lastFolder" : (isSound ? "lastSoundDir" : "lastCustomTexDir");
    QString currentMem = settings.value(settingsKey).toString();
    QString rootPath = settings.value("libraryRootPath").toString();

    if (currentMem.isEmpty() || currentMem.contains("build", Qt::CaseInsensitive) || !QDir(currentMem).exists()) {
        if (isSurface) currentMem = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
        else if (isSound) currentMem = settings.value("pathSounds", rootPath + "/sounds").toString();
        else currentMem = settings.value("pathTextures", rootPath + "/textures").toString();
    }

    // Il tipo e' gia' deciso, quindi il dialogo deve aprirsi NEL SUO RAMO. La
    // cartella ricordata e' per-tipo e di norma ci sta gia' dentro, ma puo'
    // essere stata spostata (o venire da una versione precedente che
    // condivideva la chiave): fuori ramo il salvataggio verrebbe poi rifiutato
    // dal blocco "Save Blocked" di saveTexture/saveSound.
    //
    // Il ramo NON si ricava appendendo il nome a LibraryFolders::root(): "Change
    // Folder for Textures/Sounds/Surfaces" puo' portare un ramo FUORI dalla
    // radice, e le chiavi pathTextures/pathSounds/pathSurfaces sono la sede
    // autorevole di dov'e' finito. Con il solo percorso costruito a mano, un
    // ramo spostato risultava sempre "fuori ramo" e il dialogo veniva dirottato
    // su una cartella che nella libreria non e' piu' quel ramo.
    {
        const QString rootFallback = LibraryFolders::root();
        const QString typeKey  = isSurface ? "pathSurfaces" : (isSound ? "pathSounds" : "pathTextures");
        const QString typeName = isSurface ? "/surfaces"    : (isSound ? "/sounds"    : "/textures");
        QString typeRoot = settings.value(typeKey, rootFallback + typeName).toString();
        if (typeRoot.isEmpty()) typeRoot = rootFallback + typeName;

        // Appartenenza al ramo: dentro la cartella del ramo, non "il testo
        // comincia per". Senza il separatore ".../textures2" passerebbe per
        // ".../textures".
        const QString memAbs  = QDir(currentMem).absolutePath();
        const QString rootAbs = QDir(typeRoot).absolutePath();
        const bool inBranch = (memAbs.compare(rootAbs, Qt::CaseInsensitive) == 0)
                           || memAbs.startsWith(rootAbs + "/", Qt::CaseInsensitive);
        if (!inBranch) {
            currentMem = typeRoot;
            if (!QDir(currentMem).exists()) QDir().mkpath(currentMem);
        }
    }

    QString fileName;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString title = isSurface ? "Save Shape Script" : (isSound ? "Save Sound Script" : "Save Texture Script");
    // Nome di default: per le texture il nome di quella caricata, altrimenti vuoto (nuovo).
    QString defaultName;
    if (!isSurface && !isSound && m_mainWindow->surfaceHasImage())
        defaultName = QFileInfo(m_mainWindow->surfaceImagePath()).completeBaseName();
    // navFloor = radice del tipo corrente: l'Up e' disponibile quando currentMem e'
    // una sottocartella e si ferma alla radice (surfaces/sounds/textures). Su mobile
    // LibraryFolders::root() e' il container fisso del sistema = radice reale sempre
    // valida (niente workspace custom, che e' desktop-only).
    QString presetsRoot = LibraryFolders::root();
    QString navFloor = isSurface ? presetsRoot + "/surfaces"
                     : (isSound  ? presetsRoot + "/sounds"
                                 : presetsRoot + "/textures");
    MobileSaveDialog dialog(title, currentMem, defaultName, m_mainWindow, navFloor);

    if (dialog.exec() != QDialog::Accepted) {
        resumeTimers();
        return;
    }
    fileName = dialog.getSelectedPath();
#else
    if (isSurface) fileName = getSaveFileNameWithSuffix(m_mainWindow, "Save Shape Script", currentMem, "Shape Script (*.json)", "json");
    else if (isSound) fileName = getSaveFileNameWithSuffix(m_mainWindow, "Save Sound Script", currentMem, "Sound Script (*.json)", "json");
    else fileName = getSaveFileNameWithSuffix(m_mainWindow, "Save Texture Script", currentMem, "Texture Script (*.json)", "json");
#endif

    resumeTimers();

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // NB: la conferma di sovrascrittura la dà GIA' il dialogo di salvataggio:
    // su desktop il prompt "replace?" di getSaveFileNameWithSuffix, su mobile
    // il bottone Save del MobileSaveDialog che diventa "Overwrite?" (vedi
    // MobileSaveDialog::onSaveClicked). Un QMessageBox manuale qui sarebbe un
    // secondo avviso ridondante ("sovrascrivere" dopo "rimpiazzare"), quindi non
    // lo aggiungiamo. Il backup di backupBeforeOverwrite resta come rete di sicurezza.

    QJsonObject root;

    // Aggiungiamo il nome estratto dal percorso
    root["name"] = QFileInfo(fileName).baseName();

    if (isSurface) {
        root["type"] = "surface";
        root["scriptCode"] = content;

        root["isScript"] = true;
        root["isImplicitMode"] = (m_mainWindow->implicitMode());

        root["steps"] = m_mainWindow->m_scene.steps;

        QJsonObject limits;
        writeParametricLimits(limits);
        writeSpaceLimits(limits);

        root["limits"] = limits;

        // Costanti dai CAMPI TESTO, non dagli slider centesimali. Vedi saveSurface.
        const MainWindow::CascadeConstants kc = m_mainWindow->resolveCascadeConstants(false);
        QJsonObject constants;
        constants["A"] = kc.a;
        constants["B"] = kc.b;
        constants["C"] = kc.c;
        constants["D"] = kc.d;
        constants["E"] = kc.e;
        constants["F"] = kc.f;
        constants["S"] = kc.s;
        root["constants"] = constants;

        // Condizioni iniziali del flusso geodetico: servono agli script metrici
        // che non le dichiarano con le direttive U:=/dU:=/...
        QJsonObject geo;
        geo["u0"] = m_mainWindow->m_scene.eq.geoU;
        geo["v0"] = m_mainWindow->m_scene.eq.geoV;
        geo["w0"] = m_mainWindow->m_scene.eq.geoW;
        geo["du"] = m_mainWindow->m_scene.eq.geoDU;
        geo["dv"] = m_mainWindow->m_scene.eq.geoDV;
        geo["dw"] = m_mainWindow->m_scene.eq.geoDW;
        geo["conform"] = m_mainWindow->m_scene.eq.conform;
        root["geodesic"] = geo;

        // Mappa di visualizzazione (embedding) di uno script metrico: se la mappa
        // x/y/z/p è custom (non la carta identità, es. paraboloide di Flamm) va
        // salvata, altrimenti al ricaricamento torna l'identità.
        m_mainWindow->writeMetricDisplayMap(root);

        // Colore: saveScript NON lo scriveva, quindi una superficie salvata in
        // modalità Script tornava sempre verde. Stesso formato di saveSurface, così
        // il parser (colors.surfColor) lo ripristina identico.
        QJsonObject colors;
        colors["surfColor"] = globalSurfaceColor().name();
        colors["alpha"] = globalSurfaceAlpha();
        root["colors"] = colors;
    }
    else if (isSound) {
        root["code"] = content;
        root["type"] = "sound";
    }
    else {
        root["code"] = content;
        root["type"] = "custom_texture";

        if (m_mainWindow->ui->glWidget) {
            QVector2D pan = m_mainWindow->ui->glWidget->getFlatPan();
            root["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
            root["pan_x"] = (double)pan.x();
            root["pan_y"] = (double)pan.y();
            root["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
            root["hasCustomColors"] = true;
            root["color1"] = m_mainWindow->surfaceTexColor(1).name();
            root["color2"] = m_mainWindow->surfaceTexColor(2).name();
        }
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        // Ricreiamo le chiavi di salvataggio per evitare errori di compilazione
        bool isSurf = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSurface);
        bool isSnd  = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound);
        QString safeSettingsKey = isSurf ? "lastFolder" : (isSnd ? "lastSoundDir" : "lastCustomTexDir");

        // Il lavoro e' su disco: senza dirlo, la conferma "vuoi salvare?"
        // credeva che il salvataggio non fosse mai avvenuto, e il popup tornava
        // al caricamento successivo anche senza nuove modifiche. Stesso buco
        // gia' corretto in saveSoundAs; qui mancava per tutti e tre i modi.
        // Pulito SOLO il modulo salvato: un file texture o sound non contiene
        // le equazioni, quindi dichiarare pulita la scena darebbe per salvato
        // un lavoro sulla superficie mai scritto da nessuna parte. Per lo
        // script di SUPERFICIE, invece, il file e' la scena, come in
        // saveSurface.
        if (isSurf)      m_mainWindow->markSurfaceSaved();
        else if (isSnd)  m_mainWindow->markSoundSaved();
        else             m_mainWindow->markTextureSaved();
        m_mainWindow->m_lastSaveSucceeded = true;

        QSettings().setValue(safeSettingsKey, QFileInfo(fileName).absolutePath());

        if (!isSurf && !isSnd) {
            m_mainWindow->m_currentTexturePresetPath = fileName;
        }

        QTreeWidget *targetTree = isSurf ? m_mainWindow->ui->treeSurfaces
                                : isSnd  ? m_mainWindow->ui->treeSounds
                                         : m_mainWindow->ui->treeTextures;
        QTimer::singleShot(100, m_mainWindow, [this, targetTree, fileName]() {
            m_mainWindow->refreshAndSelectPreset(targetTree, fileName);
        });

    } else {
        QMessageBox::critical(m_mainWindow, "Error", "Could not write to file.");
    }
}

void PresetSerializer::saveSound(const QString &filePath)
{
    if (filePath.isEmpty()) return;

    // 1. Se è un file audio multimediale (mp3, wav, ogg), non facciamo nulla.
    // L'app non altera i byte di un file audio originale. (Per copiarlo si usa Save As).
    if (filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
        filePath.endsWith(".wav", Qt::CaseInsensitive) ||
        filePath.endsWith(".ogg", Qt::CaseInsensitive)) {
        return;
    }

    // 2. È uno script JSON. Procediamo al salvataggio silenzioso.
    QString finalPath = filePath;
    if (!finalPath.endsWith(".json", Qt::CaseInsensitive)) {
        finalPath += ".json";
    }

    // Backup di sicurezza prima di sovrascrivere (se implementato)
    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(finalPath);
    }

    // 3. Recuperiamo il contenuto aggiornato dello script
    QString content = m_mainWindow->m_scene.soundScriptText;

    // 4. Creiamo la struttura JSON
    QJsonObject root;
    root["code"] = content;
    root["type"] = "sound";
    root["name"] = QFileInfo(finalPath).baseName();

    // 5. Scrittura fisica su disco (sovrascrittura brutale e silenziosa)
    QDir().mkpath(QFileInfo(finalPath).absolutePath());
    QFile outFile(finalPath);

    if (outFile.exists()) {
        outFile.setPermissions(QFile::WriteOwner);
        outFile.remove();
    }

    if (outFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        outFile.write(doc.toJson());
        outFile.close();

        // Il lavoro e' su disco: lo legge confirmDiscardUnsavedSound per sapere
        // se il "Save" e' riuscito. SOLO il flag del modulo, per la stessa
        // ragione di saveTexture: un file sound non contiene la superficie.
        m_mainWindow->markSoundSaved();
        m_mainWindow->m_lastSaveSucceeded = true;
    }

    // 6. Aggiorniamo la UI per riflettere eventuali modifiche (es. orario di ultima modifica)
    QTimer::singleShot(100, m_mainWindow, [this, finalPath]() {
        m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSounds, finalPath);
    });
}

void PresetSerializer::saveTextureAs(const QString &startDir, const QString &sourceFilePath)
{
    if (refuseEmptyTexture()) return;
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);
    // Nome di default: quello della texture caricata, vuoto se è una texture nuova.
    QString defaultName = sourceFilePath.isEmpty()
                              ? QString()
                              : QFileInfo(sourceFilePath).completeBaseName();

    // startDir puo' arrivare inesistente (tap sul NODO di categoria "Textures":
    // rootPath+"/Textures" con la T maiuscola / cartella mai creata su iOS) ->
    // il dialog aprirebbe una dir vuota e il salvataggio fallirebbe. Ricadiamo su
    // una cartella texture GARANTITA e la creiamo (vedi saveSurface/saveMotion).
    QString effStartDir = startDir;
    if (!isUsableStartDir(effStartDir))
        effStartDir = LibraryFolders::root() + "/textures";
    if (!QDir(effStartDir).exists())
        QDir().mkpath(effStartDir);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // navFloor = radice del tipo (textures): l'Up e' disponibile quando si parte
    // da una SOTTOCARTELLA e si ferma qui, senza salire nel filesystem sandbox.
    MobileSaveDialog dialog("Save Texture As...", effStartDir, defaultName, m_mainWindow,
                            LibraryFolders::root() + "/textures");

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString defaultSelection = effStartDir + "/";
    if (!defaultName.isEmpty()) defaultSelection += defaultName + ".json";
    QString savePath = getSaveFileNameWithSuffix(m_mainWindow, "Save Texture As...", defaultSelection, "JSON Files (*.json)", "json");
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastCustomTexDir", QFileInfo(savePath).absolutePath());

    saveTexture(savePath);
}

void PresetSerializer::saveSurfaceAs(const QString &startDir, const QString &sourceFilePath)
{
    if (refuseEmptyShape()) return;
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    // startDir puo' essere inesistente o una risorsa ":/" (item builtin): il
    // dialog ripiegherebbe sull'ultima cartella visitata. Fallback alla radice
    // del tipo, garantita esistente (stessa guardia di saveTextureAs/saveSoundAs).
    QString effStartDir = startDir;
    if (!isUsableStartDir(effStartDir))
        effStartDir = LibraryFolders::root() + "/surfaces";
    if (!QDir(effStartDir).exists())
        QDir().mkpath(effStartDir);

    QString defaultSelection = effStartDir + "/NewShape.json";
    if (!sourceFilePath.isEmpty()) defaultSelection = effStartDir + "/" + QFileInfo(sourceFilePath).fileName();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    // navFloor = radice del tipo (surfaces): Up disponibile dalle sottocartelle,
    // fermo alla radice.
    MobileSaveDialog dialog("Save Shape As...", effStartDir, baseName, m_mainWindow,
                            LibraryFolders::root() + "/surfaces");

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString savePath = getSaveFileNameWithSuffix(m_mainWindow, "Save Shape As...", defaultSelection, "JSON Files (*.json)", "json");
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    // NIENTE setValue("lastFolder") QUI. saveSurface applica il blocco "Save
    // Blocked" (una superficie statica non puo' finire in records/textures/
    // sounds) e puo' rifiutare questo percorso -- ma la memoria era gia' stata
    // scritta, quindi restava puntata a una cartella di un ALTRO ramo. Da li'
    // il dialogo di saveScript si apriva sull'ultimo percorso invece che nel
    // ramo del tipo che si sta salvando. Ora la memoria la scrive saveSurface,
    // DOPO la validazione: solo un salvataggio accettato la aggiorna.
    saveSurface(savePath);
}

void PresetSerializer::saveSoundAs(const QString &startDir, const QString &sourceFilePath)
{
    // Con un file di partenza si copia quello della Library; senza, il suono in scena.
    if (sourceFilePath.isEmpty() && refuseEmptySound()) return;
    // startDir puo' arrivare inesistente (tap sul NODO di categoria "Sounds":
    // rootPath+"/Sounds" con la S maiuscola / cartella mai creata su iOS) -> il
    // dialog aprirebbe una dir vuota e il salvataggio fallirebbe. Ricadiamo su
    // una cartella sound GARANTITA e la creiamo (vedi saveSurface/saveMotion).
    QString effStartDir = startDir;
    if (!isUsableStartDir(effStartDir))
        effStartDir = LibraryFolders::root() + "/sounds";
    if (!QDir(effStartDir).exists())
        QDir().mkpath(effStartDir);

    QString defaultSelection = effStartDir + "/NewSound.json";
    QString filter = "JSON Files (*.json)";
    bool isMediaFile = false;
    QString expectedExt = "json"; // Inizializza con json

    // 1. Capisce se stiamo clonando un file audio reale (MP3/WAV/OGG)
    if (!sourceFilePath.isEmpty()) {
        defaultSelection = effStartDir + "/" + QFileInfo(sourceFilePath).fileName();
        if (sourceFilePath.endsWith(".mp3", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".wav", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".ogg", Qt::CaseInsensitive)) {

            isMediaFile = true;
            expectedExt = QFileInfo(sourceFilePath).suffix().toLower();
            // Aggiorna il filtro dinamicamente (Es: "MP3 Files (*.mp3)")
            filter = QString("%1 Files (*.%2)").arg(expectedExt.toUpper(), expectedExt);
        }
    }

    // ==============================================================
    // Protezione contro i congelamenti UI
    // ==============================================================
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    // ==============================================================
    // Apertura finestra di salvataggio
    // ==============================================================
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    // navFloor = radice del tipo (sounds): Up dalle sottocartelle, fermo alla radice.
    MobileSaveDialog dialog("Save Sound As...", effStartDir, baseName, m_mainWindow,
                            LibraryFolders::root() + "/sounds");

    if (dialog.exec() != QDialog::Accepted) {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
        return;
    }
    QString savePath = dialog.getSelectedPath();

    // FIX MOBILE (Android & iOS): Se è un file media, assicuriamoci che getSelectedPath non abbia
    // forzato ".json" per errore (la nostra classe aggiunge sempre .json di default).
    if (isMediaFile && savePath.endsWith(".json", Qt::CaseInsensitive)) {
        savePath.chop(5); // Rimuove ".json"
    }

#else
    // FIX DESKTOP: Usa la variabile "filter" calcolata sopra (e il suffisso
    // atteso: json o l'estensione del media clonato).
    QString savePath = getSaveFileNameWithSuffix(m_mainWindow, "Save Sound As...", defaultSelection, filter, expectedExt);
#endif

    // Riprendiamo i timer dopo la chiusura della finestra
    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    // ==============================================================

    if (savePath.isEmpty()) return;

    // 2. Forza l'estensione corretta (che sia json o mp3/wav)
    if (!savePath.endsWith("." + expectedExt, Qt::CaseInsensitive)) {
        savePath += "." + expectedExt;
    }

    QSettings settings;
    settings.setValue("lastSoundDir", QFileInfo(savePath).absolutePath());

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(savePath);
    }

    // --- SALVATAGGIO FISICO ---
    if (isMediaFile) {
        // Copia fisica del file multimediale
        if (savePath != sourceFilePath) {
            if (QFile::exists(savePath)) QFile::remove(savePath);
            if (QFile::copy(sourceFilePath, savePath)) {
                QFile::setPermissions(savePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                m_mainWindow->markSoundSaved();   // su disco: vedi il ramo "da zero"
                m_mainWindow->m_lastSaveSucceeded = true;
            }
        }
    } else if (!sourceFilePath.isEmpty()) {
        // Clonazione file JSON esistente
        QFile inFile(sourceFilePath);
        if (inFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
            inFile.close();
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                root["name"] = QFileInfo(savePath).baseName();
                QDir().mkpath(QFileInfo(savePath).absolutePath());

                QFile outFile(savePath);
                if (outFile.exists()) { outFile.setPermissions(QFile::WriteOwner); outFile.remove(); }
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(QJsonDocument(root).toJson());
                    outFile.close();
                    m_mainWindow->markSoundSaved();   // su disco: vedi il ramo "da zero"
                    m_mainWindow->m_lastSaveSucceeded = true;
                }
            }
        }
    } else {
        // Creazione nuovo script audio (JSON) da zero
        QString content = m_mainWindow->m_scene.soundScriptText;

        QJsonObject root;
        root["code"] = content;
        root["type"] = "sound";
        root["name"] = QFileInfo(savePath).baseName();

        QDir().mkpath(QFileInfo(savePath).absolutePath());

        QFile outFile(savePath);
        if (outFile.exists()) { outFile.setPermissions(QFile::WriteOwner); outFile.remove(); }
        if (outFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(root);
            outFile.write(doc.toJson());
            outFile.close();

            // Il lavoro sul suono e' su disco. Senza questo, la conferma
            // "vuoi salvare?" -- che ne legge l'esito -- credeva
            // che il salvataggio fosse FALLITO: il popup si ripresentava al
            // caricamento successivo anche senza nuove modifiche, e l'azione
            // in corso veniva sospesa. saveSound (l'altro writer del tipo) lo
            // azzerava gia'; qui mancava in tutti e tre i rami.
            m_mainWindow->markSoundSaved();
            m_mainWindow->m_lastSaveSucceeded = true;
        }
    }

    QTimer::singleShot(100, m_mainWindow, [this, savePath]() {
        m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSounds, savePath);
    });
}

void PresetSerializer::saveMotionAs(const QString &startDir, const QString &sourceFilePath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    // startDir puo' essere inesistente o una risorsa ":/" (item builtin): il
    // dialog ripiegherebbe sull'ultima cartella visitata. Fallback alla radice
    // del tipo, garantita esistente (stessa guardia di saveTextureAs/saveSoundAs).
    QString effStartDir = startDir;
    if (!isUsableStartDir(effStartDir))
        effStartDir = LibraryFolders::root() + "/records";
    if (!QDir(effStartDir).exists())
        QDir().mkpath(effStartDir);

    QString defaultSelection = effStartDir + "/NewMotion.json";
    if (!sourceFilePath.isEmpty()) defaultSelection = effStartDir + "/" + QFileInfo(sourceFilePath).fileName();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    // navFloor = radice del tipo (records): Up dalle sottocartelle, fermo alla radice.
    MobileSaveDialog dialog("Save Record As...", effStartDir, baseName, m_mainWindow,
                            LibraryFolders::root() + "/records");

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString savePath = getSaveFileNameWithSuffix(m_mainWindow, "Save Record As...", defaultSelection, "JSON Files (*.json)", "json");
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastMotionDir", QFileInfo(savePath).absolutePath());

    saveMotion(savePath);
}

// ==========================================================
// SALVATAGGIO DALL'AVVISO "VUOI SALVARE?"
// ==========================================================

// Salvataggio del lavoro non salvato prima di un'azione distruttiva (apertura di
// un altro preset, ripristino della superficie di default, New, cambio di
// modalita').
//
// Qui NON si sa che cosa l'utente consideri il suo lavoro: puo' essere la
// superficie, ma anche la texture, il suono o l'intera scena con rotazioni,
// camera e path -- che vivono SOLO nel record. Per questo il dialogo non parte
// dal ramo delle superfici con "NewShape" gia' scritto (era un suggerimento
// sbagliato: chi aveva lavorato su texture e rotazioni le perdeva comunque,
// perche' il ramo surfaces non le salva), ma dalla RADICE dei preset e senza
// nome di default: la scelta del ramo -- Surfaces, Textures, Sounds, Records --
// e' la scelta di che cosa salvare, e la fa l'utente.
//
// Il ramo scelto decide poi il formato: si delega al save del tipo giusto, che
// e' anche quello che sa rifiutare le collocazioni sbagliate. Nessuna copia
// della logica di scrittura.
bool PresetSerializer::saveUnsavedWorkInteractive()
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
    bool wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);

    const auto resumeMotion = [&]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    };

    // Radice reale della libreria: la cartella che CONTIENE i quattro rami.
    QString root = LibraryFolders::root();
    if (root.isEmpty()) {
#if defined(Q_OS_ANDROID)
        root = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#else
        root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
               + "/SurfaceExplorer_Presets";
#endif
    }
    if (!QDir(root).exists()) QDir().mkpath(root);

    // I quattro rami devono esistere, altrimenti il dialogo si apre su una
    // radice semivuota e il ramo che serve non e' nemmeno raggiungibile (prima
    // scrittura su iOS: il container puo' non averli ancora).
    for (const QString &branch : { QStringLiteral("surfaces"), QStringLiteral("textures"),
                                   QStringLiteral("sounds"),   QStringLiteral("records") }) {
        if (!QDir(root + "/" + branch).exists()) QDir().mkpath(root + "/" + branch);
    }

    QString savePath;
    // Ciclo: se l'utente si ferma sulla radice (nessun ramo scelto) il file non
    // apparterrebbe a nessun tipo e nessun save lo accetterebbe. Invece di
    // fallire in silenzio si spiega il problema e si riapre il dialogo dov'era.
    for (;;) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // navFloor = la radice: da qui si scende nei rami e non si sale oltre.
        // NON si parte dal ramo gia' deciso (es. records/ quando e' sporca
        // l'intera scena): da un record si puo' voler salvare la SOLA
        // superficie, e col floor sul ramo surfaces/ sarebbe irraggiungibile.
        MobileSaveDialog dialog("Save Work", root, QString(), m_mainWindow, root);
        if (dialog.exec() != QDialog::Accepted) { resumeMotion(); return false; }
        savePath = dialog.getSelectedPath();
#else
        // Cartella di partenza SENZA nome preselezionato: niente "NewShape".
        savePath = getSaveFileNameWithSuffix(m_mainWindow, "Save Work", root + "/",
                                             "JSON Files (*.json)", "json");
#endif
        if (savePath.isEmpty()) { resumeMotion(); return false; }
        if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

        if (LibraryFolders::branchOf(QFileInfo(savePath).absolutePath()) != LibraryFolders::Branch::None)
            break;

        QMessageBox::warning(m_mainWindow, "Choose a branch",
                             "Pick one of the four library branches first:\n\n"
                             "• Shapes (the 'surfaces' folder) — the surface or solid curve alone (no textures, no rotations)\n"
                             "• Textures — the texture preset alone\n"
                             "• Sounds — the sound script alone\n"
                             "• Records — the whole scene: surface, texture, rotations, camera and paths");
    }

    resumeMotion();

    // Il ramo decide il formato. saveSurface/saveMotion riaprirebbero un loro
    // dialogo se ricevessero una cartella: qui il percorso e' gia' un .json,
    // quindi lo prendono cosi' com'e' e scrivono.
    const LibraryFolders::Branch branch = LibraryFolders::branchOf(QFileInfo(savePath).absolutePath());
    if (branch == LibraryFolders::Branch::Records) {
        QSettings().setValue("lastMotionDir", QFileInfo(savePath).absolutePath());
        // Esito: saveMotion/saveSurface lo alzano solo se il file e' finito su
        // disco. Un rifiuto ("Save Blocked") o un errore di scrittura lo
        // lasciano a false, e il chiamante sospende l'azione distruttiva.
        m_mainWindow->m_lastSaveSucceeded = false;
        saveMotion(savePath);
        return m_mainWindow->m_lastSaveSucceeded;
    }
    if (branch == LibraryFolders::Branch::Textures) {
        QSettings().setValue("lastCustomTexDir", QFileInfo(savePath).absolutePath());
        m_mainWindow->m_lastSaveSucceeded = false;
        saveTexture(savePath);
        return m_mainWindow->m_lastSaveSucceeded;
    }
    if (branch == LibraryFolders::Branch::Sounds) {
        m_mainWindow->m_lastSaveSucceeded = false;
        saveSound(savePath);
        return m_mainWindow->m_lastSaveSucceeded;
    }
    m_mainWindow->m_lastSaveSucceeded = false;
    saveSurface(savePath);
    return m_mainWindow->m_lastSaveSucceeded;
}

// ==========================================================
// LIMITI PARAMETRICI U/V/W
// ==========================================================

void PresetSerializer::writeParametricLimits(QJsonObject &limits)
{
    // Stessa regola del Save di superfici e record (numero sempre, "...Expr"
    // solo se formula): una cattura e una scrittura sole, in comune.
    LibraryItem d;
    captureParametricLimits(d);
    LibraryManager::writeParametricLimits(d, limits);
}

void PresetSerializer::writeSpaceLimits(QJsonObject &limits)
{
    LibraryItem d;
    captureSpaceLimits(d);
    LibraryManager::writeSpaceLimits(d, limits);
}
