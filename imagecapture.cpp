#include "imagecapture.h"
#include "glwidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageWriter>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <limits>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStyleHints>
#include <QTimer>
#include <QTouchEvent>
#include <QVector2D>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

// ==========================================================
// LOGICA PURA
// ==========================================================

static int s_testBlankFrames = 0;   // vedi setTestBlankFrames

QSize ImageCapture::sizeFor(double ratio, int longSide)
{
    longSide = std::max(1, longSide);
    ratio = std::max(0.01, ratio);
    if (ratio >= 1.0)
        return QSize(longSide, std::max(1, int(std::lround(longSide / ratio))));
    return QSize(std::max(1, int(std::lround(longSide * ratio))), longSide);
}

float ImageCapture::verticalScaleFor(double ratio, double viewRatio)
{
    if (ratio <= viewRatio || viewRatio <= 0.0) return 1.0f;
    return float(viewRatio / ratio);
}

QRect ImageCapture::guideRect(const QSize &view, double ratio, const QPointF &position)
{
    if (view.isEmpty() || ratio <= 0.0) return QRect();
    const double viewRatio = double(view.width()) / double(view.height());
    int w, h;
    if (ratio <= viewRatio) { h = view.height(); w = int(std::lround(h * ratio)); }
    else                    { w = view.width();  h = int(std::lround(w / ratio)); }
    // Dal centro di sempre, spostato di una frazione dello spazio libero (zero
    // lungo il lato che il riquadro occupa tutto).
    const auto placed = [](int room, double p) {
        const int centred = room / 2;
        return qBound(0, centred + int(std::lround(qBound(-1.0, p, 1.0) * room / 2.0)), std::max(0, room));
    };
    return QRect(placed(view.width() - w, position.x()), placed(view.height() - h, position.y()), w, h);
}

QPointF ImageCapture::captureShiftFor(double ratio, double viewRatio, const QPointF &position)
{
    if (ratio <= 0.0 || viewRatio <= 0.0) return QPointF();
    // Piu' stretta della vista: l'immagine e' alta come la vista e larga ratio /
    // viewRatio di lei. Il riquadro spostato di p * (spazio libero) / 2 porta al
    // centro dell'immagine il punto della vista che stava li': nelle NDC
    // dell'immagine, -p * (viewRatio / ratio - 1).
    if (ratio <= viewRatio) return QPointF(-qBound(-1.0, position.x(), 1.0) * (viewRatio / ratio - 1.0), 0.0);
    // Piu' larga: larga come la vista, alta viewRatio / ratio di lei (col campo
    // verticale ridotto). In basso nella vista = y negativa nelle NDC.
    return QPointF(0.0, qBound(-1.0, position.y(), 1.0) * (ratio / viewRatio - 1.0));
}

bool ImageCapture::isSingleColor(const QImage &im)
{
    if (im.isNull()) return false;
    const QRgb first = im.pixel(0, 0);
    // Pochi punti sparsi bastano quasi sempre, e costano niente.
    for (int k = 1; k <= 64; ++k)
        if (im.pixel(im.width() * k / 65, im.height() * (k * 7 % 64 + 1) / 65) != first) return false;
    // Poi tutti: un wireframe rado su uno sfondo unito puo' sfuggire ai punti sparsi.
    const QImage rgb = (im.depth() == 32) ? im : im.convertToFormat(QImage::Format_RGB32);
    const quint32 value = *reinterpret_cast<const quint32 *>(rgb.constScanLine(0));
    for (int y = 0; y < rgb.height(); ++y) {
        const quint32 *line = reinterpret_cast<const quint32 *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x)
            if (line[x] != value) return false;
    }
    return true;
}

bool ImageCapture::wireFits(int maxWidth, qint64 budget, qint64 segments, int width)
{
    // Senza limiti (desktop), o a un pixel: la linea nativa costa poco a ogni dimensione.
    if (maxWidth <= 0 || width <= 1) return true;
    return width <= maxWidth && 2 * segments * width <= budget;   // due passate, profondita' e colore
}

namespace { bool isPhone(); }

// Limiti del wireframe a strisce su telefono e tablet: una striscia larga su un wireframe
// fitto sovrappone milioni di pixel, con blending, in un solo fotogramma, e il sistema
// ferma la GPU (visto su iPhone a 7984x4491 con "Thick"). Si limitano spessore massimo e
// costo (ImageCapture::wireFits); al dialogo conviene ridurre la DIMENSIONE dell'immagine
// piuttosto che sbiadire le linee (a spessore proporzionale, meno pixel = meno spessore).
// Valori prudenti, non misurati. maxWidth 0 = nessun limite (desktop).
struct WireLimits { int maxWidth; qint64 budget; };
static WireLimits wireLimitsForDevice()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return isPhone() ? WireLimits{ 6, 6000000 } : WireLimits{ 32, 80000000 };
#else
    return WireLimits{ 0, 0 };
#endif
}

// Quanto l'immagine e' piu' grande della vista, nel verso che resta fisso (l'altezza se si
// taglia ai lati, la larghezza se il campo verticale si riduce): e' la scala dello spessore.
static double wireScaleFor(GLWidget *gl, const QSize &size, double ratio)
{
    const double viewRatio = double(gl->width()) / double(std::max(1, gl->height()));
    const double dpr = gl->devicePixelRatioF();
    return (ratio <= viewRatio) ? size.height() / std::max(1.0, gl->height() * dpr)
                                : size.width() / std::max(1.0, gl->width() * dpr);
}

