// mainwindow_p.h - Intestazione PRIVATA dei file di MainWindow
// (mainwindow*.cpp, divisi per argomento): include comuni, aiutanti
// condivisi e sonde di debug. Non includerla da altri moduli.
#pragma once

#include "mainwindow.h"
#include "texthelpers.h"

#include <optional>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include "ui_mainwindow.h"


#include "surfaceengine.h"
#include "expressionparser.h"
#include "uistylemanager.h"
#include "glsltranslator.h"
#include "videorecorder.h"
#include "statusbarscroller.h"
#include "librarymenucontroller.h"
#include "presetserializer.h"
#include "libraryfileoperations.h"
#include "librarydragdrophandler.h"
#include "audiocontroller.h"
#include "securitybookmark.h"
#include "inputvalidator.h"
#include "expressionparser.h"

#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QCheckBox>
#include <QTabBar>
#include <QTimer>
#include <QScopeGuard>
#include <QAction>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileDialog>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTreeWidgetItem>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QSlider>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QMargins>
#include <QScroller>
#include <QScrollBar>
#include <QSettings>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QDirIterator>
#include <QTextDocumentFragment>
#include <QListWidget>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QPainter>
#include <QTextBrowser>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPointer>
#include "ioseditmenu.h"
#include <QGesture>
#include <QGestureEvent>
#include <QMouseEvent>
#include <QTextStream>
#include <QVector>
#include <QVector4D>
#include <QInputMethod>
#include <QGuiApplication>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <cmath>
#include <algorithm>
#include <functional>

// Versione marketing iniettata dal build (project(... VERSION ...) nel
// CMakeLists -> target_compile_definitions). La guardia serve alle build che
// non passano la define, es. il vecchio SurfaceExplorer.pro (qmake): senza,
// il dialogo About non compilerebbe.
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QCoreApplication>
#endif

// SONDE DEL COMMIT GEODETICO (spente). Mettere a 1 per stampare chi committa un
// campo, da dove arrivano le equazioni e se lo snapshot dell'applicato (m_eqApplied) esiste.
// Servono a riconoscere la regressione classica di questa zona: se ricompare
// "le equazioni si applicano senza il Run" oppure "il campo confermato con
// l'Invio non ha effetto", la riga da leggere e' snapshot=0 (snapshot mai
// preso -> in passato si ricadeva sui campi UI) o un ABORT.
// Su un commit di servizio (Invio su una costante, slider) ci si aspetta ORA
// freezeMap=1 E freezeFlow=1: sia la mappa X/Y/Z/P sia i 7 campi del flusso
// sono congelati sull'ultimo Run. Un freezeFlow=0 con useAppliedEqs=1 e' la
// regressione da cui nasceva il disallineamento fra geodetiche e parametriche
// (la costante entrava senza premere Run).
// NB: il progetto non usa qDebug altrove, per questo sono dietro una macro.
#define SE_GEO_COMMIT_PROBE 0
#if SE_GEO_COMMIT_PROBE
#  define SE_GEO_PROBE(...) qDebug("GEO_PROBE " __VA_ARGS__)
#else
#  define SE_GEO_PROBE(...) do {} while (0)
#endif

// TRACCIA DELLA TEXTURE. Stessa forma della sonda geodetica qui sopra, stessa
// ragione (il progetto non usa qDebug altrove) e stesso uso: si accende a mano
// mettendo 1, si rimette a 0 appena il caso e' chiuso.
//
// Serve al caso in cui EDITOR e SCHERMO dicono cose diverse: li' non c'e' alcun
// errore da leggere, e l'unico modo di capire quale dei tre canali (m_textureCode
// per il Ray Marching, m_customFragmentCode per il parametrico globale,
// MeshPart::textureCode per le fasce) sia rimasto indietro e' stamparli TUTTI
// nello stesso istante, insieme a F -- che nei Wireframe moltiplica la densita'
// e da solo spiega rese diverse a parita' di codice.
// Da qui in avanti va stampata la STESSA riga in ogni punto della sequenza: il
// guasto si vede nel DIVERGERE di due campi, non nel valore di uno solo.
//
// A 1 stampa, a 0 e' inerte (le chiamate restano, non costano nulla). Ha gia'
// pagato una volta: e' cosi' che si e' visto che i tre canali texture erano
// SEMPRE allineati e che a divergere era F -- azzerata a 1 al primo
// caricamento di un record perche' la texture che la usa non era ancora nei
// campi quando le costanti venivano giudicate (vedi "costanti-rigiudicate" in
// applyMotionExample).
#define SE_TEX_PROBE 0
#if SE_TEX_PROBE
#  define SE_TEXP(tag) dumpTextureState(tag)
#else
#  define SE_TEXP(tag) do {} while (0)
#endif


