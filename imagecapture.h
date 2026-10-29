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
#pragma once

#include <QImage>
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

    // --- Logica pura, usata dal dialogo e dai test ---
    // Le dimensioni da un rapporto (largo/alto) e dal lato lungo in pixel.
    static QSize sizeFor(double ratio, int longSide);
    // La scala del campo visivo verticale per un'immagine di rapporto `ratio` da
    // una vista di rapporto `viewRatio` (1 se l'immagine e' piu' stretta).
    static float verticalScaleFor(double ratio, double viewRatio);
    // Il riquadro, nelle coordinate della vista, di cio' che entra nell'immagine.
    static QRect guideRect(const QSize &view, double ratio);
    // Rende l'immagine di dimensione `size` fuori schermo. Vuota (con `error`) se
    // la GPU non ce la fa.
    static QImage render(GLWidget *gl, const QSize &size, double ratio, QString *error = nullptr);
    // Secondi per megapixel, misurati su un fotogramma di prova: serve a stimare
    // il tempo di un'immagine grande (e ad avvisare se la scena e' pesante).
    static double secondsPerMegapixel(GLWidget *gl);
};
