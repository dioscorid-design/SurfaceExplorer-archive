#ifndef PRESETSERIALIZER_H
#define PRESETSERIALIZER_H

#include <QObject>
#include <QString>
#include <QColor>
#include "librarymanager.h"

class MainWindow;
class QJsonObject;

class PresetSerializer : public QObject
{
    Q_OBJECT
public:
    explicit PresetSerializer(MainWindow *parent);

    // Le 4 funzioni "pesanti" estratte dalla MainWindow
    void saveSurface(const QString &suggestedPath = "");
    void saveTexture(const QString &path);
    void saveMotion(const QString &suggestedPath = "");
    void saveSoundAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveSound(const QString &filePath);
    void saveScript();
    void saveTextureAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveSurfaceAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveMotionAs(const QString &startDir, const QString &sourceFilePath = "");

    // Salvataggio chiesto dall'avviso "vuoi salvare?": dialogo aperto sulla
    // RADICE dei preset e senza nome di default, perche' e' l'utente a dover
    // scegliere il ramo (= che cosa salvare). Ritorna true solo se il file e'
    // finito davvero su disco. Vedi il commento sull'implementazione.
    bool saveUnsavedWorkInteractive();

    // Moto in corsa al momento del Save: decide la chiave "activeMotion".
    struct MotionRunState {
        bool rotating = false;
        bool path4D   = false;
        bool path3D   = false;
    };

    // Il JSON che Save Surface / Save Record scriverebbero adesso, senza dialoghi
    // ne' disco. Unica implementazione: i save li chiamano dopo aver scelto
    // percorso, hint e suono; il test di andata e ritorno (presetroundtrip.cpp)
    // li chiama direttamente.
    QJsonObject buildSurfaceJson(const QString &name);
    QJsonObject buildMotionJson(const QString &name, const MotionRunState &run,
                                bool includeSound);

    // Lo stato della scena fotografato in un LibraryItem, cio' che i due
    // build*Json traducono con LibraryManager::toJson (tappa 2 dello "stato
    // unico della scena"). Nessun dialogo, nessun disco.
    LibraryItem captureSurfaceState(const QString &name);
    LibraryItem captureMotionState(const QString &name, const MotionRunState &run,
                                   bool includeSound);

private:
    // NIENTE DA SALVARE: una Shape da una scena senza superficie, una texture dal
    // bersaglio senza texture, un record da una scena vuota, un suono senza
    // suono. Lo dicono con un messaggio e ritornano true (il chiamante esce
    // prima del dialogo).
    bool refuseEmptyShape();
    bool refuseEmptyTexture();
    bool refuseEmptyRecord();
    bool refuseEmptySound();
    // Scrive i sei limiti parametrici in `limits`: sempre la chiave numerica
    // (uMin/uMax/...), piu' la gemella "...Expr" con il testo grezzo quando
    // l'utente ha scritto una formula anziche' un numero. Per il Save degli
    // script: e' captureParametricLimits + LibraryManager::writeParametricLimits,
    // le stesse che usano superfici e record.
    void writeParametricLimits(QJsonObject &limits);
    // Gemella per il taglio x/y/z del Ray Marching (captureSpaceLimits).
    void writeSpaceLimits(QJsonObject &limits);

    void captureCommonState(LibraryItem &d);
    void captureParametricLimits(LibraryItem &d);
    void captureSpaceLimits(LibraryItem &d);

    // Colore GLOBALE della superficie letto dal motore, la sua unica sede.
    // Vedi il commento sull'implementazione.
    QColor globalSurfaceColor() const;
    // Trasparenza GLOBALE dal motore, mai dallo slider (stessa ragione).
    double globalSurfaceAlpha() const;

    MainWindow *m_mainWindow;
};

#endif // PRESETSERIALIZER_H