// Sceglie il FOV da applicare al caricamento di un preset/record.
// Regola: 45 e' il DEFAULT, ma un file che ha salvato un valore diverso lo
// mantiene. Le tre chiavi provengono dal JSON:
//   cameraFov -> chiave storica "unico FOV, applicato sempre" (default 45)
//   fov3D/fov4D -> i due FOV per-path delle build intermedie
// Un file delle build intermedie puo' avere cameraFov = 45 (il FOV vivo al
// momento del salvataggio, con i path fermi) ma fov3D/fov4D larghi: e' il caso
// dei preset Clifford (103 gradi). Prendere solo cameraFov perderebbe la loro
// inquadratura, percio' se cameraFov e' rimasto al default e uno dei due
// per-path e' diverso, vince quest'ultimo.
inline float resolveSavedFov(float cameraFov, float fov3D, float fov4D)
{
    const float kDefault = 45.0f;
    const float eps = 0.01f;

    if (std::fabs(cameraFov - kDefault) > eps)
        return cameraFov;                       // salvato esplicitamente: vince

    if (std::fabs(fov4D - kDefault) > eps)
        return fov4D;                           // build intermedia, path 4D
    if (std::fabs(fov3D - kDefault) > eps)
        return fov3D;                           // build intermedia, path 3D

    return kDefault;
}

// ==========================================
// Controllo robustezza valori prima dell'invio in GPU
// ==========================================
// SPESSORE DEL BORDO. Il motore lavora sul RAGGIO dei tubi (unita' di scena,
// fino a kBorderThicknessMax); slider ed etichetta su una scala piu' leggibile,
// raggio = valore * kBorderThicknessUnit, quindi 0.25..10. Slider 0..100:
//  - 0 = nessun bordo;
//  - 1..100 = da kBorderThicknessDisplayMin a 10 con curva QUADRATICA, fine
//    vicino al minimo dove stanno i bordi sottili (50 -> 2.64).
// Il minimo non e' zero: il primo scatto deve dare un bordo visibile. Il clic
// su Border col bordo spento lo accende a kBorderThicknessDisplayDefault.
// Le conversioni stanno qui insieme: lo slider (clic) e setBorderUI (load,
// reset) devono concordare.
// STEPS. Lo slider va da 10 a kStepsSliderMax, come sempre; dal campo si scrive
// anche di piu' e il fondo corsa si allarga fino al valore scritto, e resta li'
// mentre si trascina lo slider; torna a kStepsSliderMax quando dal campo si
// scrive un valore piu' basso (come gli slider delle costanti). Fino a un tetto (MainWindow::stepsLimit): in
// parametrico i punti della mesh, tutte le parti insieme -- ogni punto costa 40
// byte di vertice e circa 24 di indici, sulla CPU e sulla GPU; in Ray Marching
// GLWidget::kRayStepsMax, il tetto del ciclo nello shader.
inline constexpr int kStepsSliderMax = 1000;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
inline constexpr double kStepsPointBudget = 2250000.0;   // una superficie fino a 1500
#else
inline constexpr double kStepsPointBudget = 4000000.0;   // una superficie fino a 2000
#endif