QImage ImageCapture::render(GLWidget *gl, const QSize &size, double ratio, QString *error, double wireFactor,
                            const QPointF &position)
{
    const double viewRatio = double(gl->width()) / double(std::max(1, gl->height()));
    const float verticalScale = verticalScaleFor(ratio, viewRatio);
    const QVector2D shift(captureShiftFor(ratio, viewRatio, position));
    // Le linee del wireframe sono larghe un pixel qualunque sia la dimensione:
    // si ispessiscono in proporzione a quanto l'immagine e' piu' grande della vista
    // (nel verso che resta fisso: l'altezza se si taglia ai lati, la larghezza se
    // il campo verticale si riduce).
    const double scale = wireScaleFor(gl, size, ratio);
    // Ogni ridisegno costa tutto il wireframe: su una scena fitta troppi ridisegni
    // in un solo fotogramma fanno fallire la GPU (immagine tutta magenta). Si limita
    // lo spessore al costo (indici x ridisegni, al massimo 30 milioni) e, se l'immagine
    // esce comunque di un solo colore, si riprova con meno.
    auto drawsFor = [](int w) { return std::max(1, 2 * w - 1); };   // la croce di wireframeOffsets
    // Spessore proporzionale alla vista, moltiplicato per la scelta nel dialogo
    // (sottile / normale / spesso).
    int lineWidth = int(std::lround(scale * wireFactor));
    const qint64 wireIndices = gl->wireframeIndexTotal();
    // Con le strisce di triangoli (parametriche) lo spessore e' libero: un solo
    // disegno, qualunque sia. Le linee native ridisegnate (tubi, mesh custom)
    // restano col tetto di costo.
    if (wireIndices > 0 && !gl->wireframeQuadsEligible()) {
        lineWidth = std::min(lineWidth, 12);
        while (lineWidth > 1 && qint64(drawsFor(lineWidth)) * wireIndices > 30000000) --lineWidth;
    }
    // Rete di sicurezza: il dialogo riduce gia' la dimensione perche' lo spessore chiesto
    // stia nei limiti del dispositivo; qui si impedisce comunque di superarli.
    const WireLimits wl = wireLimitsForDevice();
    if (wl.maxWidth > 0 && wireIndices > 0) {
        lineWidth = std::min(lineWidth, wl.maxWidth);
        while (!wireFits(wl.maxWidth, wl.budget, wireIndices / 2, lineWidth)) --lineWidth;
    }
    QImage img;
    bool singleColor = false;
    for (;;) {
        gl->setCaptureVerticalScale(verticalScale);
        gl->setCaptureShift(shift);
        gl->setCaptureLineWidth(lineWidth);
        gl->beginHiResCapture(size.width(), size.height());
        img = gl->getFrameForVideo(size.width(), size.height(), true);
        // Campo visivo, spostamento e spessore tornano normali PRIMA di chiudere
        // la cattura: endHiResCapture ridisegna subito la vista, e con i valori
        // della cattura restava a schermo un wireframe fitto fino al primo click.
        gl->setCaptureVerticalScale(1.0f);
        gl->setCaptureShift(QVector2D());
        gl->setCaptureLineWidth(1);
        gl->endHiResCapture();
        if (s_testBlankFrames > 0) {
            --s_testBlankFrames;
            img.fill(Qt::magenta);
        }
        singleColor = isSingleColor(img);
        // Ridurre lo spessore aiuta solo il wireframe; a meta' per volta, ogni prova pesa.
        if (!singleColor || lineWidth <= 1 || !gl->hasVisibleWireframe()) break;
        lineWidth /= 2;
    }
    // Ancora di un solo colore: un fotogramma fallito, o una scena davvero uniforme (il
    // solo sfondo). Lo dice un fotogramma piccolo, che la GPU non ha motivo di rifiutare.
    if (singleColor) {
        const QSize probe = sizeFor(ratio, 320);
        gl->setCaptureVerticalScale(verticalScale);
        gl->setCaptureShift(shift);
        gl->beginHiResCapture(probe.width(), probe.height());
        const QImage small = gl->getFrameForVideo(probe.width(), probe.height(), true);
        gl->setCaptureVerticalScale(1.0f);
        gl->setCaptureShift(QVector2D());
        gl->endHiResCapture();
        if (!small.isNull() && !isSingleColor(small)) img = QImage();
    }
    gl->update();
    if (img.isNull() || img.size() != size) {
        if (error) *error = QObject::tr("The GPU could not render an image of %1 x %2 pixels.")
                                .arg(size.width()).arg(size.height());
        return QImage();
    }
    img.setDevicePixelRatio(1.0);
    if (img.format() != QImage::Format_RGB32) img = img.convertToFormat(QImage::Format_RGB32);
    return img;
}

void ImageCapture::setTestBlankFrames(int frames) { s_testBlankFrames = frames; }

double ImageCapture::secondsPerMegapixel(GLWidget *gl)
{
    const QSize probe(1280, 720);
    // Due giri: il primo paga le risorse e la riconfigurazione del buffer.
    double seconds = 0.0;
    for (int i = 0; i < 2; ++i) {
        QElapsedTimer t;
        t.start();
        gl->beginHiResCapture(probe.width(), probe.height());
        const QImage img = gl->getFrameForVideo(probe.width(), probe.height(), true);
        gl->endHiResCapture();
        seconds = double(t.nsecsElapsed()) * 1e-9;
        if (img.isNull()) return 0.0;
    }
    return seconds / (double(probe.width()) * probe.height() / 1e6);
}

