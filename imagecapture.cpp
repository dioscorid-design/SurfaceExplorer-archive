#include "imagecapture.h"
#include "glwidget.h"

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
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <limits>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

// ==========================================================
// LOGICA PURA
// ==========================================================

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

QRect ImageCapture::guideRect(const QSize &view, double ratio)
{
    if (view.isEmpty() || ratio <= 0.0) return QRect();
    const double viewRatio = double(view.width()) / double(view.height());
    int w, h;
    if (ratio <= viewRatio) { h = view.height(); w = int(std::lround(h * ratio)); }
    else                    { w = view.width();  h = int(std::lround(w / ratio)); }
    return QRect((view.width() - w) / 2, (view.height() - h) / 2, w, h);
}

// Un'immagine di un solo colore: sintomo di un fotogramma fallito sulla GPU.
static bool isSingleColor(const QImage &im)
{
    if (im.isNull()) return false;
    const QRgb first = im.pixel(0, 0);
    for (int k = 1; k <= 64; ++k)
        if (im.pixel(im.width() * k / 65, im.height() * (k * 7 % 64 + 1) / 65) != first) return false;
    return true;
}

QImage ImageCapture::render(GLWidget *gl, const QSize &size, double ratio, QString *error)
{
    const double viewRatio = double(gl->width()) / double(std::max(1, gl->height()));
    const float verticalScale = verticalScaleFor(ratio, viewRatio);
    // Le linee del wireframe sono larghe un pixel qualunque sia la dimensione:
    // si ispessiscono in proporzione a quanto l'immagine e' piu' grande della vista
    // (nel verso che resta fisso: l'altezza se si taglia ai lati, la larghezza se
    // il campo verticale si riduce).
    const double dpr = gl->devicePixelRatioF();
    const double scale = (ratio <= viewRatio) ? size.height() / std::max(1.0, gl->height() * dpr)
                                              : size.width() / std::max(1.0, gl->width() * dpr);
    // Ogni ridisegno costa tutto il wireframe: su una scena fitta troppi ridisegni
    // in un solo fotogramma fanno fallire la GPU (immagine tutta magenta). Si limita
    // lo spessore al costo (indici x ridisegni, al massimo 30 milioni) e, se l'immagine esce comunque
    // di un solo colore, si riprova con meno.
    auto drawsFor = [](int w) { return std::max(1, 2 * w - 1); };   // la croce di wireframeOffsets
    int lineWidth = int(std::lround(scale));
    const qint64 wireIndices = gl->wireframeIndexTotal();
    if (wireIndices > 0)
        while (lineWidth > 1 && qint64(drawsFor(lineWidth)) * wireIndices > 30000000) --lineWidth;
    QImage img;
    for (;;) {
        gl->setCaptureVerticalScale(verticalScale);
        gl->setCaptureLineWidth(lineWidth);
        gl->beginHiResCapture(size.width(), size.height());
        img = gl->getFrameForVideo(size.width(), size.height(), true);
        // Campo visivo e spessore tornano normali PRIMA di chiudere la cattura:
        // endHiResCapture ridisegna subito la vista, e con i valori della cattura
        // restava a schermo un wireframe fitto fino al primo click.
        gl->setCaptureVerticalScale(1.0f);
        gl->setCaptureLineWidth(1);
        gl->endHiResCapture();
        if (lineWidth <= 1 || img.isNull() || !isSingleColor(img)) break;
        lineWidth = std::max(1, lineWidth - 1);
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
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        view->installEventFilter(this);
        setGeometry(view->rect());
    }
    void setRatio(double ratio) { m_ratio = ratio; update(); }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj == m_view && ev->type() == QEvent::Resize) setGeometry(m_view->rect());
        return QWidget::eventFilter(obj, ev);
    }
    void paintEvent(QPaintEvent *) override
    {
        if (m_ratio <= 0.0) return;
        const QRect r = ImageCapture::guideRect(size(), m_ratio);
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
    QWidget *m_view;
    double m_ratio = 0.0;
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

        // --- MODO: pixel o misura di stampa (non su telefono) ---
        m_phone = isPhone();
        m_modeCombo = new QComboBox(this);
        m_modeCombo->addItems({ tr("Pixels"), tr("Print size") });
        m_modeCombo->hide();
        if (!m_phone) {
            m_modeCombo->setCurrentIndex(qBound(0, st.value("imageSizeMode", 0).toInt(), 1));
            form->addRow(tr("Size by:"), m_modeCombo);
            m_modeCombo->setVisible(true);
        }

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

        // --- INFORMAZIONI ---
        m_info = new QLabel;
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
        connect(m_customW, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
        connect(m_customH, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
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
    QSize size() const { return m_size; }

    void saveSettings() const
    {
        QSettings st;
        st.setValue("imageRatioIndex", m_ratioCombo->currentIndex());
        st.setValue("imageCustomW", m_customW->value());
        st.setValue("imageCustomH", m_customH->value());
        st.setValue("imageSizeMode", m_modeCombo->currentIndex());
        st.setValue("imageLongSide", m_longSpin->value());
        st.setValue("imagePrintLength", m_printLen->value());
        st.setValue("imagePrintUnit", m_unitCombo->currentIndex());
        st.setValue("imageDpi", m_dpiSpin->value());
    }

private:
    void refresh()
    {
        const int idx = m_ratioCombo->currentIndex();
        const bool custom = (m_items[idx].ratio == 0.0);
        m_customRow->setVisible(custom);
        m_ratio = custom ? double(m_customW->value()) / double(m_customH->value()) : m_items[idx].ratio;

        const bool print = !m_phone && m_modeCombo->currentIndex() == 1;
        m_pixelRow->setVisible(!print);
        if (m_pixelLabel) m_pixelLabel->setVisible(!print);
        m_printRow->setVisible(print && !m_phone);
        if (m_printLabel) m_printLabel->setVisible(print && !m_phone);

        qint64 longSide;
        if (print) {
            const double inches = (m_unitCombo->currentIndex() == 0) ? m_printLen->value() / 2.54
                                                                      : m_printLen->value();
            longSide = qint64(std::llround(inches * m_dpiSpin->value()));
        } else {
            longSide = m_longSpin->value();
        }
        const bool tooBig = longSide > m_maxTex;
        longSide = std::clamp<qint64>(longSide, 16, m_maxTex);
        m_size = ImageCapture::sizeFor(m_ratio, int(longSide));
        bool memLimited = false;
        while (qint64(m_size.width()) * m_size.height() > maxPixels() && longSide > 16) {
            longSide = longSide * 95 / 100;
            m_size = ImageCapture::sizeFor(m_ratio, int(longSide));
            memLimited = true;
        }
        if (m_guide) m_guide->setRatio(m_ratio);

        const double mp = double(m_size.width()) * m_size.height() / 1e6;
        const double seconds = m_secPerMp * mp;
        const double gb = double(m_size.width()) * m_size.height() * 4.0 * 3.0 / 1e9;   // colore, profondita', copia
        QString text = tr("<b>%1 &times; %2 px</b> (%3 megapixel)")
                           .arg(m_size.width()).arg(m_size.height()).arg(mp, 0, 'f', 1);
        if (tooBig)
            text += tr("<br><span style='color:#e8a030'>Limited to %1 px, the largest side the GPU can render.</span>").arg(m_maxTex);
        else
            text += tr("<br>GPU limit: %1 px per side.").arg(m_maxTex);
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
        m_info->setText(text);
    }

    GLWidget *m_gl;
    double m_secPerMp;
    FrameGuide *m_guide;
    int m_maxTex = 4096;
    double m_viewRatio = 1.0;
    bool m_phone = false;
    QVector<RatioItem> m_items;
    QComboBox *m_ratioCombo, *m_modeCombo, *m_presetCombo, *m_unitCombo;
    QSpinBox *m_customW, *m_customH, *m_longSpin, *m_dpiSpin;
    QDoubleSpinBox *m_printLen;
    QWidget *m_customRow, *m_pixelRow, *m_printRow;
    QWidget *m_pixelLabel = nullptr, *m_printLabel = nullptr;
    QLabel *m_info;
    double m_ratio = 1.0;
    QSize m_size;
};

}   // namespace

// ==========================================================
// IL FLUSSO
// ==========================================================

static QString s_testSavePath;
static std::function<void(QDialog *)> s_testDialogHandler;
void ImageCapture::setTestSavePath(const QString &path) { s_testSavePath = path; }
void ImageCapture::setTestDialogHandler(std::function<void(QDialog *)> handler) { s_testDialogHandler = std::move(handler); }

bool ImageCapture::run(GLWidget *gl, QWidget *parent, const QString &defaultDir)
{
    if (!gl) return false;
    QSettings settings;

    const double secPerMp = secondsPerMegapixel(gl);

    FrameGuide *guide = new FrameGuide(gl);
    guide->show();
    guide->raise();

    ImageDialog dlg(gl, secPerMp, guide, parent);
    if (s_testDialogHandler) QTimer::singleShot(0, &dlg, [&dlg]() { s_testDialogHandler(&dlg); });
    const bool accepted = (dlg.exec() == QDialog::Accepted);
    delete guide;
    if (!accepted) return false;
    dlg.saveSettings();
    const QSize size = dlg.size();
    const double ratio = dlg.ratio();

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
        img = render(gl, size, ratio, &error);
    }
    if (img.isNull()) {
        QMessageBox::warning(parent, QObject::tr("Capture Image"), error);
        return false;
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