inline constexpr float kBorderThicknessMax = 0.2f;
inline constexpr float kBorderThicknessUnit = 0.02f;          // raggio per unita' mostrata
inline constexpr float kBorderThicknessDisplayMin = 0.25f;
inline constexpr float kBorderThicknessDisplayDefault = 0.5f; // all'accensione dal radio
inline constexpr float kBorderThicknessDisplayMax = kBorderThicknessMax / kBorderThicknessUnit;   // 10
inline float borderThicknessFromSlider(int val)
{
    if (val <= 0) return 0.0f;
    const float f = (qBound(1, val, 100) - 1) / 99.0f;
    const float shown = kBorderThicknessDisplayMin
                        + (kBorderThicknessDisplayMax - kBorderThicknessDisplayMin) * f * f;
    return shown * kBorderThicknessUnit;
}
inline int sliderFromBorderThickness(float t)
{
    if (t <= 0.0f) return 0;
    const float shown = qBound(kBorderThicknessDisplayMin, t / kBorderThicknessUnit,
                               kBorderThicknessDisplayMax);
    const float f = std::sqrt((shown - kBorderThicknessDisplayMin)
                              / (kBorderThicknessDisplayMax - kBorderThicknessDisplayMin));
    return 1 + qRound(f * 99.0f);
}
// Clic sulla corsa e frecce: passo FISSO sul valore mostrato (0.1 e 0.01), come
// gli slider delle costanti. Il passo di Qt in posizioni, sulla curva, valeva
// pochi centesimi vicino al minimo e quasi 2 in fondo. Scendendo ci si ferma al
// minimo e il passo dopo spegne il bordo; da spento il primo passo lo accende al
// minimo.
inline float borderThicknessStepped(float t, float shownStep)
{
    if (t <= 0.0f) return shownStep > 0.0f ? kBorderThicknessDisplayMin * kBorderThicknessUnit : 0.0f;
    const float cur = t / kBorderThicknessUnit;
    float shown = qRound((cur + shownStep) * 100.0f) / 100.0f;
    if (shown < kBorderThicknessDisplayMin)
        shown = (cur > kBorderThicknessDisplayMin + 0.001f) ? kBorderThicknessDisplayMin : 0.0f;
    return qMin(shown, kBorderThicknessDisplayMax) * kBorderThicknessUnit;
}
// Il valore mostrato nell'etichetta (scala 0.5..10), dal raggio.
inline QString borderThicknessText(float t)
{
    return QString::number(t / kBorderThicknessUnit, 'f', 2);
}

// Il bordo di un preset nella forma del motore.
inline GLWidget::BorderStyle borderStyleOf(const LibraryItem &d)
{
    GLWidget::BorderStyle s;
    s.radius = d.borderThickness;
    s.color = d.borderColor;
    s.mode = d.borderMode;
    s.wireAlong = d.borderWireAlong;
    s.wireAround = d.borderWireAround;
    s.texCode = d.borderTexCode;
    s.texEnabled = d.borderTexEnabled;
    s.texCol1 = d.borderTexCol1;
    s.texCol2 = d.borderTexCol2;
    s.texZoom = d.borderTexZoom;
    s.texPan = QVector2D(d.borderTexPanX, d.borderTexPanY);
    s.texRotation = d.borderTexRotation;
    s.texLibName = d.borderTexLibName;
    s.texHint = d.borderTexHintText;
    s.texHintSeconds = d.borderTexHintSeconds;
    return s;
}

inline constexpr double kMaxRenderableMagnitude = 1.0e5;
inline constexpr double kSpikeRatio = 50.0;
inline constexpr double kAliasEdgeFraction = 0.15;

inline bool isMeshSafeValue(double val) {
    return std::isfinite(val) && std::abs(val) <= kMaxRenderableMagnitude;
}


// Costanti A-F citate da un blocco di codice. Stesso criterio di
// updateConstantsUIState: match CASE-SENSITIVE (le iniettate sono maiuscole) e
// una lettera DICHIARATA come variabile locale non conta -- "float S = 512.0" e'
// la locale dello script, non lo slider. Unica sede: la usano il controllo delle
// costanti contese al caricamento di una texture e l'avviso al caricamento di un
// record.
inline QSet<QString> constantsUsedIn(const QString &raw)
{
    const QString code = stripCodeComments(raw);
    QSet<QString> out;
    for (const QChar c : QStringLiteral("ABCDEF")) {
        const QString L(c);
        const QRegularExpression use("(?<![A-Za-z0-9_.])" + L + "(?![A-Za-z0-9_])");
        if (!code.contains(use)) continue;
        const QRegularExpression decl(
            "\\b(?:float|int|uint|bool|vec[234]|mat[234])\\s+"
            "(?:\\w+\\s*(?:=[^,;()]*)?,\\s*)*" + L + "\\b");
        if (!code.contains(decl)) out.insert(L);
    }
    return out;
}