// ==========================================================
// IL RIQUADRO GUIDA, figlio della vista (non entra nell'immagine: questa si
// compone fuori schermo)
// ==========================================================
namespace {

class FrameGuide : public QWidget
{
public:
    explicit FrameGuide(QWidget *view) : QWidget(view), m_view(view)
    {
        // Riceve mouse e dita: il riquadro si trascina (la vista sotto, che li
        // userebbe per girare la scena, non li vede).
        setObjectName(QStringLiteral("imageFrameGuide"));
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_AcceptTouchEvents);
        view->installEventFilter(this);
        setGeometry(view->rect());
        publish();
    }
    void setRatio(double ratio)
    {
        m_ratio = ratio;
        updateCursor();
        update();
    }
    // Dove sta il riquadro lungo il lato libero (ImageCapture::guideRect).
    QPointF position() const { return m_position; }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj == m_view && ev->type() == QEvent::Resize) setGeometry(m_view->rect());
        return QWidget::eventFilter(obj, ev);
    }
    bool event(QEvent *ev) override
    {
        // IL DITO (telefono e tablet): lo stesso trascinamento del mouse, col
        // primo punto. Accettato qui, il tocco non arriva alla vista.
        // E IL DOPPIO TOCCO ricentra, come il doppio clic: accettando i tocchi
        // Qt non sintetizza piu' il doppio clic, e mouseDoubleClickEvent sul
        // dito non arrivava mai (il testo del dialogo lo prometteva).
        switch (ev->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto *te = static_cast<QTouchEvent *>(ev);
            if (!te->points().isEmpty()) touch(ev->type(), te->points().first().position());
            ev->accept();
            return true;
        }
        default:
            return QWidget::event(ev);
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) beginDrag(e->position());
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging) dragTo(e->position());
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) endDrag();
        e->accept();
    }
    // Doppio clic: di nuovo al centro.
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        recentre();
        e->accept();
    }
    void paintEvent(QPaintEvent *) override
    {
        if (m_ratio <= 0.0) return;
        const QRect r = ImageCapture::guideRect(size(), m_ratio, m_position);
        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        // Fuori dal riquadro: scurito.
        p.drawRect(QRect(0, 0, width(), r.top()));
        p.drawRect(QRect(0, r.bottom() + 1, width(), height() - r.bottom() - 1));
        p.drawRect(QRect(0, r.top(), r.left(), r.height()));
        p.drawRect(QRect(r.right() + 1, r.top(), width() - r.right() - 1, r.height()));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 220), 2));
        p.drawRect(r.adjusted(1, 1, -1, -1));
    }

private:
    // Lo spazio libero lungo i due lati (zero lungo quello che il riquadro occupa tutto).
    QSize room() const
    {
        const QRect r = ImageCapture::guideRect(size(), m_ratio);
        return r.isValid() ? QSize(width() - r.width(), height() - r.height()) : QSize();
    }
    bool movable() const
    {
        const QSize free = room();
        return free.width() > 0 || free.height() > 0;
    }
    void updateCursor()
    {
        if (!movable()) unsetCursor();
        else setCursor(m_dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    }
    void beginDrag(const QPointF &at)
    {
        if (!movable()) return;
        m_dragging = true;
        m_pressAt = at;
        m_pressPosition = m_position;
        updateCursor();
    }
    // Il riquadro segue il puntatore: lo spostamento in pixel, in frazione di
    // meta' dello spazio libero (la posizione va da -1 a +1).
    void dragTo(const QPointF &at)
    {
        if (!m_dragging) return;
        const QSize free = room();
        QPointF p = m_pressPosition;
        if (free.width() > 0) p.setX(qBound(-1.0, p.x() + 2.0 * (at.x() - m_pressAt.x()) / free.width(), 1.0));
        if (free.height() > 0) p.setY(qBound(-1.0, p.y() + 2.0 * (at.y() - m_pressAt.y()) / free.height(), 1.0));
        if (p == m_position) return;
        m_position = p;
        changed();
    }
    void endDrag()
    {
        m_dragging = false;
        updateCursor();
    }
    void recentre()
    {
        m_position = QPointF();
        changed();
    }
    // Un tocco: trascina, e due tocchi brevi e vicini entro l'intervallo del
    // doppio clic ricentrano. Il secondo tocco di un doppio tocco non
    // trascina; un tocco che si e' mosso o e' durato troppo non conta come
    // primo tocco.
    void touch(QEvent::Type type, const QPointF &at)
    {
        const QStyleHints *hints = QGuiApplication::styleHints();
        const int interval = hints->mouseDoubleClickInterval();
        const qreal reach = std::max(hints->touchDoubleTapDistance(), 1);
        switch (type) {
        case QEvent::TouchBegin:
            if (m_lastTap.isValid() && m_lastTap.elapsed() <= interval
                && QLineF(at, m_lastTapAt).length() <= reach) {
                m_lastTap.invalidate();
                m_secondTap = true;
                recentre();
                return;
            }
            m_secondTap = false;
            m_touchAt = at;
            m_touchTime.start();
            beginDrag(at);
            return;
        case QEvent::TouchUpdate:
            if (!m_secondTap) dragTo(at);
            return;
        case QEvent::TouchEnd:
            endDrag();
            if (!m_secondTap && m_touchTime.isValid() && m_touchTime.elapsed() <= interval
                && QLineF(at, m_touchAt).length() <= reach) {
                m_lastTap.start();
                m_lastTapAt = at;
            } else {
                m_lastTap.invalidate();
            }
            m_secondTap = false;
            return;
        default:   // TouchCancel
            endDrag();
            m_lastTap.invalidate();
            m_secondTap = false;
            return;
        }
    }
    void changed()
    {
        publish();
        update();
    }
    // Per i test: la posizione si legge come proprieta' (la classe e' privata del file).
    void publish() { setProperty("framePosition", m_position); }

    QWidget *m_view;
    double m_ratio = 0.0;
    QPointF m_position;          // vedi ImageCapture::guideRect
    bool m_dragging = false;
    QPointF m_pressAt;
    QPointF m_pressPosition;
    // Il doppio tocco (touch): il primo tocco breve, dove e quando e' finito.
    QElapsedTimer m_lastTap;
    QPointF m_lastTapAt;
    QElapsedTimer m_touchTime;   // il tocco in corso: quando e dove e' cominciato
    QPointF m_touchAt;
    bool m_secondTap = false;    // il tocco in corso e' il secondo di un doppio tocco
};

// Telefono (e non tablet): su iPhone e Android in verticale la finestra offre solo
// rapporto e pixel, senza la misura di stampa.
bool isPhone()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    QScreen *s = QGuiApplication::primaryScreen();
    const QSize sz = s ? s->size() : QSize();
    return std::min(sz.width(), sz.height()) < 600;
