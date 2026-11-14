// imagecapture.h - Lo scatto di un'IMMAGINE a risoluzione libera.
//
// Si sceglie dal tasto REC ("Image" invece di "Video"). La scena si disegna fuori
// schermo alla dimensione scelta (lo stesso meccanismo del motore nativo del
// video, GLWidget::beginHiResCapture), quindi la nitidezza NON dipende dal
// monitor: solo dal rapporto dei lati e dai pixel chiesti, fino al limite di
// texture della GPU (QRhi::TextureSizeMax). Si salva in PNG.
//
// L'inquadratura: la vista mostra, mentre il dialogo e' aperto, un riquadro con
// il rapporto scelto (FrameGuide). Cio' che sta nel riquadro e' cio' che entra
// nell'immagine: per un rapporto piu' stretto della vista si taglia ai lati, per
// uno piu' largo si riduce il campo visivo verticale (GLWidget::
// setCaptureVerticalScale), cosi' l'orizzontale resta quello della vista.
// Il riquadro occupa sempre tutta l'altezza o tutta la larghezza della vista, e
// lungo l'altro lato si TRASCINA col mouse o col dito (doppio clic: di nuovo al
// centro): l'immagine e' la parte della vista sotto di lui, una finestra
// decentrata dello stesso campo visivo (GLWidget::setCaptureShift). Per poterlo
// trascinare il dialogo non e' modale; il resto della finestra principale e'
// fermo finche' resta aperto (CaptureSession in imagecapture.cpp).
#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <functional>

class GLWidget;
class QDialog;
class QWidget;

class ImageCapture
{
public:
    // Il flusso intero: dialogo, file, rendering, salvataggio. `defaultDir` e'
    // dove propone di salvare. true se un'immagine e' stata salvata. La scena
    // deve essere gia' ferma (lo fa chi chiama, come per il REC).
    static bool run(GLWidget *gl, QWidget *parent, const QString &defaultDir);
    // Solo per i test: se non e' vuoto, run() salva li' senza aprire il selettore
    // di file (che sul Mac e' una finestra di sistema, non automatizzabile).
    static void setTestSavePath(const QString &path);
    // Solo per i test: chiamata con il dialogo appena aperto (prima di ogni altro
    // evento), per fotografarlo e accettarlo senza una persona.
    static void setTestDialogHandler(std::function<void(QDialog *)> handler);
    // Solo per i test: i prossimi `frames` fotogrammi di render() escono di un solo
    // colore, come quelli che la GPU non riesce a finire.
    static void setTestBlankFrames(int frames);
    // Solo per i test: il dialogo va nell'angolo in alto a sinistra della
    // finestra come sul telefono (dove la vista e' troppo piccola per tenerlo
    // al centro: copriva il riquadro guida).
    static void setTestCornerDialog(bool on);

    // --- Logica pura, usata dal dialogo e dai test ---
    // Le dimensioni da un rapporto (largo/alto) e dal lato lungo in pixel.
    static QSize sizeFor(double ratio, int longSide);
    // La scala del campo visivo verticale per un'immagine di rapporto `ratio` da
    // una vista di rapporto `viewRatio` (1 se l'immagine e' piu' stretta).
    static float verticalScaleFor(double ratio, double viewRatio);
    // Il riquadro, nelle coordinate della vista, di cio' che entra nell'immagine.
    // `position`: dove sta lungo il lato libero, da -1 (tutto a sinistra, o in
    // alto) a +1 (a destra, o in basso), 0 = al centro. Conta x per i rapporti
    // piu' stretti della vista, y per quelli piu' larghi.
    static QRect guideRect(const QSize &view, double ratio, const QPointF &position = QPointF());
    // Lo spostamento del riquadro in `position` per GLWidget::setCaptureShift: la
    // traslazione, nelle NDC dell'immagine (y in alto), che porta nell'immagine la
    // parte della vista sotto il riquadro. Zero al centro.
    static QPointF captureShiftFor(double ratio, double viewRatio, const QPointF &position);
    // Tutti i pixel di un colore: il sintomo di un fotogramma fallito sulla GPU.
    static bool isSingleColor(const QImage &im);
    // Un wireframe di `segments` segmenti largo `width` pixel sta nei limiti del
    // dispositivo (WireLimits)? L'unica regola, per il dialogo e per render().
    static bool wireFits(int maxWidth, qint64 budget, qint64 segments, int width);
    // Rende l'immagine di dimensione `size` fuori schermo. Vuota (con `error`) se
    // la GPU non ce la fa.
    // `wireFactor`: spessore del wireframe rispetto a quello proporzionale alla vista
    // (1 = come a schermo, 0.5 sottile, 2 spesso). `position`: il riquadro
    // trascinato (guideRect).
    static QImage render(GLWidget *gl, const QSize &size, double ratio, QString *error = nullptr,
                         double wireFactor = 1.0, const QPointF &position = QPointF());
    // Secondi per megapixel, misurati su un fotogramma di prova: serve a stimare
    // il tempo di un'immagine grande (e ad avvisare se la scena e' pesante).
    static double secondsPerMegapixel(GLWidget *gl);
};