// Regex condivise per l'analisi delle variabili nelle equazioni: vengono usate
// nei percorsi caldi (textChanged di 17 campi, aggiornamenti del master button),
// quindi le compiliamo una volta sola invece che a ogni chiamata.
inline const QRegularExpression kReLowerU("\\bu\\b");
inline const QRegularExpression kReLowerV("\\bv\\b");
inline const QRegularExpression kReLowerW("\\bw\\b");
inline const QRegularExpression kReUpperU("\\bU\\b");
inline const QRegularExpression kReUpperV("\\bV\\b");
inline const QRegularExpression kReUpperW("\\bW\\b");
inline const QRegularExpression kReTimeVar("\\b(t|iTime|u_time)\\b");

// ==========================================
// Filtro per catturare il ridimensionamento OpenGL
// ==========================================
// ==========================================
// Filtro per Desktop: Blocca l'andata a capo nelle equazioni
// ==========================================
// Campi delle equazioni path camera (4D e 3D): l'Invio su questi campi, a
// moto attivo, ricompila il path al volo (commitPathFieldOnEnter). Usato dai
// filtri tastiera desktop e mobile, che consumano il Return.
inline bool isPathEquationField(const QString& objectName)
{
    static const QSet<QString> kPathFields = {
        "lineX_P", "lineY_P", "lineZ_P", "lineP_P",
        "lineAlpha_P", "lineBeta_P", "lineGamma_P",
        "lineX_P3D", "lineY_P3D", "lineZ_P3D", "lineR_P3D"
    };
    return kPathFields.contains(objectName);
}

// Campi del modulo EQUAZIONI (tab Parametric) che definiscono la superficie:
// equazioni principali, composizioni U/V/W, vincoli espliciti e flusso
// geodetico (condizioni iniziali, direzioni, fattore conforme). Su questi
// l'Invio NON applica nulla -- si limita a togliere il focus -- perche' la
// superficie si ricostruisce solo col Run: applicare a meta' digitazione
// mostrerebbe una forma che non corrisponde a cio' che si sta scrivendo (X
// aggiornata, Y e Z ancora vecchie). Restano fuori i parametri che non
// ridefiniscono la superficie -- costanti A..F/S e Steps -- che l'Invio
// applica subito. I limiti u/v/w aspettano anch'essi il Run ma hanno un ramo
// proprio nei filtri (commitLimitFieldOnEnter: validano e restano in attesa).
inline bool isDeferredEquationField(const QString& objectName)
{
    static const QSet<QString> kEquationFields = {
        "lineX", "lineY", "lineZ", "lineP",
        "lineU", "lineV", "lineW",
        "lineExplicitU", "lineExplicitV", "lineExplicitW",
        "lnU", "lnV", "lnW", "lndU", "lndV", "lndW", "lineConform"
    };
    return kEquationFields.contains(objectName);
}

// Campi del sotto-tab TUBES (tab Parametric): la curva asse X/Y/Z/P(u) e lo
// spessore. Hanno un ramo proprio nei filtri tastiera: la curva segue la
// regola dei campi differiti (ferma aspetta il Run, in moto applica al volo),
// lo spessore si applica alla conferma del campo. Senza il ramo cadrebbero nel
// generico, che rifa' il Run anche a superficie ferma.
inline bool isTubeEquationField(const QString& objectName)
{
    static const QSet<QString> kTubeFields = {
        "lineTubeX", "lineTubeY", "lineTubeZ", "lineTubeP", "lineTubeThickness"
    };
    return kTubeFields.contains(objectName);
}


// La fascia ha una texture PROPRIA, accesa e ANIMATA? Punto unico: e' la stessa
// domanda che serve a sapere se il master deve considerarla in moto e a decidere
// quali orologi riaccendere. Scritta due volte, le due copie erano destinate a
// divergere -- e' il modo in cui sono nati piu' bug di questo progetto.
inline bool meshTextureAnimated(const MeshPart &mp,
                                const std::function<bool(const QString&)> &hasTime)
{
    return mp.hasCustomTexture && mp.textureEnabled
           && !mp.textureCode.isEmpty() && hasTime(mp.textureCode);
}