#else
    return false;
#endif
}

// Tetto di megapixel per la memoria: su iOS/Android la app viene terminata dal
// sistema (o la GPU resta bloccata, senza modo di interromperla) ben prima del
// limite di texture. Sul desktop vale solo il limite della GPU.
qint64 maxPixels()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return isPhone() ? 36000000 : 100000000;
#else
    return std::numeric_limits<qint64>::max();
#endif
}

struct RatioItem { QString label; double ratio; };

class ImageDialog : public QDialog
{
public:
    ImageDialog(GLWidget *gl, double secPerMp, FrameGuide *guide, QWidget *parent)
        : QDialog(parent), m_gl(gl), m_secPerMp(secPerMp), m_guide(guide)
    {
        setWindowTitle(tr("Capture Image"));
        m_maxTex = gl->maxTextureSize();
        m_viewRatio = double(gl->width()) / double(std::max(1, gl->height()));
        QSettings st;

        auto *form = new QFormLayout;

        // --- RAPPORTO DEI LATI ---
        m_ratioCombo = new QComboBox;
        m_items = {
            { tr("Current view"), m_viewRatio },
            { QStringLiteral("16:9"), 16.0 / 9.0 },
            { QStringLiteral("3:2"), 3.0 / 2.0 },
            { QStringLiteral("4:3"), 4.0 / 3.0 },
            { QStringLiteral("1:1"), 1.0 },
            { tr("2:3 (portrait)"), 2.0 / 3.0 },
            { tr("3:4 (portrait)"), 3.0 / 4.0 },
            { tr("A series (landscape)"), std::sqrt(2.0) },
            { tr("A series (portrait)"), 1.0 / std::sqrt(2.0) },
            { tr("Custom"), 0.0 },
        };
        for (const RatioItem &it : m_items) m_ratioCombo->addItem(it.label);
        m_ratioCombo->setCurrentIndex(qBound(0, st.value("imageRatioIndex", 0).toInt(), m_items.size() - 1));
        form->addRow(tr("Aspect ratio:"), m_ratioCombo);
        m_ratioLabel = form->labelForField(m_ratioCombo);

        auto *customRow = new QWidget;
        auto *customLay = new QHBoxLayout(customRow);
        customLay->setContentsMargins(0, 0, 0, 0);
        m_customW = new QSpinBox; m_customW->setRange(1, 999);
        m_customH = new QSpinBox; m_customH->setRange(1, 999);
        m_customW->setValue(st.value("imageCustomW", 16).toInt());
        m_customH->setValue(st.value("imageCustomH", 9).toInt());
        customLay->addWidget(m_customW);
        customLay->addWidget(new QLabel(QStringLiteral(":")));
        customLay->addWidget(m_customH);
        customLay->addStretch(1);
        m_customRow = customRow;
        form->addRow(QString(), customRow);

        // --- MODO: lato lungo in pixel, misura di stampa (non su telefono), o
        // larghezza e altezza in pixel. Il dato di ogni voce e' il modo (ModeLong,
        // ModePrint, ModeExact), anche in "imageSizeMode": sul telefono manca la
        // stampa, e l'indice non direbbe piu' il modo.
        m_phone = isPhone();
        m_modeCombo = new QComboBox;
        m_modeCombo->addItem(tr("Long side (px)"), ModeLong);
        if (!m_phone) m_modeCombo->addItem(tr("Print size"), ModePrint);
        // Larghezza e altezza scritte una per una: un rapporto fra lati di
        // migliaia di pixel non si scrive coi numeri fino a 999 di Custom (e
        // 5461 x 5734, chiesto dall'utente, non ha fattori comuni).
        m_modeCombo->addItem(tr("Width × height (px)"), ModeExact);
        const int savedMode = m_modeCombo->findData(st.value("imageSizeMode", ModeLong).toInt());
        m_modeCombo->setCurrentIndex(savedMode >= 0 ? savedMode : 0);
        form->addRow(tr("Size by:"), m_modeCombo);

        // --- PIXEL ---
        m_longSpin = new QSpinBox;
        m_longSpin->setRange(64, std::max(64, m_maxTex));
        m_longSpin->setSingleStep(256);
        m_longSpin->setValue(qBound(64, st.value("imageLongSide", 3840).toInt(), std::max(64, m_maxTex)));
        m_presetCombo = new QComboBox;
        m_presetCombo->addItem(tr("Preset…"), 0);
        const std::pair<const char *, int> presets[] = { { "Full HD (1920)", 1920 }, { "2K (2560)", 2560 },
            { "4K (3840)", 3840 }, { "5K (5120)", 5120 }, { "8K (7680)", 7680 }, { "12K (12288)", 12288 },
            { "16K (16384)", 16384 } };
        for (const auto &p : presets)
            if (p.second <= m_maxTex) m_presetCombo->addItem(QString::fromLatin1(p.first), p.second);
        m_pixelRow = new QWidget;
        auto *pixLay = new QHBoxLayout(m_pixelRow);
        pixLay->setContentsMargins(0, 0, 0, 0);
        pixLay->addWidget(m_longSpin);
        pixLay->addWidget(m_presetCombo);
        form->addRow(tr("Long side (px):"), m_pixelRow);
        m_pixelLabel = form->labelForField(m_pixelRow);

        // --- LARGHEZZA E ALTEZZA ---
        const int texMax = std::max(16, m_maxTex);
        m_exactW = new QSpinBox; m_exactW->setRange(16, texMax);
        m_exactH = new QSpinBox; m_exactH->setRange(16, texMax);
        m_exactW->setObjectName(QStringLiteral("imageExactW"));
        m_exactH->setObjectName(QStringLiteral("imageExactH"));
        m_exactW->setValue(qBound(16, st.value("imageExactW", 3840).toInt(), texMax));
        m_exactH->setValue(qBound(16, st.value("imageExactH", 2160).toInt(), texMax));
        m_exactRow = new QWidget;
        auto *exLay = new QHBoxLayout(m_exactRow);
        exLay->setContentsMargins(0, 0, 0, 0);
        exLay->addWidget(m_exactW);
        exLay->addWidget(new QLabel(QStringLiteral("×")));
        exLay->addWidget(m_exactH);
        exLay->addStretch(1);
        form->addRow(tr("Width × height:"), m_exactRow);
        m_exactLabel = form->labelForField(m_exactRow);

        // --- STAMPA ---
        m_printLen = new QDoubleSpinBox;
        m_printLen->setRange(1.0, 2000.0);
        m_printLen->setDecimals(1);
        m_printLen->setValue(st.value("imagePrintLength", 50.0).toDouble());
        m_unitCombo = new QComboBox;
        m_unitCombo->addItems({ QStringLiteral("cm"), QStringLiteral("in") });
        m_unitCombo->setCurrentIndex(qBound(0, st.value("imagePrintUnit", 0).toInt(), 1));
        m_dpiSpin = new QSpinBox;
        m_dpiSpin->setRange(30, 1200);
        m_dpiSpin->setSingleStep(50);
        m_dpiSpin->setValue(st.value("imageDpi", 300).toInt());
        m_printRow = new QWidget;
        auto *prLay = new QHBoxLayout(m_printRow);
        prLay->setContentsMargins(0, 0, 0, 0);
        prLay->addWidget(m_printLen);
        prLay->addWidget(m_unitCombo);
        prLay->addWidget(new QLabel(tr("at")));
        prLay->addWidget(m_dpiSpin);
        prLay->addWidget(new QLabel(QStringLiteral("dpi")));
        form->addRow(tr("Long side:"), m_printRow);
        m_printLabel = form->labelForField(m_printRow);

        // --- SPESSORE DEL WIREFRAME (solo se la scena ne ha uno) ---
        m_wireCombo = new QComboBox;
        m_wireCombo->addItem(tr("Thin"), 0.5);
        m_wireCombo->addItem(tr("Normal"), 1.0);
        m_wireCombo->addItem(tr("Thick"), 2.0);
        // Ogni apertura riparte da Normal: Thin e Thick valgono per l'immagine
        // per cui si sono scelti, e ritrovarli la volta dopo produceva linee
        // sottili o spesse senza accorgersene (chiesto dall'utente).
        m_wireCombo->setCurrentIndex(1);
        m_wireCombo->setToolTip(tr("Width of the wireframe lines relative to the view: Normal keeps the proportion of the screen, Thin halves it, Thick doubles it."));
        form->addRow(tr("Wireframe lines:"), m_wireCombo);
        // Senza wireframe in scena la scelta non fa niente: si spegne, col perche'.
        if (!gl->hasVisibleWireframe()) {
            m_wireCombo->setEnabled(false);
            m_wireCombo->setToolTip(tr("Only for scenes drawn in wireframe."));
        }

        // --- INFORMAZIONI ---
        m_info = new QLabel;
        m_info->setObjectName(QStringLiteral("imageInfo"));
        m_info->setWordWrap(true);
        m_info->setTextFormat(Qt::RichText);
        form->addRow(m_info);

        auto *buttons = new QDialogButtonBox;
        buttons->addButton(tr("Capture…"), QDialogButtonBox::AcceptRole);
        buttons->addButton(QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *lay = new QVBoxLayout(this);
        lay->addLayout(form);
        lay->addWidget(buttons);
        setMinimumWidth(360);

        auto update = [this]() { refresh(); };
        connect(m_ratioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
        connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
        connect(m_wireCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
        connect(m_customW, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        connect(m_customH, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        connect(m_exactW, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        connect(m_exactH, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        // Il menu mostra il preset che corrisponde al valore, "Preset..." (il titolo,
        // non una misura) se nessuno.
        auto syncPreset = [this]() {
            const QSignalBlocker b(m_presetCombo);
            const int at = m_presetCombo->findData(m_longSpin->value());
            m_presetCombo->setCurrentIndex(at > 0 ? at : 0);
        };
        connect(m_longSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, syncPreset]() {
            syncPreset();
            refresh();
        });
        connect(m_presetCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
            const int v = m_presetCombo->itemData(i).toInt();
            if (v > 0) m_longSpin->setValue(v);
        });
        connect(m_printLen, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, update);
        connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
        connect(m_dpiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        syncPreset();   // il valore salvato dall'ultima volta
        refresh();
    }

    double ratio() const { return m_ratio; }
    double wireFactor() const { return m_wireCombo->currentData().toDouble(); }
    // I dpi scelti, se l'immagine e' a misura di stampa (0 = a pixel).
    int dpi() const { return mode() == ModePrint ? m_dpiSpin->value() : 0; }
    QSize size() const { return m_size; }

    void saveSettings() const
    {
        QSettings st;
        st.setValue("imageRatioIndex", m_ratioCombo->currentIndex());
        st.setValue("imageCustomW", m_customW->value());
        st.setValue("imageCustomH", m_customH->value());
        st.setValue("imageSizeMode", mode());
        st.setValue("imageExactW", m_exactW->value());
        st.setValue("imageExactH", m_exactH->value());
        st.remove("imageWireWidth");   // non si ricorda piu': si riparte da Normal
        st.setValue("imageLongSide", m_longSpin->value());
        st.setValue("imagePrintLength", m_printLen->value());
        st.setValue("imagePrintUnit", m_unitCombo->currentIndex());
        st.setValue("imageDpi", m_dpiSpin->value());
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        QDialog::showEvent(e);
        // In coda: alla prima apertura la larghezza vera arriva dopo lo show.
        QTimer::singleShot(0, this, [this]() { fitToText(); });
    }

private:
    enum Mode { ModeLong = 0, ModePrint = 1, ModeExact = 2 };
    int mode() const { return m_modeCombo->currentData().toInt(); }

    // L'ALTEZZA SEGUE IL TESTO. Le informazioni vanno a capo e cambiano a ogni
    // scelta (righe che compaiono: rapporto, avvisi, trascinamento), ma il
    // dialogo restava alto quanto all'apertura e l'ultima riga usciva tagliata
    // ("Drag the frame in the view to choose what goes in the"). Qui l'altezza
    // e' quella che il layout chiede per la larghezza attuale, in su e in giu'.
    void fitToText()
    {
        if (!isVisible() || !layout()) return;
        layout()->activate();
        int need = layout()->totalHeightForWidth(width());
        if (need <= 0) need = sizeHint().height();
        need = std::max(need, minimumSizeHint().height());
        if (need != height()) resize(width(), need);
    }

    void refresh()
    {
        const bool print = mode() == ModePrint;
        const bool exact = mode() == ModeExact;

        // A larghezza e altezza il rapporto viene dai due lati: menu del rapporto
        // nascosto, e il riquadro guida segue i due numeri.
        const int idx = m_ratioCombo->currentIndex();
        const bool custom = (m_items[idx].ratio == 0.0);
        m_ratioCombo->setVisible(!exact);
        if (m_ratioLabel) m_ratioLabel->setVisible(!exact);
        m_customRow->setVisible(custom && !exact);
        if (exact)
            m_ratio = double(m_exactW->value()) / double(m_exactH->value());
        else
            m_ratio = custom ? double(m_customW->value()) / double(m_customH->value()) : m_items[idx].ratio;

        m_pixelRow->setVisible(!print && !exact);
        if (m_pixelLabel) m_pixelLabel->setVisible(!print && !exact);
        m_printRow->setVisible(print);
        if (m_printLabel) m_printLabel->setVisible(print);
        m_exactRow->setVisible(exact);
        if (m_exactLabel) m_exactLabel->setVisible(exact);

        qint64 longSide;
        if (print) {
            const double inches = (m_unitCombo->currentIndex() == 0) ? m_printLen->value() / 2.54
                                                                      : m_printLen->value();
            longSide = qint64(std::llround(inches * m_dpiSpin->value()));
        } else if (exact) {
            longSide = std::max(m_exactW->value(), m_exactH->value());
        } else {
            longSide = m_longSpin->value();
        }
        const bool tooBig = longSide > m_maxTex;
        longSide = std::clamp<qint64>(longSide, 16, m_maxTex);
        // I due lati scritti, esatti: sizeFor dal rapporto potrebbe arrotondare
        // di un pixel. (I campi non superano il limite della GPU.)
        m_size = exact ? QSize(m_exactW->value(), m_exactH->value())
                       : ImageCapture::sizeFor(m_ratio, int(longSide));
        bool memLimited = false;
        while (qint64(m_size.width()) * m_size.height() > maxPixels() && longSide > 16) {
            longSide = longSide * 95 / 100;
            m_size = ImageCapture::sizeFor(m_ratio, int(longSide));
            memLimited = true;
        }
        // Wireframe sul telefono/tablet: si riduce la dimensione, non lo spessore.
        bool wireLimited = false;
        const WireLimits wl = wireLimitsForDevice();
        if (wl.maxWidth > 0 && m_gl->hasVisibleWireframe()) {
            const qint64 segments = m_gl->wireframeIndexTotal() / 2;
            const double factor = m_wireCombo->currentData().toDouble();
            auto fits = [&]() {
                const int w = int(std::lround(wireScaleFor(m_gl, m_size, m_ratio) * factor));
                return ImageCapture::wireFits(wl.maxWidth, wl.budget, segments, w);
            };
            while (!fits() && longSide > 16) {
                longSide = longSide * 95 / 100;
                m_size = ImageCapture::sizeFor(m_ratio, int(longSide));
                wireLimited = true;
            }
        }
        if (m_guide) m_guide->setRatio(m_ratio);

        const double mp = double(m_size.width()) * m_size.height() / 1e6;
        const double seconds = m_secPerMp * mp;
        const double gb = double(m_size.width()) * m_size.height() * 4.0 * 3.0 / 1e9;   // colore, profondita', copia
        QString text = tr("<b>%1 &times; %2 px</b> (%3 megapixel)")
                           .arg(m_size.width()).arg(m_size.height()).arg(mp, 0, 'f', 1);
        if (exact)
            text += tr("<br>Aspect ratio %1 : 1, as shown by the frame in the view.").arg(m_ratio, 0, 'f', 4);
        if (tooBig)
            text += tr("<br><span style='color:#e8a030'>Limited to %1 px, the largest side the GPU can render.</span>").arg(m_maxTex);
        else
            text += tr("<br>GPU limit: %1 px per side.").arg(m_maxTex);
        if (wireLimited)
            text += tr("<br><span style='color:#e8a030'>Reduced to %1 px: the wireframe at this thickness would be too heavy for this device. Thin allows a larger size.</span>")
                        .arg(std::max(m_size.width(), m_size.height()));
        if (memLimited)
            text += tr("<br><span style='color:#e8a030'>Reduced to %1 megapixel, the most this device can render safely.</span>")
                        .arg(maxPixels() / 1000000);
        if (!print) {
            // Cosa sono quei pixel su carta, per chi non pensa in pixel.
            for (int dpi : { 300, 150 }) {
                const double wcm = m_size.width() * 2.54 / dpi, hcm = m_size.height() * 2.54 / dpi;
                text += tr("<br>At %1 dpi: %2 &times; %3 cm").arg(dpi).arg(wcm, 0, 'f', 1).arg(hcm, 0, 'f', 1);
            }
        } else {
            const double wcm = m_size.width() * 2.54 / m_dpiSpin->value(), hcm = m_size.height() * 2.54 / m_dpiSpin->value();
            text += tr("<br>Print size: %1 &times; %2 cm").arg(wcm, 0, 'f', 1).arg(hcm, 0, 'f', 1);
        }
        if (m_secPerMp > 0.0) {
            const QString secText = seconds < 0.1 ? tr("under 0.1 s")
                                  : seconds < 1.0 ? tr("about %1 s").arg(seconds, 0, 'f', 1)
                                                  : tr("about %1 s").arg(std::lround(seconds));
            text += tr("<br>Estimated render time: %1 &middot; memory about %2 GB")
                        .arg(secText).arg(gb, 0, 'f', 1);
            if (seconds > 15.0)
                text += tr("<br><span style='color:#e05050'>Very heavy: the system may stop the GPU before it finishes. Try a smaller size.</span>");
            else if (seconds > 4.0)
                text += tr("<br><span style='color:#e8a030'>Heavy scene: a long GPU job may be stopped by the system.</span>");
        }
        if (qAbs(m_ratio - m_viewRatio) > 0.02 && m_ratio > m_viewRatio)
            text += tr("<br>Wider than the view: the vertical field of view is reduced so the frame shows what the guide shows.");
        if (qAbs(m_ratio - m_viewRatio) > 0.02)
            text += tr("<br>Drag the frame in the view to choose what goes in the image; double-click it to centre it again.");
        m_info->setText(text);
        fitToText();
    }

    GLWidget *m_gl;
    double m_secPerMp;
    FrameGuide *m_guide;
    int m_maxTex = 4096;
    double m_viewRatio = 1.0;
    bool m_phone = false;
    QVector<RatioItem> m_items;
    QComboBox *m_ratioCombo, *m_modeCombo, *m_presetCombo, *m_unitCombo, *m_wireCombo;
    QSpinBox *m_customW, *m_customH, *m_longSpin, *m_dpiSpin, *m_exactW, *m_exactH;
    QDoubleSpinBox *m_printLen;
    QWidget *m_customRow, *m_pixelRow, *m_printRow, *m_exactRow;
    QWidget *m_ratioLabel = nullptr, *m_pixelLabel = nullptr, *m_printLabel = nullptr, *m_exactLabel = nullptr;
    QLabel *m_info;
    double m_ratio = 1.0;
    QSize m_size;
};

}   // namespace

// ==========================================================
// LA SESSIONE DEL DIALOGO
// ==========================================================
namespace {

// LA SESSIONE DEL DIALOGO. Il dialogo non e' modale perche' il riquadro nella
// vista si possa trascinare: un dialogo modale blocca tutta la finestra della
// vista, riquadro compreso. Il resto della finestra principale resta fermo come
// con un dialogo modale, e lo tiene fermo questo filtro dell'applicazione:
//  - mouse, dita, rotella, menu contestuali e trascinamenti vanno solo al riquadro;
//  - tastiera e scorciatoie solo al dialogo (si giudicano al widget che le
//    riceve, o che possiede l'azione: la QWindow le passa al widget col fuoco);
//  - le voci della barra dei menu si spengono (sul Mac la barra sta fuori dalla
//    finestra, e i clic li' non passano da un evento);
//  - la finestra principale non si chiude;
//  - il dialogo resta davanti: cliccare la vista porta avanti la sua finestra.
// Tutto torna com'era quando la sessione finisce.
class CaptureSession : public QObject
{
public:
    CaptureSession(QWidget *mainWindow, QWidget *guide, QWidget *dialog)
        : m_main(mainWindow), m_guide(guide), m_dialog(dialog)
    {
        if (QMenuBar *bar = m_main ? m_main->findChild<QMenuBar *>() : nullptr) disableActions(bar->actions());
        qApp->installEventFilter(this);
    }
    ~CaptureSession() override
    {
        qApp->removeEventFilter(this);
        for (const Disabled &d : m_disabled)
            if (d.action) d.action->setEnabled(d.wasEnabled);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        switch (ev->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::ContextMenu:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::NativeGesture:
        case QEvent::Gesture:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::Drop:
            return inMainWindow(obj) && !inGuide(obj);
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::Shortcut:
            return widgetOf(obj) && !inDialog(obj);
        case QEvent::Close:
            if (obj == m_main) {
                ev->ignore();
                return true;
            }
            break;
        case QEvent::WindowActivate:
            if (obj == m_main && m_dialog) {
                QPointer<QWidget> dialog = m_dialog;
                QTimer::singleShot(0, dialog, [dialog]() { if (dialog) dialog->raise(); });
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    // Il widget che riceve l'evento, o che possiede l'azione o la scorciatoia.
    static QWidget *widgetOf(QObject *obj)
    {
        for (QObject *o = obj; o; o = o->parent())
            if (o->isWidgetType()) return static_cast<QWidget *>(o);
        return nullptr;
    }
    bool inMainWindow(QObject *obj) const
    {
        QWidget *w = widgetOf(obj);
        return m_main && w && w->window() == m_main;
    }
    bool inGuide(QObject *obj) const
    {
        QWidget *w = widgetOf(obj);
        return m_guide && w && (w == m_guide || m_guide->isAncestorOf(w));
    }
    bool inDialog(QObject *obj) const
    {
        QWidget *w = widgetOf(obj);
        return m_dialog && w && w->window() == m_dialog;
    }
    void disableActions(const QList<QAction *> &actions)
    {
        for (QAction *a : actions) {
            if (!a || a->isSeparator()) continue;
            m_disabled.append({ a, a->isEnabled() });
            a->setEnabled(false);
            if (a->menu()) disableActions(a->menu()->actions());
        }
    }

    QPointer<QWidget> m_main, m_guide, m_dialog;
    struct Disabled { QPointer<QAction> action; bool wasEnabled = true; };
    QList<Disabled> m_disabled;
};

}   // namespace

// ==========================================================
// IL FLUSSO
// ==========================================================

static QString s_testSavePath;
static std::function<void(QDialog *)> s_testDialogHandler;
void ImageCapture::setTestSavePath(const QString &path) { s_testSavePath = path; }
void ImageCapture::setTestDialogHandler(std::function<void(QDialog *)> handler) { s_testDialogHandler = std::move(handler); }
static bool s_testCornerDialog = false;
void ImageCapture::setTestCornerDialog(bool on) { s_testCornerDialog = on; }

// IL DIALOGO NELL'ANGOLO, sul telefono: centrato copriva buona parte della
// vista e il riquadro guida non si vedeva (segnalato dall'utente su iPhone).
// In alto a sinistra, dentro l'area sicura (in orizzontale l'isola dinamica
// sta su un lato); il riquadro resta visibile e si trascina nel resto della
// vista. Ancorato all'angolo, il dialogo cresce verso il basso e a destra
// quando cambia il modo (righe che compaiono), senza spostarsi.
static void placeDialogInCorner(QWidget *dlg, QWidget *mainWindow)
{
    if (!dlg || !mainWindow) return;
    constexpr int kMargin = 8;
    QMargins safe;
    if (QWindow *w = mainWindow->windowHandle()) safe = w->safeAreaMargins();
    dlg->move(mainWindow->geometry().topLeft() + QPoint(safe.left() + kMargin, safe.top() + kMargin));
}

bool ImageCapture::run(GLWidget *gl, QWidget *parent, const QString &defaultDir)
{
    if (!gl) return false;
    QSettings settings;

    const double secPerMp = secondsPerMegapixel(gl);

    FrameGuide *guide = new FrameGuide(gl);
    guide->show();
    guide->raise();

    ImageDialog dlg(gl, secPerMp, guide, parent);
    bool accepted = false;
    {
        // NON MODALE, cosi' il riquadro si trascina; CaptureSession tiene fermo
        // il resto della finestra principale finche' il dialogo e' aperto.
        CaptureSession session(gl->window(), guide, &dlg);
        dlg.setWindowModality(Qt::NonModal);
        QEventLoop loop;
        QObject::connect(&dlg, &QDialog::finished, &loop, &QEventLoop::quit);
        if (s_testDialogHandler) QTimer::singleShot(0, &dlg, [&dlg]() { s_testDialogHandler(&dlg); });
        dlg.show();
        if (isPhone() || s_testCornerDialog) placeDialogInCorner(&dlg, gl->window());
        dlg.raise();
        dlg.activateWindow();
        loop.exec();
        accepted = (dlg.result() == QDialog::Accepted);
    }
    // Dove l'utente ha lasciato il riquadro: l'inquadratura dell'immagine.
    const QPointF framePosition = guide->position();
    delete guide;
    if (!accepted) return false;
    dlg.saveSettings();
    const QSize size = dlg.size();
    const double ratio = dlg.ratio();
    const double wireFactor = dlg.wireFactor();
    const int dpi = dlg.dpi();

    // IL FILE. Prima del rendering: un'immagine grande puo' volerci piu' di qualche
    // secondo, e chiedere il nome dopo la butterebbe via a un Annulla.
    QDir().mkpath(defaultDir);
    QString lastDir = settings.value("lastImageDir", defaultDir).toString();
    if (!QDir(lastDir).exists()) lastDir = defaultDir;
    QString path;
    if (!s_testSavePath.isEmpty()) {
        path = s_testSavePath;
    } else
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // Sul telefono nessun selettore di file: si salva nella cartella dei render,
    // con la data nel nome, e lo si dice.
    path = QDir(defaultDir).filePath(QStringLiteral("Image_%1.png")
                                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
#else
    {
        QFileDialog fd(parent, QObject::tr("Save Image"), QDir(lastDir).filePath(QStringLiteral("NewImage.png")));
        fd.setAcceptMode(QFileDialog::AcceptSave);
        fd.setNameFilter(QObject::tr("PNG Image (*.png)"));
        fd.setDefaultSuffix(QStringLiteral("png"));
        if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty()) return false;
        path = fd.selectedFiles().first();
        settings.setValue("lastImageDir", QFileInfo(path).absolutePath());
    }
#endif

    // IL RENDERING, con un avviso: l'interfaccia non risponde finche' la GPU lavora.
    QString error;
    QImage img;
    {
        QProgressDialog busy(QObject::tr("Rendering %1 x %2 px…").arg(size.width()).arg(size.height()),
                             QString(), 0, 0, parent);
        busy.setWindowModality(Qt::WindowModal);
        busy.setMinimumDuration(0);
        busy.show();
        QCoreApplication::processEvents();
        img = render(gl, size, ratio, &error, wireFactor, framePosition);
    }
    if (img.isNull()) {
        QMessageBox::warning(parent, QObject::tr("Capture Image"), error);
        return false;
    }

    // La misura di stampa va nel file: senza, chi lo apre vede la densita' di default.
    if (dpi > 0) {
        const int dotsPerMeter = int(std::lround(dpi / 0.0254));
        img.setDotsPerMeterX(dotsPerMeter);
        img.setDotsPerMeterY(dotsPerMeter);
    }

    // IL SALVATAGGIO, in un altro thread: la codifica PNG di un'immagine da
    // centinaia di megapixel richiede decine di secondi.
    bool saved = false;
    QString writeError;
    {
        QProgressDialog busy(QObject::tr("Saving the image…"), QString(), 0, 0, parent);
        busy.setWindowModality(Qt::WindowModal);
        busy.setMinimumDuration(0);
        busy.show();
        QFutureWatcher<bool> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&]() {
            QImageWriter writer(path, "png");
            const bool ok = writer.write(img);
            if (!ok) writeError = writer.errorString();
            return ok;
        }));
        loop.exec();
        saved = watcher.result();
    }
    if (!saved) {
        QMessageBox::warning(parent, QObject::tr("Capture Image"),
                             QObject::tr("The image could not be saved:\n%1").arg(writeError));
        return false;
    }
    QMessageBox::information(parent, QObject::tr("Capture Image"),
                             QObject::tr("Saved %1 x %2 px.\n\n%3").arg(size.width()).arg(size.height()).arg(path));
    return true;
}
