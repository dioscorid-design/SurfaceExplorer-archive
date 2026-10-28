#include "imagetest.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "librarymanager.h"
#include "audiocontroller.h"
#include "camerapaths.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cstdlib>

namespace {

// Differenza fra due foto: pixel con almeno un canale diverso e scarto massimo.
struct ImageDiff {
    qint64 pixels = 0;
    int maxDelta = 0;
    bool sizeMismatch = false;
};

ImageDiff compareImages(const QImage &a0, const QImage &b0)
{
    ImageDiff d;
    if (a0.size() != b0.size()) { d.sizeMismatch = true; return d; }
    const QImage a = a0.convertToFormat(QImage::Format_RGBA8888);
    const QImage b = b0.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < a.height(); ++y) {
        const uchar *pa = a.constScanLine(y);
        const uchar *pb = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; x += 4) {
            int m = 0;
            for (int c = 0; c < 4; ++c) m = std::max(m, std::abs(int(pa[x + c]) - int(pb[x + c])));
            if (m > 0) { ++d.pixels; d.maxDelta = std::max(d.maxDelta, m); }
        }
    }
    return d;
}

// L'immagine attenuata con i pixel diversi in rosso pieno.
QImage diffImage(const QImage &a0, const QImage &b0)
{
    const QImage a = a0.convertToFormat(QImage::Format_RGBA8888);
    const QImage b = b0.convertToFormat(QImage::Format_RGBA8888);
    QImage out(a.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < a.height(); ++y) {
        const uchar *pa = a.constScanLine(y);
        const uchar *pb = b.constScanLine(y);
        uchar *po = out.scanLine(y);
        for (int x = 0; x < a.width() * 4; x += 4) {
            bool differs = false;
            for (int c = 0; c < 4; ++c) differs |= pa[x + c] != pb[x + c];
            po[x + 0] = differs ? 255 : pa[x + 0] / 4;
            po[x + 1] = differs ? 0   : pa[x + 1] / 4;
            po[x + 2] = differs ? 0   : pa[x + 2] / 4;
            po[x + 3] = 255;
        }
    }
    return out;
}

QString pngName(const QString &rel)
{
    QString n = rel;
    if (n.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) n.chop(5);
    return n + QStringLiteral(".png");
}

} // namespace

bool ImageTest::requested(const QStringList &args)
{
    return args.contains(QStringLiteral("--image-test"));
}

void ImageTest::start(MainWindow *mw, const QStringList &args)
{
    auto *t = new ImageTest(mw, args);
    if (!t->m_valid) {
        qCritical("uso: SurfaceExplorer --image-test <radice preset> <cartella uscita> "
                  "[--compare <cartella>] [--filter <testo>] [--settle <ms>] [--size <L>x<A>]");
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(2); });
        return;
    }
    // A finestra aperta e avvio concluso, come gli altri test.
    QTimer::singleShot(2000, t, &ImageTest::run);
}

ImageTest::ImageTest(MainWindow *mw, const QStringList &args)
    : QObject(mw), m_mw(mw)
{
    const int i = args.indexOf(QStringLiteral("--image-test"));
    m_root = args.value(i + 1);
    m_outDir = args.value(i + 2);
    auto option = [&args](const char *name) {
        const int k = args.indexOf(QLatin1String(name));
        return k >= 0 ? args.value(k + 1) : QString();
    };
    m_compareDir = option("--compare");
    m_filter = option("--filter");
    if (!option("--settle").isEmpty()) m_settleMs = option("--settle").toInt();
    const QStringList size = option("--size").split(QLatin1Char('x'));
    if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        m_width = size[0].toInt();
        m_height = size[1].toInt();
    }
    m_valid = !m_root.isEmpty() && !m_outDir.isEmpty() && QDir(m_root).exists()
              && (m_compareDir.isEmpty() || QDir(m_compareDir).exists());
    if (!m_valid) return;
    m_root = QDir(m_root).absolutePath();
    m_outDir = QDir(m_outDir).absolutePath();
    if (!m_compareDir.isEmpty()) m_compareDir = QDir(m_compareDir).absolutePath();
    QDir().mkpath(m_outDir);

    // Popup modali (immagine mancante, avvisi): si annotano e si chiudono, o
    // l'exec() fermerebbe il test.
    auto *watcher = new QTimer(this);
    connect(watcher, &QTimer::timeout, this, [this] {
        QWidget *w = QApplication::activeModalWidget();
        if (!w) return;
        QString desc = w->windowTitle();
        if (auto *mb = qobject_cast<QMessageBox *>(w)) desc += QStringLiteral(": ") + mb->text();
        m_popups.append(desc.simplified());
        if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        else w->close();
    });
    watcher->start(100);
}

void ImageTest::wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Stesse sedi del test di andata e ritorno: surfaces/ e records/, in ordine.
void ImageTest::collect()
{
    const struct { const char *dir; bool isRecord; } branches[] = {
        { "surfaces", false }, { "records", true },
    };
    for (const auto &b : branches) {
        const QString base = m_root + QLatin1Char('/') + QLatin1String(b.dir);
        QStringList files;
        QDirIterator it(base, { QStringLiteral("*.json") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
        files.sort(Qt::CaseInsensitive);
        for (const QString &f : files) {
            const QString rel = QDir(m_root).relativeFilePath(f);
            if (!m_filter.isEmpty()) {
                bool match = false;
                for (const QString &part : m_filter.split(QLatin1Char('|'), Qt::SkipEmptyParts))
                    if (rel.contains(part, Qt::CaseInsensitive)) match = true;
                if (!match) continue;
            }
            m_entries.append({ f, rel, b.isRecord });
        }
    }
}

// Le chiamate del recorder all'avvio del REC (VideoRecorder::toggleRecord):
// path, flusso geodetico, rotazioni e tick dell'animazione. Gli orologi
// restano guidati da fuori per tutto il test, quindi il render non li avanza.
void ImageTest::freeze()
{
    GLWidget *gl = m_mw->ui->glWidget;
    if (m_mw->pathRunning(CameraPaths::Path4D)) m_mw->m_paths->stop(CameraPaths::Path4D);
    if (m_mw->pathRunning(CameraPaths::Path3D)) m_mw->m_paths->stop(CameraPaths::Path3D);
    if (m_mw->m_geoAnimTimer && m_mw->m_geoAnimTimer->isActive()) m_mw->m_geoAnimTimer->stop();
    gl->stopAllTimers();
    gl->setClocksDrivenByRecorder(true);
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
}

bool ImageTest::anyMotionRunning() const
{
    return m_mw->pathRunning(CameraPaths::Path4D) || m_mw->pathRunning(CameraPaths::Path3D)
           || (m_mw->m_geoAnimTimer && m_mw->m_geoAnimTimer->isActive())
           || m_mw->ui->glWidget->isAnimating();
}

void ImageTest::shoot(const Entry &e)
{
    GLWidget *gl = m_mw->ui->glWidget;
    m_popups.clear();

    LibraryManager lm;
    const LibraryItem item = lm.parseJson(e.path, e.isRecord ? LibraryType::Motion
                                                             : LibraryType::Surface);
    if (item.name.isEmpty()) {
        m_lines.append(QStringLiteral("NON CARICABILE  ") + e.rel);
        return;
    }
    // Stesse funzioni del click nell'albero (onExampleItemClicked); il fermo
    // subito dopo, prima che giri un solo tick.
    if (e.isRecord) m_mw->applyMotionExample(item);
    else            m_mw->applySurfaceExample(item);
    freeze();
    wait(m_settleMs);
    const bool deferred = anyMotionRunning();
    freeze();
    wait(200);

    const QImage first = gl->getFrameForVideo(m_width, m_height, true);
    wait(300);
    const QImage second = gl->getFrameForVideo(m_width, m_height, true);
    const ImageDiff self = compareImages(first, second);
    const bool stable = self.pixels == 0 && !self.sizeMismatch;

    const QString name = pngName(e.rel);
    const QString outPath = m_outDir + QStringLiteral("/images/") + name;
    QDir().mkpath(QFileInfo(outPath).absolutePath());
    first.save(outPath);

    QString line;
    if (!stable) {
        ++m_unstable;
        line = QStringLiteral("INSTABILE       %1  (%2 pixel cambiano fra due foto)").arg(e.rel).arg(self.pixels);
    } else if (deferred && !m_compareDir.isEmpty()) {
        // Posa presa dal tempo reale: due giri non la ritrovano uguale.
        line = QStringLiteral("DIFFERITO       %1  (fuori dal confronto)").arg(e.rel);
    } else if (!m_compareDir.isEmpty()) {
        const QImage ref(m_compareDir + QStringLiteral("/images/") + name);
        if (ref.isNull()) {
            ++m_missingRef;
            line = QStringLiteral("SENZA RIFERIM.  ") + e.rel;
        } else {
            const ImageDiff d = compareImages(ref, first);
            if (d.sizeMismatch) {
                ++m_different;
                line = QStringLiteral("DIVERSA         %1  (misure diverse)").arg(e.rel);
            } else if (d.pixels == 0) {
                ++m_same;
                line = QStringLiteral("uguale          ") + e.rel;
            } else {
                const double pct = 100.0 * double(d.pixels) / double(first.width() * first.height());
                // RUMORE: pochi pixel di uno o due livelli. E' la sezione
                // trasportata dei tubi, smorzata frame dopo frame: dipende da
                // quanti frame passano nell'assestamento, e gli stessi preset
                // risultano instabili per un pixel anche nel riferimento.
                const bool noise = pct <= kNoisePercent && d.maxDelta <= kNoiseDelta;
                if (noise) ++m_noise; else ++m_different;
                line = QStringLiteral("%1%2  (%3 pixel, %4%, scarto max %5)")
                           .arg(noise ? QStringLiteral("rumore          ") : QStringLiteral("DIVERSA         "), e.rel)
                           .arg(d.pixels).arg(pct, 0, 'f', 3).arg(d.maxDelta);
                const QString diffPath = m_outDir + QStringLiteral("/diff/") + name;
                QDir().mkpath(QFileInfo(diffPath).absolutePath());
                diffImage(ref, first).save(diffPath);
            }
        }
    } else {
        ++m_same;
        line = QStringLiteral("foto            ") + e.rel;
    }
    if (deferred) {
        ++m_deferred;
        if (m_compareDir.isEmpty())
            line += QStringLiteral("  [differito: un moto e' partito dopo il fermo, posa dal tempo reale]");
    }
    for (const QString &p : std::as_const(m_popups))
        line += QStringLiteral("  [popup chiuso: ") + p + QLatin1Char(']');
    m_lines.append(line);
    qInfo().noquote() << "[imagetest]" << line;
}

void ImageTest::run()
{
    collect();
    GLWidget *gl = m_mw->ui->glWidget;
    // Come nel REC: le catture sono lente per costruzione, e il watchdog
    // fermerebbe le animazioni con un popup (Glass 3-Torus) a meta' preset.
    gl->setRecordingActive(true);
    gl->beginHiResCapture(m_width, m_height);
    for (const Entry &e : std::as_const(m_entries)) shoot(e);
    gl->endHiResCapture();
    gl->setRecordingActive(false);
    gl->setClocksDrivenByRecorder(false);
    finish();
}

void ImageTest::finish()
{
    QStringList head;
    head << QStringLiteral("TEST DELLE IMMAGINI")
         << QStringLiteral("Libreria: ") + m_root
         << QStringLiteral("Foto %1x%2, assestamento %3 ms").arg(m_width).arg(m_height).arg(m_settleMs);
    if (!m_compareDir.isEmpty()) head << QStringLiteral("Riferimento: ") + m_compareDir;
    head << QString();
    if (!m_compareDir.isEmpty())
        head << QStringLiteral("Uguali al riferimento: %1").arg(m_same)
             << QStringLiteral("Rumore (fino allo %1% dei pixel, scarto <= %2; non conta): %3")
                    .arg(kNoisePercent).arg(kNoiseDelta).arg(m_noise)
             << QStringLiteral("DIVERSE: %1").arg(m_different)
             << QStringLiteral("Senza riferimento: %1").arg(m_missingRef);
    else
        head << QStringLiteral("Fotografati: %1").arg(m_same);
    head << QStringLiteral("Instabili (fuori dal confronto): %1").arg(m_unstable)
         << QStringLiteral("Con avvio differito (fuori dal confronto): %1").arg(m_deferred)
         << QString();

    QFile f(m_outDir + QStringLiteral("/image-report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream s(&f);
        for (const QString &l : std::as_const(head)) s << l << '\n';
        for (const QString &l : std::as_const(m_lines)) s << l << '\n';
    }
    const int rc = (!m_compareDir.isEmpty() && (m_different > 0 || m_missingRef > 0)) ? 1 : 0;
    QTimer::singleShot(0, qApp, [rc] { QCoreApplication::exit(rc); });
}
