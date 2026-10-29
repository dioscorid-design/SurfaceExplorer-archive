// mainwindow_scene.cpp - MainWindow: la scena come insieme -- lavoro non salvato
// (impronta per valore, conferme), reset di scena, load di superfici e
// record (applySurfaceExample, applyMotionExample, applyCommonData).
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


// =============================================================================
// LAVORO NON SALVATO E CONFLITTO FRA DOCK
// =============================================================================
// Un edit dell'utente su un modulo qualsiasi (equazioni, equazione implicita,
// script, texture) segna la scena come "sporca": il reset di modalita' chiedera'
// se salvare prima di buttare via tutto.
//
// `source` e' il campo che ha ricevuto l'edit: serve al SECONDO avviso, quello
// che segnala di stare scrivendo su un dock mentre la superficie a schermo
// arriva da un altro. E' la situazione in cui i due si contendono la scena e il
// risultato sorprende (il vecchio vince, o si mescolano stati incompatibili):
// il consiglio e' resettare prima. L'avviso NON blocca: si limita a informare, e
// compare una volta sola per situazione -- la coppia (dock scritto, sorgente)
// in m_warnedEditedDock/m_warnedOrigin -- non a ogni tasto premuto.
void MainWindow::noteSceneEdited(QWidget *source)
{
    // Ci arriva solo la digitazione: load e reset scrivono i campi a segnali
    // bloccati (setEqText, setRmText, setScriptText). Le scritture del boot
    // precedono m_uiReady.
    if (!m_uiReady) return;

    if (!source) return;

    // DOVE STA SCRIVENDO L'UTENTE. txtScriptEditor e' UN widget per tre moduli:
    // conta come dock Script solo quando mostra lo script di SUPERFICIE --
    // scrivere una texture o un suono non contende la scena alle equazioni.
    SurfaceOrigin editedDock;
    if (source == ui->lineX || source == ui->lineY || source == ui->lineZ
     || source == ui->lineP || source == ui->lineEquation
     || source == ui->lineEquationCrossSection) {
        editedDock = OriginEquations;
    } else if (source == ui->txtScriptEditor
            && m_currentScriptMode == ScriptModeSurface) {
        editedDock = OriginScript;
    } else {
        return;   // campo che non definisce la superficie: nessun conflitto
    }

    // Nessun conflitto se la superficie e' quella di DEFAULT (nessun dock e'
    // impegnato: i suoi campi sono pieni, ma non sono lavoro da difendere), se
    // si sta scrivendo proprio nel dock che l'ha prodotta, o se e' una superficie
    // METRICA, che vive legittimamente in tutti e due i dock.
    if (m_surfaceOrigin == OriginDefault) return;
    if (m_surfaceOrigin == OriginBoth)    return;
    if (m_surfaceOrigin == editedDock)    return;

    // Gia' avvisato per QUESTA situazione: una coppia (dock scritto, sorgente)
    // diversa e' un conflitto diverso e merita il suo avviso.
    if (m_warnedEditedDock == editedDock && m_warnedOrigin == m_surfaceOrigin) return;
    m_warnedEditedDock = editedDock;
    m_warnedOrigin     = m_surfaceOrigin;

    const QString loadedWhat = (m_surfaceOrigin == OriginScript)
                             ? QStringLiteral("a SCRIPT")
                             : QStringLiteral("the EQUATIONS panel");

    // Vedi sopra: il testo principale non va a capo, il corpo sta
    // nell'informativeText o il box invade tutto lo schermo.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Another module is loaded"));
    box.setText("The surface on screen comes from " + loadedWhat + ".");
    box.setInformativeText(
        "What you are typing here may not take effect, or may mix with what is "
        "already loaded."
        "\n\nTo start from a clean state, click the tab of the current mode "
        "(Parametric or Implicit) to restore the default surface, or press NEW "
        "on the status bar to clear everything and leave the scene empty.");
    box.exec();
}

// =============================================================================
// LAVORO NON SALVATO, PER VALORE (vedi mainwindow.h)
// =============================================================================
namespace {
void flattenFingerprint(const QString &prefix, const QJsonValue &v, QMap<QString, QJsonValue> &out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            flattenFingerprint(prefix.isEmpty() ? it.key() : prefix + QLatin1Char('/') + it.key(), it.value(), out);
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (int i = 0; i < a.size(); ++i)
            flattenFingerprint(QStringLiteral("%1[%2]").arg(prefix).arg(i), a.at(i), out);
    } else if (v.isString()) {
        // Testo in forma canonica: fine riga unica, niente spazi in coda. Lo
        // stesso script letto dall'editor o dal suo slot non deve risultare
        // diverso per un "\r\n" o per uno spazio a fine riga.
        QString t = v.toString();
        t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        QStringList lines = t.split(QLatin1Char('\n'));
        for (QString &l : lines)
            while (!l.isEmpty() && l.at(l.size() - 1).isSpace()) l.chop(1);
        out.insert(prefix, lines.join(QLatin1Char('\n')).trimmed());
    } else {
        out.insert(prefix, v);
    }
}
} // namespace

MainWindow::FingerprintPart MainWindow::fingerprintPart(const QString &key)
{
    if (key.startsWith(QLatin1String("sound/")) || key == QLatin1String("soundLibName"))
        return PartSound;
    if (key.startsWith(QLatin1String("texture/")))
        return PartTexture;
    // Dello sfondo sono della SCENA il colore e la forma (li comandano i
    // controlli del dock Renderer); il resto e' la sua texture.
    if (key.startsWith(QLatin1String("background/")))
        return (key == QLatin1String("background/color") || key == QLatin1String("background/skyMode"))
                   ? PartCore : PartTexture;
    if (key.startsWith(QLatin1String("meshParts[")))
        return key.section(QLatin1Char('/'), 1).startsWith(QLatin1String("tex")) ? PartTexture : PartCore;
    // Tubo: la curva e' testo della superficie (conta quando il Run la porta a
    // schermo, come le equazioni); lo spessore agisce subito, come le costanti.
    if (key.startsWith(QLatin1String("tube/")))
        return key == QLatin1String("tube/thickness") ? PartCore : PartSurfaceText;
    if (key.startsWith(QLatin1String("equations/")) || key.startsWith(QLatin1String("geodesic/"))
        || key.startsWith(QLatin1String("limits/")) || key == QLatin1String("scriptCode")
        || key == QLatin1String("implicitEquation") || key == QLatin1String("crossSectionEquation"))
        return PartSurfaceText;
    return PartCore;
}

MainWindow::SceneFingerprint MainWindow::fingerprintSlice(const SceneFingerprint &fp, FingerprintPart part)
{
    SceneFingerprint out;
    for (auto it = fp.constBegin(); it != fp.constEnd(); ++it)
        if (fingerprintPart(it.key()) == part) out.insert(it.key(), it.value());
    return out;
}

// Cio' che Save Record scriverebbe adesso, senza la posa e senza i dati che non
// sono stato della scena (nome, tipo, messaggi).
MainWindow::SceneFingerprint MainWindow::sceneFingerprint() const
{
    SceneFingerprint fp;
    if (!m_uiReady || !m_presetSerializer || !ui->glWidget) return fp;

    PresetSerializer::MotionRunState run;
    run.rotating = ui->glWidget->isAnimating();
    run.path4D   = pathRunning(CameraPaths::Path4D);
    run.path3D   = pathRunning(CameraPaths::Path3D);
    // Senza il suono dentro il codice della texture: e' un modulo a parte.
    QJsonObject root = m_presetSerializer->buildMotionJson(QString(), run, /*includeSound=*/false);

    // Via la posa (la seguono gli eventi, vedi noteViewControlUsed), i dati che
    // non sono stato della scena (nome, tipo, messaggi) e l'ambito All/Mesh,
    // che sceglie su CHE COSA agiscono i comandi e non e' lavoro da salvare.
    for (const char *k : { "name", "type", "camera3D", "angles", "observer4D",
                           "hintText", "hintSeconds", "textureHintText", "textureHintSeconds",
                           "meshScopeAll" })
        root.remove(QLatin1String(k));
    QJsonObject bg = root.value(QLatin1String("background")).toObject();
    bg.remove(QLatin1String("hintText"));
    bg.remove(QLatin1String("hintSeconds"));
    root.insert(QLatin1String("background"), bg);

    flattenFingerprint(QString(), root, fp);

    // Il suono: com'e' scritto nel suo slot (anche non eseguito, come gli
    // altri moduli).
    const QString sound = soundCode();
    flattenFingerprint(QStringLiteral("sound/code"), QJsonValue(sound), fp);
    return fp;
}

void MainWindow::markSceneClean()
{
    const SceneFingerprint fp = sceneFingerprint();
    m_cleanScene     = fp;
    m_appliedSurface = fingerprintSlice(fp, PartSurfaceText);
    m_cleanTexture   = fingerprintSlice(fp, PartTexture);
    m_cleanSound     = fingerprintSlice(fp, PartSound);
    m_pickedTexture  = m_cleanTexture;
    m_pickedSound    = m_cleanSound;
    m_viewTouched = false;
    m_textureViewTouched = false;
}

// Save Surface: la scena e' su disco come superficie. I moduli texture e suono
// restano com'erano -- un file di surfaces/ non li contiene, e il loro lavoro
// ha il suo avviso e il suo ramo.
void MainWindow::markSurfaceSaved()
{
    const SceneFingerprint fp = sceneFingerprint();
    m_cleanScene     = fp;
    m_appliedSurface = fingerprintSlice(fp, PartSurfaceText);
    m_pickedTexture  = fingerprintSlice(fp, PartTexture);
    m_pickedSound    = fingerprintSlice(fp, PartSound);
    m_viewTouched = false;
}

void MainWindow::markTexturePicked()
{
    m_cleanTexture  = fingerprintSlice(sceneFingerprint(), PartTexture);
    m_pickedTexture = m_cleanTexture;
    m_textureViewTouched = false;
}

void MainWindow::markTextureSaved()
{
    m_cleanTexture = fingerprintSlice(sceneFingerprint(), PartTexture);
    m_textureViewTouched = false;
}

void MainWindow::markSoundPicked()
{
    m_cleanSound  = fingerprintSlice(sceneFingerprint(), PartSound);
    m_pickedSound = m_cleanSound;
}

void MainWindow::markSoundSaved()
{
    m_cleanSound = fingerprintSlice(sceneFingerprint(), PartSound);
}

void MainWindow::noteSurfaceApplied(const QStringList &prefixes)
{
    if (!m_uiReady) return;
    const SceneFingerprint now = fingerprintSlice(sceneFingerprint(), PartSurfaceText);
    if (prefixes.isEmpty()) { m_appliedSurface = now; return; }
    auto matches = [&prefixes](const QString &key) {
        for (const QString &p : prefixes) if (key.startsWith(p)) return true;
        return false;
    };
    for (auto it = m_appliedSurface.begin(); it != m_appliedSurface.end(); )
        it = matches(it.key()) ? m_appliedSurface.erase(it) : it + 1;
    for (auto it = now.constBegin(); it != now.constEnd(); ++it)
        if (matches(it.key())) m_appliedSurface.insert(it.key(), it.value());
}

void MainWindow::absorbChangesSince(const SceneFingerprint &before)
{
    const SceneFingerprint now = sceneFingerprint();
    QSet<QString> keys;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) keys.insert(it.key());
    for (auto it = now.constBegin(); it != now.constEnd(); ++it) keys.insert(it.key());
    // Chi era pulito su quella chiave resta pulito; chi era gia' sporco no.
    auto absorb = [&](SceneFingerprint &ref, const QString &k) {
        if (ref.value(k) != before.value(k)) return;
        if (now.contains(k)) ref.insert(k, now.value(k));
        else ref.remove(k);
    };
    for (const QString &k : std::as_const(keys)) {
        if (before.value(k) == now.value(k)) continue;
        absorb(m_cleanScene, k);
        switch (fingerprintPart(k)) {
        case PartSurfaceText: absorb(m_appliedSurface, k); break;
        case PartTexture:     absorb(m_cleanTexture, k); absorb(m_pickedTexture, k); break;
        case PartSound:       absorb(m_cleanSound, k);   absorb(m_pickedSound, k);   break;
        case PartCore:        break;
        }
    }
}

// Le chiavi che fanno uscire l'avviso, col nome della parte che difendono.
QStringList MainWindow::unsavedKeys() const
{
    QStringList out;
    const SceneFingerprint fp = sceneFingerprint();
    QSet<QString> keys;
    for (auto it = fp.constBegin(); it != fp.constEnd(); ++it) keys.insert(it.key());
    for (auto it = m_cleanScene.constBegin(); it != m_cleanScene.constEnd(); ++it) keys.insert(it.key());
    QStringList sorted(keys.constBegin(), keys.constEnd());
    sorted.sort();
    for (const QString &k : std::as_const(sorted)) {
        const QJsonValue clean = m_cleanScene.value(k);
        switch (fingerprintPart(k)) {
        case PartCore:
            if (fp.value(k) != clean) out << QStringLiteral("scena:") + k;
            break;
        case PartSurfaceText:
            // Conta cio' che e' andato a schermo, non il testo nei campi.
            if (m_appliedSurface.value(k) != clean) out << QStringLiteral("scena:") + k;
            break;
        case PartTexture:
            if (m_pickedTexture.value(k) != clean) out << QStringLiteral("scena:") + k;
            if (fp.value(k) != m_cleanTexture.value(k)) out << QStringLiteral("texture:") + k;
            break;
        case PartSound:
            if (m_pickedSound.value(k) != clean) out << QStringLiteral("scena:") + k;
            if (fp.value(k) != m_cleanSound.value(k)) out << QStringLiteral("suono:") + k;
            break;
        }
    }
    if (m_viewTouched) out << QStringLiteral("scena:(vista mossa)");
    if (m_textureViewTouched) out << QStringLiteral("texture:(inquadratura 2D mossa)");
    return out;
}

// L'utente ha lavoro di SCENA da proteggere: lo stato che il record salverebbe
// differisce da quello dell'ultimo momento pulito.
//
// Si protegge il lavoro che E' ANDATO A SCHERMO, non il testo nei campi: le
// equazioni (e limiti, script) contano da quando un Run riuscito le ha
// applicate. Restano fuori i due casi in cui l'avviso offrirebbe di salvare il
// nulla -- equazioni che danno errore e testo scritto e mai eseguito -- senza
// nascondere il lavoro gia' applicato quando si ricomincia a scrivere.
bool MainWindow::hasUnsavedWork() const
{
    const QStringList keys = unsavedKeys();
    for (const QString &k : keys)
        if (k.startsWith(QLatin1String("scena:"))) return true;
    return false;
}

bool MainWindow::textureModuleDirty() const
{
    const QStringList keys = unsavedKeys();
    for (const QString &k : keys)
        if (k.startsWith(QLatin1String("texture:"))) return true;
    return false;
}

bool MainWindow::soundModuleDirty() const
{
    const QStringList keys = unsavedKeys();
    for (const QString &k : keys)
        if (k.startsWith(QLatin1String("suono:"))) return true;
    return false;
}

// La POSA mossa dall'utente: trascinamento del mouse, rotella, touch, tasti di
// camera e di rotazione a scatto. E' l'unico lavoro che si segue per EVENTI:
// camera e angoli li muovono anche rotazioni e path, da soli, e il confronto
// per valore sporcherebbe la scena a ogni frame.
// A SCENA VUOTA non marca nulla: dopo un NEW non c'e' niente da proteggere, e
// ruotare il nulla non e' lavoro (l'app chiedeva "vuoi salvare?" su una scena
// in cui l'utente non aveva messo niente).
void MainWindow::noteViewControlUsed()
{
    // Ci arrivano solo gesti (mouse, touch, tasti di camera); prima di
    // m_uiReady nessuno.
    if (!m_uiReady) return;
    if (isSceneEmpty()) return;
    m_viewTouched = true;
}

// Collega i comandi di POSA. Tutto il resto (aspetto, costanti, velocita',
// path, texture, suono) non si collega piu': lo dice il confronto dello stato.
void MainWindow::wireViewControlsTracking()
{
    // Tasti a scatto dei dock 3D e 4D che spostano camera, osservatore o
    // angoli: ogni pressione muove la posa.
    const QList<QAbstractButton*> steppers = {
        ui->btnRollLeft,  ui->btnRollRight,
        ui->btnOmegaAhead, ui->btnOmegaRear,
        ui->btnPhiAhead,   ui->btnPhiRear,
        ui->btnPsiAhead,   ui->btnPsiRear,
        ui->btnXPlus, ui->btnXMinus, ui->btnYPlus, ui->btnYMinus,
        ui->btnZPlus, ui->btnZMinus, ui->btnPPlus, ui->btnPMinus,
        ui->btnUp,    ui->btnDown,   ui->btnLeft,  ui->btnRight,
        ui->btnForward, ui->btnBackward
    };
    for (QAbstractButton *b : steppers) {
        if (b) connect(b, &QAbstractButton::clicked, this, [this]() { noteViewControlUsed(); });
    }

    // Il checkbox della texture mostra o nasconde: l'albero Library va
    // risincronizzato in tutti e due i casi (spenta non indica nulla, riaccesa
    // torna a evidenziare la texture che e' di nuovo a schermo).
    if (ui->chkBoxTexture) {
        connect(ui->chkBoxTexture, &QAbstractButton::toggled, this, [this]() {
            if (!m_uiReady) return;
            syncTextureTreeSelection();
        });
    }

    // --- MOUSE E TOUCH SULLA VISTA ---
    // Segnale dedicato: rotationChanged() non va bene, lo emette a ogni frame
    // anche il moto automatico.
    // In vista 2D (editing piatto della texture) il mouse non muove la scena:
    // zoom, pan e rotazione sono l'inquadratura della TEXTURE, lavoro del suo
    // modulo.
    if (ui->glWidget) {
        connect(ui->glWidget, &GLWidget::userMovedView, this, [this]() {
            if (ui->glWidget && ui->glWidget->isFlatView()) {
                if (!m_uiReady) return;
                m_textureViewTouched = true;
                return;
            }
            noteViewControlUsed();
        });
    }
}

MainWindow::RunOutcomeGuard::RunOutcomeGuard(MainWindow *mw, bool arm)
    : w(mw), before(InputValidator::errorCount()), armed(arm)
{
}

// Nessun errore mostrato durante il Run = cio' che l'utente ha scritto e'
// andato a schermo.
MainWindow::RunOutcomeGuard::~RunOutcomeGuard()
{
    // COSTANTI: all'uscita di un Run RIUSCITO campi e schermo coincidono di
    // nuovo, ed e' qui che una costante tolta dalle equazioni cade davvero in
    // disuso e torna neutra (vedi updateConstantsUIState). Dopo un Run fallito
    // a schermo c'e' ancora la superficie di prima: le modifiche restano in
    // sospeso e le sue costanti non si toccano.
    if (InputValidator::errorCount() == before)
        w->refreshConstants();
    // LAVORO NON SALVATO: da questo momento il testo della superficie conta per
    // l'avviso "vuoi salvare?" (vedi noteSurfaceApplied). Dopo un Run fallito
    // no: a schermo non e' andato niente. Nel commit di servizio le equazioni
    // sono quelle gia' applicate, non il testo dei campi: entrano solo limiti
    // e script.
    if (armed && surfaceApplied && InputValidator::errorCount() == before) {
        if (equationsApplied)
            w->noteSurfaceApplied();
        else
            w->noteSurfaceApplied({ QStringLiteral("limits/"), QStringLiteral("scriptCode"),
                                    QStringLiteral("implicitEquation"), QStringLiteral("crossSectionEquation") });
    }
}


// UNICA conferma per il lavoro non salvato. `scope` dice CHE COSA sta per
// essere perso, e da li' discende sia quando chiedere sia dove salvare.
//
//   ScopeScene   - si perde tutta la scena: sostituzione con un altro preset,
//                  superficie di default, NEW, reset/cambio di modalita'.
//                  Si difende tutto cio' che e' sporco (scena, texture, suono)
//                  con UN popup solo, e il salvataggio parte dalla radice --
//                  scegliere "records" tiene insieme superficie, texture,
//                  suono, camera e rotazioni in un file solo.
//   ScopeTexture - si perde la sola texture (se ne sta caricando un'altra).
//   ScopeSound   - si perde il solo suono.
//                  Per questi due si guarda SOLO il flag del modulo e il
//                  dialogo punta diritto al suo ramo: il tipo e' gia' deciso.
//
// Il criterio e' stato scelto dall'utente dopo due versioni sbagliate: una che
// apriva un popup per ogni modulo sporco, e una che per qualunque modifica
// chiedeva della scena intera. Caricare una texture cambia si' la scena, ma
// far uscire li' l'avviso "stai perdendo la scena" produceva una raffica di
// popup a ogni modifica: la domanda si fa quando si perde la scena INTERA.
bool MainWindow::confirmDiscardUnsaved(DiscardScope scope)
{
    // Qualunque salvataggio parta da qui e' un "salva prima di buttare via":
    // subito dopo il chiamante carica un altro preset o resetta la scena, ed e'
    // QUELLO che l'utente sta guardando. La libreria si aggiorna comunque (il
    // file nuovo compare), ma il focus non deve spostarsi sul file salvato.
    // Il rilascio e' ritardato perche' i writer chiamano refreshAndSelectPreset
    // a 100ms: azzerando il flag all'uscita di questa funzione, quei timer lo
    // troverebbero gia' spento.
    m_suppressSelectAfterSave = true;
    struct SelectGuard {
        MainWindow *w;
        ~SelectGuard() {
            QTimer::singleShot(300, w, [w = this->w]() { w->m_suppressSelectAfterSave = false; });
        }
    } selectGuard{this};

    // --- Moduli singoli: si guarda un flag solo e si salva nel suo ramo ---
    if (scope == ScopeTexture || scope == ScopeSound) {
        const bool isTex = (scope == ScopeTexture);
        if (isTex ? !textureModuleDirty() : !soundModuleDirty()) return true;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(isTex ? "Unsaved texture" : "Unsaved sound");
        box.setText(isTex ? "The current texture has unsaved changes."
                          : "The current sound has unsaved changes.");
        box.setInformativeText("They will be discarded. Do you want to save it first?");
        QPushButton *saveBtn    = box.addButton("Save",       QMessageBox::AcceptRole);
        QPushButton *discardBtn = box.addButton("Don't save", QMessageBox::DestructiveRole);
        box.addButton("Cancel", QMessageBox::RejectRole);
        box.setDefaultButton(saveBtn);
        // "Don't save" chiede 109px contro i 108 imposti dal foglio globale:
        // sforava di UN pixel e si perdeva la "D".
        UiStyleManager::widenMessageBoxButtons(&box);
        box.exec();

        if (box.clickedButton() == discardBtn) return true;
        if (box.clickedButton() != saveBtn)    return false;   // Cancel

        QSettings settings;
        if (isTex) {
            // Cartella di partenza: l'ultima usata per le texture, con fallback
            // al ramo del tipo (stessa logica di onSaveTextureClicked).
            QString startDir = settings.value("lastCustomTexDir").toString();
            if (startDir.isEmpty() || startDir.contains("build", Qt::CaseInsensitive)
                || !QDir(startDir).exists()) {
                startDir = LibraryFolders::root() + "/textures";
            }
            m_lastSaveSucceeded = false;
            m_presetSerializer->saveTextureAs(startDir, m_currentTexturePresetPath);
            return m_lastSaveSucceeded;   // vero solo se il file e' su disco
        }
        QString startDir = settings.value("pathSounds",
                                          LibraryFolders::root() + "/sounds").toString();
        m_lastSaveSucceeded = false;
        m_presetSerializer->saveSoundAs(startDir, "");
        return m_lastSaveSucceeded;
    }

    // --- Scena intera: si difende tutto cio' che e' sporco, con un popup solo ---
    //
    const bool dirtyScene = hasUnsavedWork();
    const bool dirtyTex   = textureModuleDirty();
    const bool dirtySnd   = soundModuleDirty();
    if (!dirtyScene && !dirtyTex && !dirtySnd) return true;

    // Elenco leggibile di cio' che si sta per perdere: senza, l'avviso e'
    // identico sia che si butti via un'equazione sia un intero record.
    QStringList parts;
    if (dirtyScene) parts << "scene";
    if (dirtyTex)   parts << "texture";
    if (dirtySnd)   parts << "sound";
    QString what = parts.size() == 1
                     ? parts.first()
                     : parts.mid(0, parts.size() - 1).join(", ") + " and " + parts.last();
    what[0] = what[0].toUpper();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Unsaved work");
    box.setText(what + (parts.size() == 1 ? " has unsaved changes."
                                          : " have unsaved changes."));
    box.setInformativeText(
        parts.size() == 1
            ? "They will be discarded. Do you want to save first?"
            : "They will be discarded. Save them as a Record to keep them all in one file.");
    QPushButton *saveBtn    = box.addButton("Save",       QMessageBox::AcceptRole);
    QPushButton *discardBtn = box.addButton("Don't save", QMessageBox::DestructiveRole);
    box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    // Come sopra: "Don't save" non ci sta nei 108px di default.
    UiStyleManager::widenMessageBoxButtons(&box);
    box.exec();

    if (box.clickedButton() == discardBtn) return true;
    if (box.clickedButton() != saveBtn)    return false;   // Cancel o finestra chiusa

    // Un salvataggio solo, dalla radice dell'albero: il ramo scelto decide il
    // formato, e "records" e' quello che tiene insieme superficie, texture,
    // suono, camera e rotazioni.
    //
    // La radice, e non il ramo gia' deciso: da un record si puo' voler salvare
    // la SOLA superficie, quindi surfaces/ deve restare raggiungibile. Aprire
    // direttamente in records/ (col navFloor li') lo impedirebbe -- provato e
    // scartato. Qui la scelta di ramo e' parte del gesto.
    // L'esito e' vero solo se il file e' finito su disco.
    return m_presetSerializer->saveUnsavedWorkInteractive();
}

// Pulizia totale + superficie di default della modalita' `index` (0 =
// parametrico, 1 = ray marching). Era la lambda di currentChanged: estratta
// perche' ha un secondo chiamante, il clic sulla linguetta GIA' attiva, che la
// usa come "ricomincia da capo". Il corpo non e' stato modificato -- e' il
// percorso di reset gia' in produzione, e deve restare in una sede sola.
void MainWindow::applyModeTabReset(int index)
{
    // Cambio di modalita' annullato dall'utente nel dialogo del lavoro non
    // salvato: la linguetta e' gia' cambiata (Qt non permette di fermarla da
    // tabBarClicked) e sta per tornare indietro da sola, ma la scena non va
    // toccata. Vedi m_suppressNextModeTabReset.
    if (m_suppressNextModeTabReset) return;

    resetScene(index, /*loadDefaultSurface=*/true);
}

// Corpo del reset, parametrizzato sulla superficie finale (vedi mainwindow.h).
// Con loadDefaultSurface=false la pulizia e' IDENTICA ma non si scrive nessuna
// equazione e non si compila nulla: resta la scena vuota del tasto New.
// ---- Passi di resetScene (le guardie RAII restano a lei) ----

void MainWindow::stopSceneForReset()
{
    // Scena nuova: la scala della texture geodetica si rifissa al primo calcolo
    // (vedi setCustomMesh). Come al load di un preset -- e a differenza di un
    // semplice cambio di limiti, dove il riferimento deve restare fermo.
    if (ui->glWidget) ui->glWidget->resetGeodesicUvReference();

    // Sfondo di nuovo FISSO: entrambi i rami del reset spengono lo sfondo
    // (setBackgroundTextureEnabled(false)), e la sua forma ne fa parte.
    // Qui, nella parte comune, per non doverlo ricordare in ciascun ramo.
    applyBackgroundSkyMode(GLWidget::BgFixed);

    // FLUSSO GEODETICO fermato PRIMA di svuotare i campi. E' un moto come le
    // rotazioni e i path (fermati poco piu' sotto) ma non veniva mai spento dal
    // reset: il suo timer continuava a girare su una scena azzerata, chiamava
    // updateGeodesicMesh e la validazione dei limiti -- ora vuoti -- faceva
    // comparire il popup "Minimum values must be strictly less than Maximum"
    // subito dopo il reset, senza che l'utente avesse premuto niente.
    // Va fermato prima dello svuotamento: un tick che passasse fra clear() e
    // stop() troverebbe gia' i campi vuoti.
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
    m_geodesicErrorPending = false;

    // ==========================================================
    // RESET PATH CAMERA (4D e 3D) AL CAMBIO TAB
    // ==========================================================
    // Il cambio tab carica la superficie di default, quindi nessun residuo
    // del path del preset precedente deve sopravvivere. Lo stop passa dai
    // TASTI (onDeparture*Clicked): testo riportato a "DEPARTURE",
    // setPathAnimating(false), master button riallineato. Il ramo Ray
    // Marching fermava i timer con m_paths->stop(CameraPaths::Path4D) diretto: il tasto
    // restava su STOP e campi/tempo/flag del path sopravvivevano al tab.
    if (pathRunning(CameraPaths::Path4D)) onDepartureClicked();
    if (pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();
}

void MainWindow::resetScriptEngineForNewScene()
{
    // ==========================================================
    // AGGIORNAMENTO UI E PULIZIA MOTORE SCRIPT AL CAMBIO TAB
    // ==========================================================
    // 1. Aggiorna dinamicamente i nomi sui bottoni del dock script
    updateScriptButtonText();

    // 2. Spegne la modalità script per evitare che il codice parametrico
    // finisca nel Ray Marching (e causi il crash "unexpected EQUAL")
    if (ui->glWidget && ui->glWidget->getEngine()) {
        ui->glWidget->getEngine()->setScriptMode(false);
        ui->glWidget->getEngine()->setScriptCodeGLSL("");
        // Azzera anche il cutout: senza, il //CUTOUT di uno script
        // precedente restava iniettato e continuava a tagliare (vedi
        // applyCommonData). Un nuovo Run script lo reimposta dal contenuto.
        ui->glWidget->getEngine()->setCutoutCodeGLSL("");
        // Stessa cosa per le parti multi-mesh: senza azzerarle, la
        // superficie del tab successivo resterebbe spezzata nei rami
        // dichiarati dallo script precedente.
        ui->glWidget->getEngine()->clearMeshParts();
        m_lastSectionCount = 0;
        m_lastAppliedMeshCount = 1;
        m_lastSourceUsesMesh = false;
        // ...e il taglio dell'ambito "All", che altrimenti resterebbe in vigore
        // sulla superficie nuova.
        ui->glWidget->getEngine()->clearAllDomain();
    }

    // BERSAGLIO SU SURFACE, per entrambe le modalita': la scena nuova non ha
    // uno sfondo (lo spengono tutti e due i rami qui sotto), quindi il dock
    // Renderer non resta puntato li'. radioSurface e radioBackground sono
    // esclusivi, ma si toccano a segnali bloccati: il ripristino del dock e'
    // gestito esplicitamente qui intorno e l'handler toggled di radioBackground
    // non deve girare.
    // QUI, prima dell'editor e del checkbox Texture, perche' tutti e due
    // guardano il bersaglio. Prima lo faceva ciascun ramo, in fondo: l'editor
    // restava con lo script dello sfondo appena spento, e il checkbox veniva
    // riallineato dal solo ramo parametrico con un setChecked a segnali VIVI --
    // se mostrava lo sfondo acceso faceva girare il suo handler a meta' reset
    // (in Ray Marching restava acceso, con l'etichetta dello sfondo).
    showSurfaceTarget();

    // 3. Gli slot dei moduli sono gia' quelli della scena di default (vedi
    // defaultScene): NEW li svuota tutti -- l'editor restava pieno dello
    // script texture della superficie buttata via --; il CAMBIO di linguetta
    // verso il parametrico tiene quello della texture (l'editor ne e' solo la
    // vista, e tornando in parametrico lo si ritrova), verso il Ray Marching lo
    // svuota (li' il dock non lo mostra nemmeno: shownScriptSlot); il RICLIC
    // sulla linguetta attiva ("ricomincia da capo", m_sameTabRestart) lo
    // svuota con la texture che si spegne, o riaprendolo in Texture si
    // ritroverebbe lo script della texture spenta.
    // Testo in lavorazione per una fascia: e' della scena di prima.
    m_meshTextureScriptPart = -2;
    // L'editor segue gli slot (e' la loro vista), i tasti con lui.
    updateScriptButtonText();
    exitMetricScriptMode();
}

void MainWindow::resetTextureAndSoundForNewScene()
{
    // ==========================================================
    // GESTIONE STATI TEXTURE
    // ==========================================================
    m_blockTextureGen = false;
    // Anche l'IMMAGINE, dalla GPU: prima si azzeravano solo i flag che la
    // descrivevano, e dopo un NEW l'immagine restava nel sampler (trovato dal
    // test degli scenari). La texture e' spenta: il motore la segue subito sotto.
    // E il CODICE: restava compilato nel fragment quello della texture di prima.
    if (ui->glWidget) ui->glWidget->clearTexture();
    commitSurfaceTextureCode(QString());

    // La vista segue (il bersaglio e' gia' Surface: vedi sopra).
    refreshTextureCheckbox();

    // Intenzione spenta (scena di default): il motore la segue.
    applySurfaceTextureToEngine();
    // ==========================================================

    // ==========================================================
    // RESET AUDIO E COLORI
    // ==========================================================
    // 1. Ferma l'audio per QUALSIASI cambio tab
    if (m_audioController) {
        m_audioController->stopAll();
    }
    if (ui->btnRunCurrentScript && ui->btnRunCurrentScript->text() == "Stop Sound") {
        ui->btnRunCurrentScript->setText("Run Sound");
    }
}

void MainWindow::resetAppearanceForNewScene()
{
    // (Colore e colori 1/2 dello sfondo: scena di default, in testa.) Il verde
    // della superficie vive solo nel motore.
    const QColor defaultGreen = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    if (ui->glWidget) {
        ui->glWidget->setBackgroundColor(m_scene.bgColor);
        ui->glWidget->setColor(defaultGreen.redF(), defaultGreen.greenF(), defaultGreen.blueF());
        ui->glWidget->setGlobalTextureColors(defaultGreen, Qt::black);
        // Globali ora coerenti: si puo' lasciare la mesh senza esporre un
        // frame col colore stantio.
        ui->glWidget->setActiveMeshPart(-1);

        // AMBITO "ALL" SUBITO, non a fine giro. La superficie di destinazione
        // e' a mesh singola, quindi l'ambito DEVE finire su "All": ci arrivava
        // gia', ma tardi, per via di updateMeshScopeEnabled (~12981) che gira
        // da updateMeshSelectorRange sull'onda di meshPartsChanged, cioe' DOPO
        // che la sfera e' stata compilata e disegnata. setMeshAppearanceUniform
        // fa buildWireframeGeometry() + rebuildShader(), e rebuildShader
        // DISTRUGGE le pipeline: si pagava una SECONDA compilazione dello
        // shader a sfera gia' a schermo. Da qui il transitorio visibile solo
        // venendo da una superficie multi-mesh (da mesh singola l'ambito e'
        // gia' "All" e il setter esce subito per il early-return su ==).
        // Portandolo qui la ricompilazione avviene PRIMA del rebuildShader
        // del ramo Ray Marching, che quindi la assorbe: una sola compilazione.
        // La chiamata tardiva resta e diventa un no-op (stesso valore).
        ui->glWidget->setMeshAppearanceUniform(true);

        // INQUADRATURA 2D di texture e sfondo al default, come al load di una
        // superficie (resetVisuals). Non la azzerava nessuno: la scena nuova
        // teneva zoom, pan e rotazione del record di prima -- la scacchiera di
        // default riaccesa dopo Dynamic Mobius Band (zoom 9.36) si vedeva 2x2
        // invece che 8x8, e il Save li scriveva nella scena nuova. Dopo aver
        // lasciato la mesh: il buffer di lavoro della vista 2D e' di nuovo
        // quello della superficie.
        ui->glWidget->resetTextureFraming();
    }

    // Aggiorna gli slider colore della UI per allinearli ai valori appena resettati
    onColorTargetChanged();

    // La superficie di default (toro/sfera) e' opaca: la trasparenza della
    // superficie precedente non deve sopravvivere al cambio tab.
    resetTransparency();

    // Cambio tab -> superficie di default (sfera/toro): la densità wireframe torna al
    // default, come trasparenza e luminosità. (Il ripristino della densità SALVATA
    // avviene solo caricando un preset che la contiene, in applyCommonData.)
    if (ui->glWidget) ui->glWidget->resetWireframeDensity();

    // Anche la LUMINOSITA' (intensità luce direzionale) non deve sopravvivere
    // al cambio tab: senza questo reset la superficie di default eredita la
    // luminosità della superficie/preset precedente. Vale per il cambio tab
    // manuale E per quello forzato dal caricamento di una texture incompatibile
    // (che passa anch'esso da qui via setCurrentIndex). setValue da solo non
    // riemette valueChanged se il valore è già 100, quindi applichiamo anche
    // direttamente intensità e label.
    {
        bool oldLight = ui->lightSlider->blockSignals(true);
        ui->lightSlider->setValue(100);
        ui->lightSlider->blockSignals(oldLight);
        setFillLightUI(0.0f);   // come la luce principale: torna al default
        setBorderUI(GLWidget::BorderStyle());   // niente bordo, aspetto di default
        ui->lblValLight->setText("100 %");
        if (ui->glWidget) ui->glWidget->setLightIntensity(1.0f);
    }
}

void MainWindow::resetRotationsForNewScene()
{
    // ==========================================================
    // RESET ROTAZIONI (oggetto 3D + 4D) -- COMUNE AI DUE RAMI
    // ==========================================================
    // Le rotazioni non appartengono a una modalita': girano identiche in
    // parametrico e in ray marching (le 4D comprese, vedi il master button in
    // RM). Questo azzeramento pero' viveva DENTRO il solo ramo parametrico:
    // ripristinando la superficie di default IMPLICITA le velocita' del preset
    // precedente sopravvivevano nel motore, i campi del dock 3D restavano sui
    // vecchi valori e il tasto GO/STOP restava su "STOP" -- la sfera di default
    // appariva giusta, ma con lo stato di rotazione di cio' che era stato
    // appena buttato via.
    //
    // Ordine obbligato: PRIMA di entrare nei rami. Il ramo parametrico chiude
    // con onStopClicked(), che e' un toggle e con velocita' gia' a zero esce
    // subito (hasAnyRotationSpeed falso) limitandosi a riallineare i master
    // button: il comportamento di quel ramo non cambia.
    //
    // onStopClicked e' un toggle anche qui: si chiama solo se il moto e'
    // davvero in corso, altrimenti con le velocita' ancora vive lo FAREBBE
    // PARTIRE invece di fermarlo.
    if (ui->glWidget && ui->glWidget->isAnimating()) onStopClicked();

    // Velocita' reali del motore, non solo le etichette.
    if (ui->glWidget) {
        ui->glWidget->setNutationSpeed(0.0f);
        ui->glWidget->setPrecessionSpeed(0.0f);
        ui->glWidget->setSpinSpeed(0.0f);
        ui->glWidget->setOmegaSpeed(0.0f);
        ui->glWidget->setPhiSpeed(0.0f);
        ui->glWidget->setPsiSpeed(0.0f);
    }
    refreshRotationSpeedLabels();

    // Il tasto delle rotazioni torna a "GO": con le velocita' azzerate un
    // "STOP" residuo descriverebbe un moto che non esiste piu'. Lo stop
    // manuale del contesto precedente si dimentica, come gli altri clock al
    // caricamento di una nuova superficie: qui la scena e' nuova di zecca.
    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    m_userStoppedCameraMotion = false;

    // Animazione del tempo (t): stesso discorso: non e' stato di modalita'.
    if (ui->glWidget) {
        ui->glWidget->resetTime();
        ui->glWidget->setSurfaceAnimating(false);
    }
    if (m_btnStart) m_btnStart->setText("START");
}

void MainWindow::resetEngineToImplicit(bool loadDefaultSurface)
{
    // --- PASSAGGIO A IMPLICIT (RAY MARCHING) ---
    // (Campi RM, slot, Step Relax, Ray Steps e taglio x/y/z: scena di
    // default, assegnata in blocco piu' sopra. Qui motore e controlli.)

    // ILLUMINAZIONE AL DEFAULT (Basic, niente speculare, niente 4D).
    // Il ramo PARAMETRICO qui sotto lo faceva gia'; questo no, e il modello
    // restava quello della scena precedente: arrivando in Ray Marching da una
    // superficie in Phong, le due superfici di default (sfera del sotto-tab
    // 3D e T^3 del Cross Section) si presentavano in Phong -- ereditando un
    // aspetto che nessuno aveva scelto per loro. Bug preesistente al Cross
    // Section, che si limitava a raddoppiarne le occorrenze.
    //
    // Il motore va scritto ESPLICITAMENTE, non solo muovendo il radio:
    // setChecked(true) su un radio GIA' selezionato non emette toggled, e
    // l'handler onRenderRadioToggled -- che e' quello che riscrive il motore
    // -- non girerebbe. E' il caso "ero gia' in Basic ma con lo speculare
    // acceso da un preset".
    refreshRenderRadios();
    if (ui->glWidget) {
        ui->glWidget->setSpecularEnabled(false);   // spegne il Phong residuo
        ui->glWidget->set4DLighting(false);        // non usata in ray marching
        ui->glWidget->setLightingMode4D(0);
    }
    m_scene.lightingMode4D = 0;
    if (ui->btnLightMode) ui->btnLightMode->setText("Directional Lighting");

    // 2. Ferma il timer dell'animazione shader (rotazioni, tempo e path
    // camera sono già stati fermati nei blocchi comuni più sopra).
    ui->glWidget->stopAnimationTimer();

    // 4. Texture e rilievo fuori dal motore (la forma nuda)
    ui->glWidget->setTextureCode("");
    ui->glWidget->setDisplacementCode("");

    ui->glWidget->setBackgroundTextureEnabled(false);
    forgetBackgroundTexture();

    // (Lo slot della texture di SUPERFICIE e' vuoto nella scena di
    // default: restando pieno, l'editor -- che lo legge -- ripescava il
    // codice della texture spenta e il dock Script tornava sporco.)

    // (Il bersaglio e' gia' tornato su Surface nella parte comune, sopra.)

    // 5. L'equazione e' quella della scena di default: la sfera, o VUOTA
    // in entrambi i sotto-tab per NEW (un New "a meta'" lasciava scritta
    // l'equazione del Cross Section, al contrario del 3D e del parametrico).

    // ==========================================================
    // ADATTAMENTO SLIDER "S" IN "STEP RELAX" (MEMORIA SEPARATA)
    // ==========================================================
    ui->lblS->setText("Step Relax");
    // Memoria protetta: un valore non positivo congela i raggi (la scena
    // di default l'ha gia' letta cosi').
    if (m_lastImplicitS <= 0.0) m_lastImplicitS = 0.4;
    // Il valore e' nella scena; lo slider (range 0..1, allargato se serve)
    // lo segue.
    refreshConstantSliders();
    // ==========================================================

    // --- SETUP MODALITA' RAY MARCHING (Originale) ---
    ui->glWidget->setEngineMode(GLWidget::ModeImplicit);

    ui->lblSteps->setText("Ray Steps=");

    ui->glWidget->setRaySteps(m_scene.steps);

    // Taglio x/y/z vuoto (scena di default): non taglia la superficie
    if (ui->glWidget) {
        applySpaceLimits(/*notify=*/false);   // campi vuoti: nessun taglio

        // Reset completo della vista, come già fa il ramo Parametrico:
        // in particolare spegne m_isPathFollowing, che dopo un record
        // col path resterebbe acceso e la view continuerebbe il lookAt
        // su m_pathTarget/m_pathUp stantii -> la sfera di default
        // compariva spostata/inclinata. La distanza viene comunque
        // forzata a 4.0 subito sotto (il salvavita zoom di
        // resetTransformations da solo non basta).
        ui->glWidget->resetTransformations();

        // Distanza camera alla standard (4.0): la sfera di default ha
        // raggio 1 e va vista da qui. Senza questo reset la camera resta a
        // quella del RECORD RM caricato prima (resetTransformations PRESERVA
        // la distanza corrente se >= 2.5, "salvavita zoom"), quindi la sfera
        // appariva RIMPICCIOLITA e la sua dimensione dipendeva dal camera3D.z
        // del record (es. N-Tours z=11.12). Stato persistente: ne' Run ne'
        // cambi tab successivi la ripristinavano.
        ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
        ui->glWidget->setCameraYaw(0.0f);
        ui->glWidget->setCameraPitch(0.0f);
        ui->glWidget->setCameraRoll(0.0f);

        // FIX 3: Compilazione e invio della sfera alla GPU
        //
        // Scena vuota: in Ray Marching non esiste una mesh da azzerare (il
        // gate m_indexCount non governa questo ramo), quindi l'unico modo di
        // non far vedere NIENTE e' dare al marcher un campo che non venga mai
        // intersecato. Non si puo' passare "": createImplicitFragmentShader
        // ha un fallback (~4077) che sostituisce la stringa vuota proprio con
        // la sfera, e otterremmo l'opposto della scena vuota. Una costante
        // positiva e' sempre > epsilon a ogni passo -> nessun hit, sfondo
        // pulito, e lo shader compila (se non compilasse, il ripristino di
        // emergenza in validateAndApplyImplicitShader rimetterebbe a schermo
        // l'equazione PRECEDENTE).
        const QString implicitEqF = loadDefaultSurface
                                  ? QStringLiteral("(x^2 + y^2 + z^2) - (1.0)")
                                  : QLatin1String(kEmptyImplicitField);
        ui->glWidget->setImplicitEquation(implicitEqF);
        ui->glWidget->validateAndApplyImplicitShader(implicitEqF, "", "");

        // Shell/Solid tornano al default di avvio (Shell, vedi il setup a
        // ~1926). Senza questo il reset alla sfera di default ereditava lo
        // stile dell'ultimo preset/record RM caricato: i radio restavano
        // dov'erano e il motore pure. Stessa implementazione condivisa dei
        // due rami di load.
        applyImplicitShellMode(true);

        // Marcher: torna a Fast, il default di avvio (~2413). Stessa ragione
        // di Shell/Solid qui sopra -- senza, il reset ereditava il marcher
        // dell'ultimo preset caricato -- e stessa regola del tasto New:
        // azzerare tutto, nessuna eccezione.
        // setMarcherUI: motore e radio insieme, a segnali bloccati (il
        // toggled dei radio e' il clic dell'utente).
        setMarcherUI(false);

        ui->glWidget->rebuildShader();

        // Sotto-tab Cross Section: stessa idea, superficie di default
        // propria (T^3). Sovrascrive quanto appena fatto sopra per il
        // sotto-tab 3D SOLO se e' Cross Section quello attivo.
        const bool crossSectionActive = crossSectionTab();
        if (loadDefaultSurface && crossSectionActive) {
            loadCrossSectionDefaultSurface();
        }
        // SCENA VUOTA dal Cross Section: il campo vuoto va applicato al SUO
        // ramo. setImplicitEquation() qui sopra riporta il marcher al ramo
        // 3D (lo fa apposta, vedi la sua nota), e le due righe sopra
        // agiscono entrambe sul solo ramo 3D. Senza questo blocco il New
        // lasciava m_eqCrossSectionF INTATTO, con dentro l'equazione
        // precedente: a schermo la scena era vuota (coerente: il ramo vivo
        // era il 3D col campo vuoto), ma lo STATO era incoerente -- sotto-tab
        // Cross Section attivo e marcher sull'altro ramo.
        // Si autocorreggeva al primo Invio, che passa useCrossSection=true:
        // il guasto viveva nella finestra fra il New e quel commit, dove
        // chiunque LEGGA il ramo attivo (p.es. isSceneEmpty) trovava il 3D
        // mentre l'utente era nel Cross Section.
        else if (!loadDefaultSurface && crossSectionActive) {
            ui->glWidget->validateAndApplyImplicitShader(
                        QLatin1String(kEmptyImplicitField), "", "",
                        /*useCrossSection=*/true);
            ui->glWidget->rebuildShader();
        }
    }
}

void MainWindow::resetEngineToParametric(bool loadDefaultSurface)
{
    // --- PASSAGGIO A PARAMETRIC (TAB 0) ---
    // (Equazioni, dominio, s e Steps: scena di default, assegnata in blocco
    // piu' sopra. Qui motore e controlli.)

    // 1. RESET FISICO (lo stop delle rotazioni e del tempo e' gia' stato
    // fatto nel blocco comune ai due rami, qui sopra; i path camera nel
    // blocco RESET PATH CAMERA piu' su).
    ui->glWidget->resetTransformations();
    // Distanza camera alla standard (4.0): il salvavita zoom di
    // resetTransformations PRESERVA una distanza >= 2.5 (es. quella
    // della camera del path/record appena abbandonato) e il toro di
    // default apparirebbe da li'. Stesso riallineamento che il ramo
    // Ray Marching fa per la sfera di default.
    ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));

    // 2. RESET ILLUMINAZIONE E RENDER MODE (Fix Bug persistenza)
    // Riportiamo tutto al modello "Basic" (Lambert) senza specolarità
    refreshRenderRadios();
    ui->glWidget->setSpecularEnabled(false); // Spegne Phong residuo

    // Spegniamo categoricamente l'illuminazione 4D (non usata nel reset RM)
    ui->glWidget->set4DLighting(false);
    m_scene.lightingMode4D = 0;
    if (ui->btnLightMode) ui->btnLightMode->setText("Directional Lighting");
    ui->glWidget->setLightingMode4D(0);

    // 3. RESET SFONDO E LIMITI
    ui->glWidget->setBackgroundTextureEnabled(false);
    forgetBackgroundTexture();
    // (Bersaglio su Surface e checkbox Texture: gia' fatti nella parte
    // comune, sopra, a segnali bloccati.)

    // I limiti sono al default in ENTRAMBI i casi (anche NEW): sono il
    // dominio di lavoro, non la superficie. A campi vuoti servono comunque
    // sensati, perche' il primo Run dell'utente li legge cosi' come sono
    // (i limiti si applicano solo al Run, mai con Invio). Qui al motore.
    updateULimits(); updateVLimits(); updateWLimits();

    // 4. GEOMETRIA: il toro di default (il trifoglio sul sotto-tab Tubes), o
    // niente (NEW)
    if (loadDefaultSurface && m_scene.tubesTab) {
        // Il tubo di defaultScene, dal motore dei tubi: curva valida per
        // costruzione, costanti al default (A..F = 1, s = 0), dominio u di
        // default. Se non compilasse resterebbe la superficie di prima: lo dice
        // il log degli shader, che il test degli scenari legge.
        bool okLo = false, okHi = false;
        const float uMin = parseLimitField(m_scene.lim.uMin, &okLo);
        const float uMax = parseLimitField(m_scene.lim.uMax, &okHi);
        if (!okLo || !okHi || !applyTubeToEngine(uMin, uMax, CascadeConstants{1, 1, 1, 1, 1, 1, 0}))
            qWarning() << "resetScene: tubo di default non applicato:" << ui->glWidget->getShaderError();
    } else if (loadDefaultSurface) {
        ui->glWidget->setParametricEquations(m_scene.eq.x, m_scene.eq.y,
                                             m_scene.eq.z, m_scene.eq.p);
    } else {
        // SCENA VUOTA. Non basta passare equazioni vuote a
        // setParametricEquations: createVertexShaderSource le traduce in
        // "0.0" (~4440), quindi lo shader compila benissimo e la griglia
        // viene generata lo stesso -- tutti i vertici collassati
        // nell'origine, cioe' un blob al centro invece del nulla.
        // La mesh si svuota alla sorgente: clear() lascia vertici e indici
        // vuoti e il ramo "mesh vuota" di render() azzera m_indexCount.
        // Anche le equazioni del MOTORE, non solo i campi: restando quelle
        // della superficie precedente, il primo updateSurfaceData() di
        // chiunque (o un semplice cambio di risoluzione) la farebbe
        // RIAPPARIRE dal nulla.
        ui->glWidget->setParametricEquations("", "", "", "");
        ui->glWidget->clearSceneGeometry();
    }

    // 5. CONFIGURAZIONE ENGINE PARAMETRICO
    ui->lblS->setText("s=");
    // s = 0 (scena di default); lo slider (range -10..10) la segue.
    refreshConstantSliders();

    ui->glWidget->setEngineMode(GLWidget::ModeParametric);
    ui->lblSteps->setText("Steps=");

    ui->glWidget->setResolution(m_scene.steps);

    if (loadDefaultSurface) {
        ui->glWidget->updateSurfaceData();
        // Inclinazione di cortesia con cui il toro di default si presenta.
        ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    } else {
        // Niente updateSurfaceData(): chiama computeMesh(), che rigenererebbe
        // la griglia. Lo svuotamento si ripete QUI, in fondo al ramo: fra il
        // clear() piu' sopra e questo punto passano setEngineMode e
        // setResolution, e l'ultima parola sulla geometria deve restare a
        // "vuota" -- il ramo `loadDefaultSurface` simmetrico fa lo stesso con
        // updateSurfaceData().
        ui->glWidget->clearSceneGeometry();
    }

    onStopClicked();
}

void MainWindow::finishSceneReset()
{
    // PROIEZIONE al default (1 = prospettica, come il costruttore ~1911).
    // Non la tocca nessuno degli altri reset: `resetTransformations` azzera le
    // rotazioni 4D e la camera, ma projectionMode e' stato di RENDERING, non una
    // trasformazione. Senza questa riga, dopo un preset in ortogonale la
    // superficie di default restava ortogonale -- e in ortogonale project4D
    // scarta la quarta coordinata (`return p.xyz`, surface.vert ~135), quindi le
    // superfici 4D apparivano schiacciate senza motivo visibile.
    if (ui->glWidget) ui->glWidget->setProjectionMode(1);
    updateProjectionButtonText();

    // FOV al default (45°, come il costruttore ~2857). Nessun percorso di reset
    // lo riportava indietro: e' l'unico controllo di camera che sopravviveva sia
    // al cambio tab sia al tasto NEW, cosi' la superficie di default (o la scena
    // vuota) si presentava con l'inquadratura del preset appena abbandonato --
    // e su un preset con FOV stretto la differenza e' vistosa.
    // Vale per entrambi i rami: e' stato di CAMERA, come projectionMode qui
    // sopra e come il setCameraPos(0,0,4) dei due rami. applyCameraFov allinea
    // in un colpo stato (m_scene.fov), label, slider e motore.
    applyCameraFov(45.0f);

    // COSTANTI AL MOTORE. I campi A..F sono stati riportati al default a
    // segnali BLOCCATI (obbligatorio: siamo dentro un reset, e quei textChanged
    // ricompilerebbero e sporcherebbero la scena), quindi nessuno li ha spinti
    // nell'UBO: senza questa riga lo shader continuerebbe a marciare con le
    // costanti del preset appena scartato, e la UI mostrerebbe 1 mentre la
    // superficie ne usa altre.
    // QUI e non dentro il blocco di reset, che sta a monte dei due rami: le
    // superfici di default scrivono le PROPRIE costanti (il T^3 le sue tre) e
    // vanno lette dopo. resolveCascadeConstants legge i campi come sono ADESSO,
    // quindi manda al motore i valori giusti in tutti e tre i casi -- scena
    // vuota, sfera del 3D, T^3 del Cross Section.
    setEngineConstants(resolveCascadeConstants(false), /*onlyIfChanged=*/false);

    // La texture non c'e' piu': nemmeno modifiche a mano da proteggere quando
    // si spegne il checkbox.
    m_textureModified = false;

    updateRenderState();
    checkParametricDependency();
    ui->glWidget->update();
    updateScriptButtonText();

    // Il cambio tab ripristina e RENDERIZZA la superficie di default del tab di
    // destinazione (toro o sfera): non c'è nulla da applicare, quindi i Run
    // "one-shot" tornano DISABILITATI. La scrittura delle equazioni di default
    // qui sopra (es. lineEquation a riga ~1048, segnali non bloccati) aveva
    // azzerato i flag via textChanged: li riasseriamo e riallineamo i tasti.
    // Anche la texture parte azzerata (campi vuoti dopo il reset): il Run
    // texture sarà comunque disabilitato dal controllo campi-vuoti.
    //
    // SCENA VUOTA: i flag valgono lo stesso, ma per un'altra ragione. Qui non
    // c'e' "nulla da applicare" perche' non c'e' NIENTE, punto: i campi sono
    // vuoti e il Run resta spento finche' l'utente non scrive qualcosa -- e in
    // quel momento textChanged azzera il flag e il tasto si riaccende da solo,
    // che e' esattamente il comportamento voluto.
    m_parametricApplied = true;
    m_implicitApplied = true;
    m_rmTextureApplied = true;
    updateMasterButtonState();
    // Le rotazioni sono state azzerate (blocco comune ai due rami): il dock 4D
    // va riallineato qui, dopo che anche resetTransformations ha riportato a
    // zero gli angoli omega/phi/psi che questa funzione legge. Prima non
    // veniva chiamata affatto da nessun percorso di reset.
    update4DButtonState();

    // L'avviso in sovrimpressione ("Slider A attivo su questa forma") descrive
    // il preset che se ne e' appena andato: se il reset arriva prima che il suo
    // timer scada, resterebbe a schermo sopra la superficie di DEFAULT, dove non
    // significa piu' nulla. showSceneHint("") lo nasconde e azzera anche
    // m_currentHintText -- hideSceneHint() da solo lascerebbe il testo in
    // memoria, pronto a finire in un eventuale risalvataggio.
    // Anche quello della TEXTURE: ora i due si compongono a schermo
    // (composedHintText), quindi lasciarlo qui lo farebbe sopravvivere al reset
    // e riapparire sotto il messaggio del preset successivo. Stessa sorte per
    // quello dello SFONDO: il reset toglie anche lo sfondo.
    m_currentTextureHintText.clear();
    m_currentBgTextureHintText.clear();
    m_sceneHintFromSurfacePreset = false;
    showSceneHint(QString(), 0.0f);

    // Scena azzerata: non c'e' piu' lavoro da proteggere, e la situazione che
    // aveva fatto scattare l'avviso "un altro modulo e' carico" non esiste piu'.
    // (Il momento pulito lo segna SceneCleanGuard all'uscita.) La sorgente torna
    // al default: qui e nel load di un preset sono le due sole sedi che la
    // cambiano.
    m_warnedEditedDock = OriginDefault;
    m_warnedOrigin     = OriginDefault;
    m_surfaceOrigin    = OriginDefault;
}

void MainWindow::clearLibrarySelectionAfterReset()
{
    // A schermo c'e' ora la superficie di DEFAULT (toro/sfera) o il NULLA: in
    // nessuno dei due casi un item di libreria descrive cio' che si vede, e
    // lasciarne uno evidenziato indica una superficie che non c'e' piu'. Stessa
    // deselezione (e stessa motivazione) del load di una texture incompatibile,
    // ~5451, estesa a tutti e quattro i rami.
    // La deselezione deve togliere anche il CURRENT item, non solo la
    // selezione: il current sopravvive a clearSelection() e si ripresenta come
    // riga evidenziata appena il ramo viene riaperto.
    // Va fatto su tutti e quattro anche perche' refreshRepositories (~11197)
    // SALVA la selezione corrente e la RIPRISTINA dopo aver ricostruito gli
    // alberi: il file-system watcher puo' farlo scattare in qualsiasi momento,
    // e quello che qui resta selezionato tornerebbe fuori da solo.
    // Rami richiusi per la stessa ragione: le cartelle aperte descrivono
    // l'esplorazione che ha portato alla superficie appena scartata. Si fa QUI
    // e non nel gate della scena vuota, che gira di continuo e richiuderebbe le
    // cartelle sotto le dita dell'utente: il reset e' un evento singolo.
    // Stessa scelta gia' fatta per il ramo Texture in wireframe
    // (setTextureLibraryGrayed): si chiude entrando, non si riapre uscendo.
    // ECCEZIONE: il cambio tab AUTOMATICO arriva qui per la stessa strada
    // (setCurrentIndex -> applyModeTabReset), ma li' l'utente non sta scartando
    // niente: sta CARICANDO qualcosa che ha imposto il cambio di modalita' --
    // una texture incompatibile, oppure un record di modo opposto a quello a
    // schermo. Due differenze, entrambe rette da m_texModeSwitchInProgress:
    //  - i rami NON si richiudono, o l'albero gli si chiude sotto le dita;
    //  - il ramo DI PROVENIENZA non si deseleziona, perche' l'item evidenziato
    //    e' proprio quello che si sta caricando (questa funzione gira PRIMA che
    //    il preset venga applicato, quindi la selezione la si cancellerebbe e
    //    basta, senza che nessuno la rimetta).
    // Quale sia il ramo di provenienza lo dice l'albero che possiede l'item
    // cliccato: texture -> treeTextures, record -> treeMotions. Non si tiene
    // fermo l'intero set, perche' gli altri rami vanno deselezionati davvero.
    // Il treeSurfaces si deseleziona anche in questi casi: la superficie
    // precedente e' stata sostituita dalla default, quindi il suo item non
    // descrive piu' cio' che si vede.
    // Segnali bloccati: itemClicked ricaricherebbe il preset appena scartato.
    // Il ramo di provenienza lo dichiara chi ha alzato il flag (vedi
    // m_modeSwitchSourceTree). Dedurlo da m_lastLoadedLibraryItem era sbagliato
    // per le texture, che non lo aggiornano: restava il RECORD caricato prima,
    // e il suo item sopravviveva evidenziato sulla superficie di default.
    QTreeWidget *keepFocus = m_texModeSwitchInProgress ? m_modeSwitchSourceTree
                                                       : nullptr;

    for (QTreeWidget *tree : { ui->treeSurfaces, ui->treeTextures,
                               ui->treeMotions,  ui->treeSounds }) {
        if (!tree) continue;
        if (m_texModeSwitchInProgress && tree == ui->treeTextures) continue;
        if (tree == keepFocus && tree != ui->treeSurfaces) continue;
        bool b = tree->blockSignals(true);
        tree->clearSelection();
        tree->setCurrentItem(nullptr);
        if (!m_texModeSwitchInProgress) tree->collapseAll();
        tree->blockSignals(b);
    }
}

void MainWindow::resetScene(int index, bool loadDefaultSurface)
{
    // All'uscita la scena e' pulita: niente lavoro da proteggere.
    SceneCleanGuard sceneCleanGuard{this};
    discardPendingLimitEdits();

    // NEW, cambio tab, riclic sulla linguetta: la scena non e' piu' il record
    // che era caricato (vedi m_currentRecordPath).
    m_currentRecordPath.clear();
    // ...e le modifiche in sospeso sono scartate con lei (vedi m_constantsEditPending).
    m_constantsEditPending = false;

    // RESET IN CORSO (m_populatingFields, vedi mainwindow.h). RIPRISTINO del
    // valore precedente, non "false": resetScene puo' girare ANNIDATA dentro un
    // caricamento (applyMotionExample forza la linguetta quando il record e' di
    // modo opposto a quello a schermo), e azzerarlo di netto lo spegnerebbe
    // anche per il resto del load.
    const bool wasPopulating = m_populatingFields;
    m_populatingFields = true;
    struct ResetGuard {
        MainWindow *w;
        bool prev;
        ~ResetGuard() { w->m_populatingFields = prev; }
    } resetGuard{this, wasPopulating};

    // LA SCENA DEL RESET (defaultScene), calcolata sulla scena che si lascia:
    // al cambio di linguetta alcune parti sopravvivono. Si assegna piu' sotto,
    // in blocco, a moti fermati.
    const SceneState def = defaultScene(index, loadDefaultSurface, m_sameTabRestart);
    // MEMORIE PER MODALITA': gli steps (e in Ray Marching lo Step Relax) del
    // modo che si LASCIA, ritrovati al ritorno. Solo se lo si lascia davvero
    // (il motore e' ancora sul modo di prima): un riclic, un NEW o il reset del
    // sotto-tab restano nello stesso modo. Prima la memoria dell'altro modo si
    // riscriveva anche li', coi valori di quello corrente: un riclic in Ray
    // Marching dava al parametrico 400 di Steps, uno in parametrico al Ray
    // Marching 100 Ray Steps.
    {
        const bool wasImplicit = ui->glWidget
                                 && ui->glWidget->getEngineMode() == GLWidget::ModeImplicit;
        if (index == 1 && !wasImplicit) {
            m_lastParametricSteps = m_scene.steps;
        } else if (index == 0 && wasImplicit) {
            m_lastImplicitSteps = m_scene.steps;
            m_lastImplicitS = m_scene.constants.s.toDouble();
        }
    }

    stopSceneForReset();

    // LA SCENA DI DEFAULT, in blocco (assignSceneTexts, la stessa strada del
    // load): equazioni, campi RM, costanti, slot, ancore, limiti e path. Le
    // ancore azzerate sono il legame con la libreria della texture e dello
    // sfondo tolti: dopo un NEW uno script scritto a mano si ritrovava in
    // Library il focus sulla texture del record di prima (il nome vince sul
    // codice). I path vuoti spengono i Departure (showLineFields). Il resto
    // della funzione porta motore e controlli su questa scena.
    assignSceneTexts(def);
    setSteps(def.steps);

    // Stato di sessione dei path azzerato, come al load di un record
    // (vedi applyMotionExample): un futuro Departure riparte da t=0 e
    // da orientamento neutro.
    m_paths->resetSession();
    // ...e i comandi dei path (vista, velocita', ultimo moto, path compilato).
    resetMotionControls();

    resetScriptEngineForNewScene();
    // ==========================================================

    resetTextureAndSoundForNewScene();

    // 2. Resetta i colori al default per evitare "sanguinamenti" dai record precedenti
    //
    // I reset che seguono (colore, e piu' sopra alpha/luce/wireframe) sono lo
    // stato della SUPERFICIE DI DEFAULT, non un comando dell'utente su una
    // parte: vanno scritti sullo stato globale. Senza il bypass, con una mesh
    // ancora selezionata dal preset multi-mesh precedente (es. "Hopf Tori Mesh
    // Colors"), setColor passa da applyToActiveMeshPart e il verde finisce
    // DENTRO la MeshPart invece che nei membri globali: la sfera di default
    // lampeggiava verde e tornava subito al colore del preset (giallo).
    // NB: clearMeshParts() piu' sopra svuota solo m_declaredParts; m_meshParts
    // (cio' che mutableMeshPart legge) sopravvive, quindi la parte attiva e'
    // ancora valida qui. Stesso schema del load superficie (~8654).
    // Il BYPASS si accende PRIMA di lasciare la mesh, ma la selezione si
    // molla DOPO aver riscritto i colori globali (piu' sotto). Ordine
    // obbligato: mentre la superficie multi-mesh e' a schermo ogni parte
    // disegna dal proprio MeshPart e i membri globali red/green/blue non
    // vengono mai esercitati, quindi restano su un valore stantio (il loro
    // inizializzatore e' bianco, glwidget.h:809). setActiveMeshPart chiama
    // update(): mollando la selezione per prima, il repaint che ne segue
    // trova la sfera di default -- che e' a mesh singola e disegna dai
    // globali -- ancora BIANCA, ed e' il lampo bianco al caricamento.
    // Col bypass acceso setColor scrive gia' sui globali anche a mesh
    // attiva, quindi si puo' sistemare il colore e solo allora deselezionare.
    // RIPRISTINO del valore precedente, non "false": resetScene gira anche
    // ANNIDATA in un load (un record di modo opposto a quello a schermo fa
    // cambiare linguetta), e spegnerlo di netto toglieva il bypass al resto del
    // load -- l'alpha globale del record finiva nella fascia 0 gia' creata
    // (Villarceau Tubes Drift dopo un record Ray Marching, round-trip).
    const bool prevBypass = ui->glWidget && ui->glWidget->meshAppearanceBypass();
    if (ui->glWidget) ui->glWidget->setMeshAppearanceBypass(true);
    struct MeshBypassGuard {
        MainWindow *w;
        bool prev;
        ~MeshBypassGuard() { if (w->ui->glWidget) w->ui->glWidget->setMeshAppearanceBypass(prev); }
    } meshBypassGuard{this, prevBypass};

    resetAppearanceForNewScene();
    // ==========================================================

    resetRotationsForNewScene();

    // COSTANTI: A..F al default (1), nessun dominio dello script e la S del
    // modo sono gia' nella scena di default, assegnata piu' sopra. Erano
    // l'ultimo stato della scena precedente a sopravvivere a cambio tab e NEW
    // (in Ray Marching il giudizio "non usata" a equazione vuota non le
    // riscrive, apposta), e i domini ("A := int(2,6)") dello script scartato
    // continuavano a far scattare gli slider. La superficie di default del
    // Cross Section ha costanti PROPRIE (il T^3 degenera senza A > B > C): le
    // scrive loadCrossSectionDefaultSurface, nel ramo implicito.

    if (index == 1) resetEngineToImplicit(loadDefaultSurface);
    else            resetEngineToParametric(loadDefaultSurface);

    finishSceneReset();

    clearLibrarySelectionAfterReset();

    // Nuovo zero dei campi del dock 4D. In coda a TUTTO: questa funzione serve
    // il cambio tab, il riclic sulla linguetta e il tasto NEW, e in ognuno dei
    // tre la scena e' appena diventata un'altra -- compresa la superficie di
    // default del Cross Section, che nasce con una sua inquadratura
    // (omega=0.5, P=0.015) che va presa come riferimento, non mostrata.
    resetNav4DBaseline();

    // SNAPSHOT DELL'APPLICATO: la scena e' la default del tab (o vuota, col
    // tasto NEW). Lo snapshot dell'ultimo Run restava quello della scena
    // scartata, e l'Invio su una costante lo avrebbe riapplicato.
    snapshotActiveEquations();
}


// ==========================================================
// UI & STATE MANAGEMENT
// ==========================================================

void MainWindow::switchToMainMode()
{
    updateLayoutForMode(0);
}

// Tasto NEW: scena vuota. Riusa il percorso di reset gia' in produzione
// (resetScene) con loadDefaultSurface=false, quindi la pulizia e' per
// costruzione la stessa del cambio tab: nessuna seconda copia da tenere
// allineata. Resta sul tab corrente -- New svuota la scena, non cambia
// modalita' -- e passa dalla conferma come il riclic sulla linguetta attiva.
void MainWindow::onNewSceneClicked()
{
    if (!confirmDiscardUnsaved(ScopeScene)) return;
    resetScene(implicitMode() ? 1 : 0, /*loadDefaultSurface=*/false);
    // Scena vuota: niente parti, l'ambito torna ad "All" (restava "Mesh" dopo
    // un multi-mesh, perche' una scena vuota non emette meshPartsChanged).
    updateMeshScopeEnabled();
}

// Avviso delle costanti condivise al caricamento di un RECORD.
//
// PERCHE' SERVE: il controllo esistente (confirmTextureConstantClash) scatta
// quando si carica una TEXTURE in una scena gia' composta -- dalla libreria o
// col Sync. Un record porta superficie, texture e sfondo gia' combinati, e
// nessuno confrontava le tre fonti fra loro: SL(2,R)/NewMotion usa F sia nella
// texture sia nello sfondo, e si caricava senza una parola.
//
// INFORMATIVO, non una domanda: il record e' gia' salvato cosi', e la
// condivisione puo' essere voluta -- i tre Kerr legano A e B alla forma e alla
// texture, e lo dicono nel loro hint. Per questo "Don't show again": si ricorda
// per QUEL file e per QUELLE lettere (la firma), cosi' se il record cambia e
// condivide altro l'avviso torna.
//
// IN CODA ALL'EVENTO: un exec() modale durante il caricamento rientrerebbe nel
// ciclo degli eventi a scena mezza caricata (e' la trappola del record che
// prendeva la camera di un altro). Qui la scena e' completa.
void MainWindow::warnSharedConstantsOnRecordLoad(const QString &recordPath)
{
    QMetaObject::invokeMethod(this, [this, recordPath]() {
        // Parti ATTIVE soltanto: una texture o uno sfondo spenti non muovono
        // nulla, anche se il loro codice cita una costante.
        const bool isRM = (implicitMode());
        QList<QPair<QString, QSet<QString>>> parts;
        parts << qMakePair(QStringLiteral("the surface"), constantsUsedIn(surfaceConstantSource()));
        if (m_scene.surfaceTextureState) {
            const QString tex = isRM
                ? (ui->lineTexture ? m_scene.rm.texture : QString()) + "\n"
                  + (ui->lineVariations ? m_scene.rm.displacement : QString())
                : m_scene.surfaceTextureCode;
            parts << qMakePair(QStringLiteral("the texture"), constantsUsedIn(tex));
        }
        if (ui->glWidget && ui->glWidget->isBackgroundTextureEnabled())
            parts << qMakePair(QStringLiteral("the background"), constantsUsedIn(m_scene.bgTextureCode));

        QStringList lines, signature, free;
        for (const QChar c : QStringLiteral("ABCDEF")) {
            const QString L(c);
            QStringList owners;
            for (const auto &p : parts) if (p.second.contains(L)) owners << p.first;
            if (owners.isEmpty()) { free << L; continue; }
            if (owners.size() < 2) continue;
            const QString who = owners.size() == 2
                ? "both " + owners[0] + " and " + owners[1]
                : owners.mid(0, owners.size() - 1).join(", ") + " and " + owners.last();
            lines << QString("%1 is used by %2.").arg(L, who);
            signature << L + ":" + owners.join("+");
        }
        if (lines.isEmpty()) return;

        // Chiave per file: il percorso ha delle '/', che QSettings leggerebbe come
        // gruppi annidati. Se ne usa l'hash.
        const QString fileKey = QString::fromLatin1(QCryptographicHash::hash(
            QFileInfo(recordPath).absoluteFilePath().toUtf8(), QCryptographicHash::Md5).toHex());
        const QString sig = signature.join(";");
        QSettings settings;
        const QString settingKey = "sharedConstantsNotice/" + fileKey;
        if (settings.value(settingKey).toString() == sig) return;

        if (InputValidator::showSharedConstantsNotice(this, lines, free))
            settings.setValue(settingKey, sig);
    }, Qt::QueuedConnection);
}

void MainWindow::applySurfaceExample(LibraryItem d)
{
    // All'uscita la scena e' quella del file: e' il momento pulito.
    SceneCleanGuard sceneCleanGuard{this};
    discardPendingLimitEdits();

    // LA SCENA DEL FILE, calcolata una volta: applyCommonData la assegna in
    // testa (una superficie riparte senza texture, sfondo, suono e ancore).
    const SceneState file = sceneFromItem(d, /*isRecord=*/false);

    // La scena non e' piu' un record: caricando una SUPERFICIE l'ancora del
    // record caricato non vale piu' (vedi m_currentRecordPath).
    m_currentRecordPath.clear();
    // Ancora e messaggio dello SFONDO: li toglie forgetBackgroundTexture, piu'
    // sotto, insieme a tutto lo sfondo che questo caricamento spegne.

    // ASPETTO PER-MESH DURANTE IL LOAD.
    // Per tutta la durata del caricamento i setter globali (colore, alpha, luce,
    // renderMode del preset) NON devono essere dirottati sulla mesh selezionata:
    // sono lo stato della superficie, non una scelta dell'utente su una parte.
    // Senza questo, con una mesh ancora attiva dalla sessione precedente quella
    // parte si prendeva i valori globali come propri (tornava verde e solida) e
    // il renderMode finiva per propagarsi a tutte le mesh che ereditano.
    // La selezione viene azzerata qui e reimpostata a 1 da
    // updateMeshSelectorRange quando le parti della nuova superficie esistono.
    // Alla fine il valore PRECEDENTE (vedi la guardia gemella in resetScene).
    const bool prevBypass = ui->glWidget && ui->glWidget->meshAppearanceBypass();
    if (ui->glWidget) {
        ui->glWidget->setMeshAppearanceBypass(true);
        ui->glWidget->setActiveMeshPart(-1);
    }
    struct MeshBypassGuard {
        MainWindow *w;
        bool prev;
        ~MeshBypassGuard() { if (w->ui->glWidget) w->ui->glWidget->setMeshAppearanceBypass(prev); }
    } meshBypassGuard{this, prevBypass};


    InputValidator::resetGeodesicWarning();

    // Nuova superficie caricata: dimentica gli stop manuali dei clock del
    // contesto precedente (stesso criterio di m_userStoppedSound in
    // applyMotionExample), altrimenti il preset partirebbe congelato.
    m_userStoppedGeomClock = false;
    m_userStoppedTexClock = false;
    m_userStoppedBgClock = false;
    m_userStoppedMeshTexClock = false;

    // 1. Pulizia totale
    ui->glWidget->pauseMotion();
    ui->glWidget->resetTransformations();
    ui->glWidget->resetVisuals();

    m_audioController->stopAll();

    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    if (m_btnStart) m_btnStart->setText("START");

    if (ui->glWidget) {
        ui->glWidget->setNutationSpeed(0.0f);
        ui->glWidget->setPrecessionSpeed(0.0f);
        ui->glWidget->setSpinSpeed(0.0f);
        ui->glWidget->setOmegaSpeed(0.0f);
        ui->glWidget->setPhiSpeed(0.0f);
        ui->glWidget->setPsiSpeed(0.0f);
    }
    refreshRotationSpeedLabels();

    // ==========================================================
    // AZZERAMENTO TOTALE STATO TEXTURE, TESTI E COLORI
    // ==========================================================
    m_blockTextureGen = false;

    // Il CODICE custom esce dal motore, che lo teneva compilato nel fragment a
    // texture spenta (l'immagine se ne va dalla GPU piu' sotto, con
    // clearTexture). I testi -- slot, accensione, ancore, suono -- li assegna
    // applyCommonData in testa.
    commitSurfaceTextureCode(QString());
    if(ui->glWidget) ui->glWidget->setDisplacementCode("");
    if(ui->glWidget) ui->glWidget->setTextureCode("");

    // Lo sfondo si spegne piu' sotto (setBackgroundTextureEnabled(false)): qui
    // se ne va tutto il resto, percorso dell'immagine e GPU compresi.
    forgetBackgroundTexture();

    // Prima dell'uscita dalla modalita' metrica, che decide la sorgente della
    // superficie guardando lo script applicato.
    clearSurfaceScript();
    exitMetricScriptMode();

    // Colori al default nel MOTORE (superficie verde); lo sfondo e' quello
    // della scena del file -- il colore salvato con la Shape, o il grigio
    // scuro di default --, che la testa di applyCommonData assegna allo stato.
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;
    if (ui->glWidget) {
        ui->glWidget->setColor(defR, defG, defB);
        ui->glWidget->setBackgroundColor(file.bgColor);
        ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(defR, defG, defB), Qt::black);
    }

    // Alpha e luce del preset NON qui: vedi dopo updateRenderState() piu' sotto.
    // Luce di riempimento: preset senza la chiave -> 0 (il parser mette gia'
    // quel default), cioe' l'illuminazione con cui sono stati salvati.
    setFillLightUI(d.fillLight);
    // Bordo: preset senza la chiave -> nessun bordo (default del parser).
    setBorderUI(borderStyleOf(d));

    onColorTargetChanged();

    // 2. Checkbox UI: segue l'intenzione, spenta al punto 1 (a segnali bloccati).
    refreshTextureCheckbox();

    // 3. Disabilita UI correlata (Slider colori texture, ecc.)
    updateTextureUIState(false);

    // 4. Reset Engine Grafico (Spegne tutte le texture)
    if (ui->glWidget) {
        applySurfaceTextureToEngine();   // intenzione spenta al punto 1 qui sopra
        ui->glWidget->setBackgroundTextureEnabled(false);

        // Scarico effettivo della texture di superficie dalla GPU. Senza questo,
        // m_surfaceTexture caricata sulla superficie PRECEDENTE resta residente e,
        // riaccendendo il checkbox sulla nuova superficie, ricompare stantia (senza
        // animazione / coi colori falsati). Allinea il cambio-superficie allo
        // spegnimento del checkbox, che gia' chiama clearTexture() (clearTextureMemory).
        ui->glWidget->clearTexture();

        // CRUCIALE: Ricostruisce lo shader standard (Phong/Basic)
        ui->glWidget->rebuildShader();
    }

    updateRenderState();

    // ALPHA E LUCE DEL PRESET DOPO la chiamata qui sopra, non prima. A questo
    // punto m_scene.renderMode e' ancora quello del preset PRECEDENTE (lo scrive
    // applyCommonData, piu' sotto): se quello era in wireframe, il reset
    // "wireframe -> opaco, luce 100" di updateRenderState cancellava i valori
    // appena impostati di un preset che in wireframe non e'. Klein Quadric
    // Ruled Lines (luce 0.97) si apriva a 1.00 dopo Hopf Tori Mesh Colors e a
    // 0.97 dopo Octahedron Bands (trovato dal test di andata e ritorno).
    // I preset davvero in wireframe restano normalizzati dalle chiamate
    // successive, che vedono il mode giusto.
    applyPresetAlpha(d.alpha);
    ui->lightSlider->setValue(qRound(d.lightIntensity * 100.0f));

    // SOTTO-TAB IMPLICITO E SUA EQUAZIONE: ripristinati PRIMA di applyCommonData,
    // non dopo (dove sta il resto del ramo implicito, piu' sotto). Ordine
    // OBBLIGATORIO: applyCommonData chiude con checkParametricDependency() ->
    // updateConstantsUIState(), che giudica quali costanti sono "usate" leggendo
    // l'equazione del sotto-tab ATTIVO (activeImplicitEquationText). Il ramo
    // "non usata" non si limita a bloccare la costante: le SCRIVE 1 (S a 0).
    // Spostando la linguetta dopo, quel giudizio vedeva ancora l'editor 3D, le
    // A/B/C del T^3 risultavano assenti e tornavano a 1 -> superficie deformata.
    // Stessa firma dei bug gia' noti su questo punto (costanti resettate da
    // preset metrico, costanti del path): chi riempie campi che contano come
    // "uso" deve farlo PRIMA del giudizio finale.
    // Il COMMIT al motore resta piu' sotto, insieme al resto del ramo implicito:
    // qui si prepara solo cio' che il giudizio sulle costanti deve poter leggere.
    // La regola e' choicesFromItem; setCrossSectionTab muove la linguetta a
    // segnali bloccati (currentChanged farebbe applyImplicitSubTabReset, cioe'
    // la superficie di DEFAULT del sotto-tab). Anche il ritorno al 3D va fatto
    // qui: una superficie del ramo 3D caricata dal Cross Section farebbe
    // leggere al giudizio l'equazione 4D rimasta a schermo.
    const QString csEqPre = d.crossSectionEq.trimmed();
    const bool loadCrossSection = file.crossSectionTab;
    if (d.isImplicitMode) setCrossSectionTab(loadCrossSection);

    // 5. CARICAMENTO DATI (Equazioni, Colori, ecc.)
    applyCommonData(d, file);

    // Ripristina il COLORE SUPERFICIE del preset. Sopra (riga ~5716) abbiamo
    // resettato al verde di default; senza questo blocco il colore salvato non
    // verrebbe mai riapplicato e ogni superficie caricata resterebbe verde
    // (come faceva gia' applyMotionExample). Il parser ora popola d.color1 sia
    // dal formato "surfColor" sia da r/g/b numerici (vedi librarymanager).
    if (d.hasCustomColors && !d.color1.isEmpty()) {
        QColor surfCol(d.color1);
        if (surfCol.isValid()) {
            if (ui->glWidget) {
                // BYPASS OBBLIGATORIO: questo e' il colore GLOBALE del preset,
                // non una scelta dell'utente su una fascia. Senza, setColor
                // passa da applyToActiveMeshPart e, con una mesh gia'
                // selezionata, lo scrive DENTRO quella parte: la fascia 1 si
                // ritrovava come colore proprio il verde globale, e da li' in
                // poi sia il render sia gli slider mostravano quello.
                // Il MeshBypassGuard di inizio funzione dovrebbe bastare (la
                // parte attiva torna a 0 dentro applyCommonData, qui sopra);
                // finche' la guardia di resetScene, annidata nei cambi di modo,
                // lo spegneva di netto, non bastava. La protezione esplicita
                // attorno a questa riga resta: non dipende da chi gira prima.
                const bool oldBypass = ui->glWidget->meshAppearanceBypass();
                ui->glWidget->setMeshAppearanceBypass(true);
                ui->glWidget->setColor(surfCol.redF(), surfCol.greenF(), surfCol.blueF());
                ui->glWidget->setMeshAppearanceBypass(oldBypass);
            }
            // GLI SLIDER SI ALLINEANO SOLO SE NON C'E' UNA MESH SELEZIONATA.
            // Qui si ripristina il colore GLOBALE della superficie, e
            // onColorTargetChanged lo riversa sugli slider leggendolo dal
            // motore -- senza mai guardare la mesh attiva.
            // Ma applyCommonData(), appena sopra, ha gia' fatto girare il sync
            // per-mesh, che aveva messo gli slider sul colore della fascia
            // selezionata: questa chiamata arriva DOPO e lo sovrascriveva col
            // globale. Era il disallineamento visibile gia' al primo
            // caricamento (fascia 1 blu a schermo, slider sul verde globale).
            // Il colore globale resta comunque impostato qui sopra: cambia solo
            // CHI viene mostrato dagli slider, che devono dire la stessa cosa
            // del render per la mesh che si sta guardando.
            const bool showingMesh = ui->glWidget && ui->glWidget->activeMeshPart() >= 0;
            if (showingMesh) syncAppearanceControlsToActiveMesh();
            else             onColorTargetChanged();
        }
    }

    // Le superfici implicite da script sono già state configurate da applyCommonData:
    // sovrascrivere lineEquation/setImplicitEquation qui ripristinerebbe la sfera di default.
    bool isImplicitScript = d.isImplicitMode && !d.scriptCode.isEmpty();

    if (d.isImplicitMode && !isImplicitScript) {
        // Il testo e' gia' nel campo (applyCommonData): qui il commit al motore.
        const QString eqToLoad = safeImplicitEquation(file.rm.equation);

        // Editor 4D e linguetta sono gia' stati ripristinati PRIMA di
        // applyCommonData (vedi la nota li': il giudizio sulle costanti in coda
        // ad applyCommonData deve poterli leggere, o resetta a 1 le costanti del
        // T^3). Qui resta il COMMIT al motore, che usa csEqPre/loadCrossSection
        // calcolati la'.
        const QString &csEq = csEqPre;

        if (ui->glWidget) {
            if (loadCrossSection) {
                // Il ramo Cross Section NON passa da setImplicitEquation (che e'
                // per definizione il ramo 3D e riporta il motore la'): serve il
                // commit completo, che e' l'unico a impostare m_eqCrossSectionF e
                // il flag di ramo. Forma "f - 0.0" come gli altri call site.
                QString csF = csEq.contains("=")
                    ? QString("(%1) - (%2)").arg(csEq.section('=', 0, 0).trimmed(),
                                                 csEq.section('=', 1).trimmed())
                    : QString("(%1) - (0.0)").arg(csEq);
                if (ui->glWidget->validateAndApplyImplicitShader(
                        csF, ui->glWidget->currentTextureCode(),
                        ui->glWidget->currentDisplacementCode(), /*useCrossSection=*/true)) {
                    ui->glWidget->rebuildShader();
                } else {
                    // L'equazione 4D del file non compila (record manomesso, o
                    // salvato da una versione con una sintassi diversa): il
                    // commit ha gia' ripristinato da se' lo stato precedente.
                    // Ripieghiamo sul ramo 3D invece di lasciare la linguetta su
                    // Cross Section con un'equazione che il motore non ha preso.
                    setCrossSectionTab(false);
                    ui->glWidget->setImplicitEquation(eqToLoad);
                }
            } else {
                ui->glWidget->setImplicitEquation(eqToLoad);
            }
            // La linguetta e' stata mossa con blockSignals, quindi
            // applyImplicitSubTabReset non ha girato (ed e' voluto: resetterebbe
            // la scena). Ma updateRenderState vive di li' per il ramo implicito:
            // senza questa chiamata le rotazioni 4D restavano spente su una
            // superficie Cross Section caricata, che invece le usa.
            updateRenderState();
            // L'equazione e' appena stata committata: ora isImplicitIllConditioned()
            // riflette il campo nuovo. updateRenderState() sopra ha girato PRIMA di
            // questo commit, quindi risincronizziamo lo slider qui.
            syncImplicitAlphaSlider(true, true);
        }
    }
    else if (isImplicitScript && ui->glWidget) {
        // Script implicito (es. Gyroid1): applyCommonData sopra ha gia' azzerato
        // m_implicitIllConditioned e (su Android) armato l'avviso trasparenza.
        // Risincronizziamo lo slider per la nuova superficie: newSurface=true lo
        // riabilita (non fu bloccato da un Chain precedente) e riarma la guardia del
        // popup di avviso.
        // Il ramo del motore torna al 3D: una superficie da script non e' mai
        // una sezione, e dopo un Cross Section il flag restava acceso.
        ui->glWidget->useImplicit3DBranch();
        syncImplicitAlphaSlider(true, true);
    }

    // Leggiamo i dati esatti salvati nel JSON per la posa statica
    float startOmega = d.startOmega;
    float startPhi   = d.startPhi;
    float startPsi   = d.startPsi;

    // Controllo Anti-Glitch per il 4D
    bool isFlat4D = (qFuzzyIsNull(startOmega) && qFuzzyIsNull(startPhi) && qFuzzyIsNull(startPsi));
    // La quarta coordinata: quella della curva, se la scena e' un tubo.
    QString wText = (d.isTube ? d.tubeP : d.w).trimmed();
    bool isSurface4D = !wText.isEmpty() && wText != "0" && wText != "0.0";

    if (isFlat4D && isSurface4D) {
        float smartOffset = 0.01f;
        // Applichiamo la minuscola rotazione solo al motore matematico
        startOmega = smartOffset; startPhi = smartOffset; startPsi = smartOffset;
    }

    // UNICO E DEFINITIVO invio alla GPU per la posizione della telecamera!
    ui->glWidget->setRotation4D(startOmega, startPhi, startPsi);

    // TRASLAZIONE DEL PIANO DI SEZIONE lungo p: l'altra meta' dello stato 4D del
    // Cross Section, insieme ai tre angoli qui sopra. Va impostata SEMPRE (anche
    // a 0) e non solo quando il preset la porta: e' stato appiccicoso del motore,
    // e senza l'azzeramento esplicito una superficie caricata dopo averne
    // sezionata un'altra ereditava la p della precedente.
    if (ui->glWidget) ui->glWidget->setCrossSectionP(d.isImplicitMode ? d.crossSectionP : 0.0f);

    refreshRotationSpeedLabels();

    // 6. Eseguiamo onStartClicked per inizializzare equazioni
    // Le superfici implicite da script non hanno equazioni valide nei campi standard.
    bool hasValidEquations = (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0") || (d.isImplicitMode && !isImplicitScript);

    // Inferiamo che è uno script se c'è codice e le equazioni sono vuote, o se è uno script implicito!
    bool isScript = d.isScript || isImplicitScript || (!d.scriptCode.isEmpty() && !hasValidEquations);

    if (!isScript) {
        runScene(RunOrigin::Load);
    } else {
        applyAnimationState(hasTimeVariable(m_scene.surfaceScriptApplied));
    }

    // 7. Recuperiamo i dati di illuminazione dal file
    int modeToApply = (d.lightingMode != -1) ? d.lightingMode : 0;

    bool want4D = d.hasLightingState ? d.use4DLighting : isSurface4D;

    // 8. Applichiamo le impostazioni ALLA VARIABILE MEMBRO
    m_scene.lightingMode4D = modeToApply;

    // 9. Applichiamo le impostazioni AL WIDGET GL
    if (ui->glWidget) {
        ui->glWidget->set4DLighting(want4D);

        ui->glWidget->setLightingMode4D(modeToApply);

        ui->glWidget->update();
    }

    // 10. Aggiorna il testo del bottone UI
    QString btnText;
    switch(modeToApply) {
    case 0: btnText = "Directional Lighting"; break;
    case 1: btnText = "Observer Lighting"; break;
    case 2: btnText = "Slice Lighting"; break;
    default: btnText = "Directional Lighting"; break;
    }
    if (ui->btnLightMode) ui->btnLightMode->setText(btnText);

    update4DButtonState();

    // 11. Finalizzazione
    ui->glWidget->setProjectionMode(d.projectionMode);
    // FOV UNICO: 45 e' il default, ma un preset che ne salva uno diverso lo
    // mantiene. La chiave letta e' `cameraFov` (default 45 in librarymanager);
    // i file piu' vecchi che salvavano solo i FOV per-path ricadono su fov3D
    // (a sua volta inizializzato da cameraFov in fase di parse), cosi' nessun
    // preset esistente perde la propria inquadratura.
    applyCameraFov(resolveSavedFov(d.cameraFov, d.fov3D, d.fov4D));
    updateProjectionButtonText();

    // Ripristino intelligente della Telecamera / Rotazione 3D
    if (!d.hasCamera3D) {
        // Preset vecchi (senza telecamera salvata): visuale standard inclinata
        ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
        ui->glWidget->setRotationQuat(QQuaternion());
        ui->glWidget->setCameraYaw(0.0f);
        ui->glWidget->setCameraPitch(0.0f);
        ui->glWidget->setCameraRoll(0.0f);
        ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    } else {
        // Preset nuovi: ripristina SOLO l'inquadratura esatta! (Senza doppioni)
        ui->glWidget->setCameraPos(QVector3D(d.camX, d.camY, d.camZ));
        ui->glWidget->setRotationQuat(QQuaternion(d.rotW, d.rotX, d.rotY, d.rotZ));
        ui->glWidget->setCameraYaw(d.camYaw);
        ui->glWidget->setCameraPitch(d.camPitch);
        ui->glWidget->setCameraRoll(d.camRoll);
    }

    // Forza il ridisegno immediato con le nuove angolazioni
    ui->glWidget->update();

    // 12. Il suono dello script e' gia' nel suo slot (applyCommonData, in
    // testa). AVVIO AUTOMATICO DELL'AUDIO AL CARICAMENTO:
    const QString fullLoadedText = m_scene.surfaceScriptApplied + "\n" + m_scene.surfaceTextureCode + "\n" + m_scene.bgTextureCode;
    if (fullLoadedText.contains("//MUSIC:") || fullLoadedText.contains("//SOUND_BEGIN")) {
        onRunSoundClicked();
    }

    // 13. L'editor segue gli slot appena scritti (e' la loro vista), coi tasti.
    updateScriptButtonText();

    QTimer::singleShot(20, this, [this, isScript]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(m_scene.steps);
            ui->glWidget->setRaySteps(m_scene.steps);

            if (!isScript) {
                checkAndTriggerMeshUpdate();
            } else {
                ui->glWidget->update();
            }
        }
    });

    m_textureModified = false;

    // La superficie caricata diventa il RIFERIMENTO dei campi del dock 4D: da
    // qui l'utente misura di quanto si e' mosso. In coda perche'
    // setRotation4D/setCrossSectionP scrivono lo stato poco sopra.
    resetNav4DBaseline();

    // Suggerimento d'uso ("hintText"), come in applyMotionExample: le superfici
    // passano da QUI e non da quella, quindi senza questa riga il messaggio
    // comparirebbe solo sui record.
    m_sceneHintFromSurfacePreset = true;
    showSceneHint(d.hintText, d.hintSeconds);

    // SNAPSHOT DELL'APPLICATO: da qui le equazioni a schermo sono quelle dei
    // campi del preset. applyMotionExample lo fa gia'; il load di una
    // SUPERFICIE no, e lo snapshot restava quello della scena di prima: record,
    // poi superficie, poi Invio su una costante riapplicava le equazioni del
    // record (test degli scenari).
    snapshotActiveEquations();

    // COSTANTI: giudizio finale a scena completa, sui soli campi del preset
    // (refreshConstants chiude le eventuali modifiche in sospeso: da qui campi e
    // scena coincidono). Senza, l'ultimo ricalcolo del load poteva ancora
    // contare l'applicato della scena di PRIMA.
    refreshConstants(/*restoreTextOnNegative=*/false);
}

// ---- Passi del load di un record (applyMotionExample), nell'ordine ----

void MainWindow::warnMissingRecordImages(const MissingImageScan &scan)
{
    if (scan.paths.isEmpty()) return;
    // I percorsi stanno su righe tutte loro e NON hanno spazi dove
    // spezzarsi: nel testo principale (che non va a capo) allargherebbero
    // il box quanto sono lunghi. Nell'informativeText il resto del testo va
    // a capo e i percorsi restano le uniche righe lunghe.
    const bool many = scan.paths.size() > 1;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Record Image Not Found");
    box.setText(many ? "Some images used in this record were not found."
                     : "The image used in this record was not found.");
    // Se DOVUNQUE l'immagine mancante lascia in piedi uno script, si dice
    // che il record parte lo stesso con la sua texture procedurale: e' il
    // caso in cui non si perde nulla se non la foto.
    const bool someScriptSurvives = scan.bgKeptScript || scan.surfaceKeptScript
                                    || scan.meshKeptScript;
    box.setInformativeText(scan.paths.join("\n") +
                           (someScriptSurvives
                                ? "\n\nThe procedural texture will be loaded without it."
                                : "\n\nThe animation will be loaded without it."));
    box.exec();
}

void MainWindow::stopMotionForRecordLoad()
{
    m_masterStopped = false;
    // Nuovo record caricato: dimentica un eventuale stop manuale del suono
    // precedente, cosi' l'audio del nuovo preset puo' partire. Idem per gli
    // stop manuali dei clock geometria/texture/sfondo.
    m_userStoppedSound = false;
    m_userStoppedGeomClock = false;
    m_userStoppedTexClock = false;
    m_userStoppedBgClock = false;

    // Moto camera del record: riletto piu' sotto dal JSON ("activeMotion");
    // azzerato qui perche' un residuo di sessione non guidi l'avvio automatico
    // di un record storico privo della chiave.
    m_scene.lastCameraMotion.clear();

    InputValidator::resetGeodesicWarning();

    // 1. STOP TOTALE (Reset stato iniziale)
    m_audioController->stopAll();

    ui->glWidget->pauseMotion(); // Ferma rotazioni
    ui->glWidget->resetTransformations();

    if (pathRunning(CameraPaths::Path4D)) onDepartureClicked();
    if (pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();

    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
    }

    // Caricare un nuovo record = nuovo "primo Departure": i flag di sessione che
    // gate-ano la neutralizzazione dell'orientamento (oggi in CameraPaths:
    // resetSession) NON venivano mai rimessi a false, quindi dal secondo
    // record in poi il path partiva da una base 4D non-neutra (l'angolo psi del
    // preset appena applicato via setRotation4D) -> il frame della camera si ribaltava
    // e il moto appariva percorso in senso OPPOSTO a ogni ricarica. Reset qui: ogni
    // record riparte pulito come il primissimo della sessione.
    // Anche il tempo dei path torna a t=0: un nuovo record deve partire da li',
    // non dal tempo RESIDUO del moto precedente (che altrimenti farebbe
    // ripartire la traiettoria da una fase arbitraria a ogni ricarica).
    m_paths->resetSession();

    if (m_btnStart) m_btnStart->setText("START");
    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
}

void MainWindow::applyRecordMode(const LibraryItem &data, const SceneState &file)
{
    const bool isImplicit = data.isImplicitMode;
    // Le superfici implicite da script sono gestite da applyCommonData: non sovrascrivere.
    const bool isImplicitScript = isImplicit && !data.scriptCode.isEmpty();

    if (isImplicit) {
        ui->tabModeSelector->setCurrentIndex(1); // Forza Tab Ray Marching

        // I dati parametrici del record di prima li svuota applyCommonData, in
        // testa (equationTextsFromItem). Il Run parametrico torna eseguibile
        // qui, esplicitamente (lo faceva il gestore della digitazione).
        m_parametricApplied = false;
        clearSurfaceScript();
        exitMetricScriptMode();

        if (!isImplicitScript) {
            // L'equazione per il motore, con lo stesso filtro di sicurezza del
            // campo (implicitTextsFromItem: vuota o senza '=' -> la sfera). Il
            // campo lo scrive applyCommonData, in testa.
            const QString eqToLoad = safeImplicitEquation(file.rm.equation);

            // SOTTO-TAB CROSS SECTION: stesso trattamento del ramo superfici
            // (applySurfaceExample). Senza, un record girato in Cross Section si
            // ricaricava con la SFERA del ramo 3D: qui si scriveva solo
            // lineEquation, e nel JSON quella chiave contiene l'equazione del
            // sotto-tab 3D, non la 4D.
            // Siamo PRIMA di applyCommonData (riga ~13051), quindi il giudizio
            // sulle costanti in coda a quella funzione (checkParametricDependency
            // -> updateConstantsUIState, che legge il sotto-tab ATTIVO) vedra' gia'
            // l'equazione 4D: e' cio' che tiene A/B/C "usate" invece di riscriverle
            // a 1. Il testo dell'editor 4D lo scrive applyCommonData, in testa.
            // La regola e' choicesFromItem (la stessa delle superfici).
            const QString csEq = data.crossSectionEq.trimmed();
            const bool loadCrossSection = file.crossSectionTab;
            // blockSignals: currentChanged e' connesso a applyImplicitSubTabReset,
            // che caricherebbe la superficie di DEFAULT del sotto-tab buttando via
            // il record appena caricato.
            setCrossSectionTab(loadCrossSection);

            if (ui->glWidget) {
                if (loadCrossSection) {
                    // Il Cross Section NON passa da setImplicitEquation (che e' per
                    // definizione il ramo 3D e riporterebbe il motore la'): serve il
                    // commit completo, l'unico che imposta m_eqCrossSectionF e il
                    // flag di ramo.
                    QString csF = csEq.contains("=")
                        ? QString("(%1) - (%2)").arg(csEq.section('=', 0, 0).trimmed(),
                                                     csEq.section('=', 1).trimmed())
                        : QString("(%1) - (0.0)").arg(csEq);
                    if (ui->glWidget->validateAndApplyImplicitShader(
                            csF, ui->glWidget->currentTextureCode(),
                            ui->glWidget->currentDisplacementCode(),
                            /*useCrossSection=*/true)) {
                        ui->glWidget->rebuildShader();
                    } else {
                        // Equazione 4D che non compila: il commit ha gia' ripristinato
                        // da se' lo stato precedente. Ripieghiamo sul ramo 3D.
                        setCrossSectionTab(false);
                        ui->glWidget->setImplicitEquation(eqToLoad);
                    }
                } else {
                    ui->glWidget->setImplicitEquation(eqToLoad);
                }
                // La linguetta e' stata mossa a segnali bloccati, quindi
                // applyImplicitSubTabReset non ha girato (ed e' voluto). Ma
                // updateRenderState vive di li' per il ramo implicito: senza, le
                // rotazioni 4D restano spente su un record Cross Section, che le usa.
                updateRenderState();
                // Equazione appena committata: risincronizza lo slider trasparenza
                // (campi a prodotto -> disabilitato + popup). Vedi syncImplicitAlphaSlider.
                syncImplicitAlphaSlider(true, true);
            }
            // ++++++++++++++++++++++++++++++++++++++++++++++++++++++
        } else {
            // RECORD DA SCRIPT: la sua SDF e' un campo 3D, il Cross Section non
            // c'entra. Questo ramo non toccava ne' il sotto-tab ne' il ramo del
            // motore: dopo un record Cross Section restavano i suoi, lo shader
            // si compilava con la rotazione della sezione e il Save scriveva
            // implicitUsesCrossSection true (trovato dal round-trip con un ordine
            // diverso dei preset, poi dal test degli scenari).
            // blockSignals: currentChanged caricherebbe la default del sotto-tab.
            setCrossSectionTab(false);
            if (ui->glWidget) ui->glWidget->useImplicit3DBranch();
            updateRenderState();
        }

    } else {
        ui->tabModeSelector->setCurrentIndex(0); // Forza Tab Parametrica
        // Distruggiamo i dati Ray Marching precedenti (a segnali bloccati; i
        // Run Ray Marching tornano eseguibili qui, come faceva il gestore
        // della digitazione).
        // (Equazione, rilievo e texture li svuota applyCommonData, in testa.)
        m_implicitApplied = false;
        m_rmTextureApplied = false;
        if (ui->glWidget) {
            ui->glWidget->setDisplacementCode("");
            ui->glWidget->setTextureCode("");
        }
    }
}

void MainWindow::applyRecordColors(const LibraryItem &data)
{
    // 3b. Colori
    if (data.hasCustomColors && !data.color1.isEmpty()) {
        QColor surfCol(data.color1);

        // Stesso trattamento del ramo superfici (applySurfaceExample): colore
        // GLOBALE del preset, quindi bypass acceso perche' non finisca nella
        // mesh selezionata, e slider allineati alla fascia se ce n'e' una.
        const bool oldBypass = ui->glWidget->meshAppearanceBypass();
        ui->glWidget->setMeshAppearanceBypass(true);
        ui->glWidget->setColor(surfCol.redF(), surfCol.greenF(), surfCol.blueF());
        ui->glWidget->setMeshAppearanceBypass(oldBypass);

        if (ui->glWidget->activeMeshPart() >= 0) syncAppearanceControlsToActiveMesh();
        else                                     onColorTargetChanged();
    }

    applyPresetAlpha(data.alpha);

    // 3c. Colore Sfondo: dallo stato (scena del file, in testa). Un record
    // senza la chiave ha il grigio di default; prima si teneva quello del
    // preset aperto prima.
    ui->glWidget->setBackgroundColor(m_scene.bgColor);
}

void MainWindow::applyRecordCamera(const LibraryItem &data, const SceneState &file)
{
    // CAMERA E MOTI: dalla struttura, non piu' dal file (come sfondo e suono).
    // Qui il load RILEGGEVA il JSON del record ("bypassiamo la limitazione della
    // libreria": LibraryItem non portava sfondo, ancore e moti). Ora parseJson
    // li legge tutti, con gli stessi default (tappa 3 dello "stato unico della
    // scena"), e ogni campo viene riscritto SEMPRE: prima, se il file non si
    // apriva, camera e moti restavano quelli del record precedente, e un record
    // senza "observer4D" si teneva l'osservatore di quello aperto prima.

    if (!data.hasCamera3D) {
        ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
        ui->glWidget->setRotationQuat(QQuaternion());
        ui->glWidget->setCameraYaw(0.0f);
        ui->glWidget->setCameraPitch(0.0f);
        ui->glWidget->setCameraRoll(0.0f);
        ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    } else {
        ui->glWidget->setCameraPos(QVector3D(data.camX, data.camY, data.camZ));
        ui->glWidget->setRotationQuat(QQuaternion(data.rotW, data.rotX, data.rotY, data.rotZ));
        // Il quaternione di un RECORD e' un'istantanea intenzionale (l'utente
        // l'ha ruotato cosi' e l'ha salvato): senza questo mark, l'avvio del
        // path in coda al load (startRecordCameraMotion -> onDepartureClicked, primo Departure
        // perche' la sessione dei path e' appena stata azzerata) passava per
        // neutralizeDefaultRotationForPath e AZZERAVA la rotazione salvata --
        // il record ricaricato appariva identico a quello di partenza. Il ramo
        // sopra (record vecchi senza camera3D) resta neutralizzabile: quel
        // tilt 30/30 e' davvero cosmetico.
        ui->glWidget->markUserRotated();
        ui->glWidget->setCameraYaw(data.camYaw);
        ui->glWidget->setCameraPitch(data.camPitch);
        ui->glWidget->setCameraRoll(data.camRoll);
    }

    ui->glWidget->setObserverPos4D(data.observer4D);

    // Moto camera attivo al salvataggio: guida l'avvio automatico
    // (startRecordCameraMotion). Nei record storici manca -> stringa vuota =
    // cascata legacy; "none" (salvato a moti fermi) idem.
    // Le regole sono in motionFromItem. Vista dei due path: i record col solo
    // "pathMode" (formato storico) la applicano a entrambi, quelli senza
    // nessuna delle due tornano a Tangent (lo decide parseJson). Velocita': 0 =
    // chiave assente (file vecchi) o path 4D azzerato dal Save in Ray Marching
    // 3D, cioe' il default (scrivere 0 dava la velocita' minima).
    m_scene.lastCameraMotion = file.lastCameraMotion;
    setPathViewModes(file.pathViewMode4D, file.pathViewMode3D);
    setPathSpeed3D(file.pathSpeed3D);
    setPathSpeed4D(file.pathSpeed4D);
    // Abilitazione coerente con lo stato dei path (a load fermo -> disabilitati).
    updateViewButtonsEnabled();
}

void MainWindow::applyRecordSurfaceTexture(const LibraryItem &data, const MissingImageScan &scan)
{
    const bool texEnabled = data.textureEnabled;
    const bool isImplicit = data.isImplicitMode;
    QString texCode = data.textureCode;
    // Codice della texture RAY MARCHING, tenuto a parte perche' il ramo
    // implicito qui sotto AZZERA texCode (per non innescare la pipeline
    // parametrica) e con esso spariva l'unica sorgente da cui si estrae il tag
    // //IMG:. Risultato: il record salvava l'immagine ma al caricamento non la
    // caricava mai, e il triplanar campionava la texture tappabuchi -- a schermo
    // sembrava la texture di default.
    QString rmTexCodeForImage;

    float surfZoom = data.zoom;
    float surfPanX = data.panX, surfPanY = data.panY;
    float surfRot = data.rotation;

    // --- TEXTURE DI SUPERFICIE E RILIEVO: i testi sono gia' nello stato ---
    // applyCommonData li ha assegnati in testa dalla scena del file
    // (textureTextsFromItem: codici senza suono, senza il tag //IMG: di
    // un'immagine mancante; ancore SEMPRE riscritte, anche a vuoto). Qui il
    // motore. In Ray Marching la texture va nel marcher e l'IMMAGINE si estrae
    // dal codice del file (texCode resta vuoto: non deve innescare la pipeline
    // parametrica piu' giu'); in parametrico texCode va nel motore, se compila
    // (APPLICAZIONE TEXTURE SUPERFICIE, piu' sotto). Il rilievo solo in Ray
    // Marching.
    if (isImplicit) {
        if (ui->glWidget) ui->glWidget->setTextureCode(m_scene.rm.texture);
        SE_TEXP("common:RM-texture-del-record");
        rmTexCodeForImage = data.textureCode;
        texCode = "";
    } else {
        texCode = m_scene.surfaceTextureScriptText;
    }
    if (ui->glWidget) ui->glWidget->setDisplacementCode(m_scene.rm.displacement);

    // Colori u_col1/u_col2 (bianco e nero se il record non li porta).
    ui->glWidget->setGlobalTextureColors(
        data.texColor1.isEmpty() ? QColor(Qt::white) : QColor(data.texColor1),
        data.texColor2.isEmpty() ? QColor(Qt::black) : QColor(data.texColor2));

    // L'immagine della superficie: in Ray Marching texCode e' vuoto e si cerca
    // nel codice del file. Mancante: un imgPath vuoto manda la superficie
    // sulla texture di default.
    QString imgPath = scan.surfaceMissing
        ? QString()
        : TextureCode::resolveImagePath(texCode.isEmpty() ? rmTexCodeForImage : texCode);

    applySurfaceTextureToEngine();
    // In RM e' QUESTA riga a rimettere in vigore la texture: createImplicitFragmentShader
    // inietta il codice solo se m_textureEnabled, e setGlobalTextureEnabled e'
    // l'unico punto (con setTextureCode) che invalida m_pipelineImplicit.
    SE_TEXP("common:texture-riabilitata");

    // (L'editor, vista degli slot, li segue.)
    refreshScriptEditor();


    // COSTANTI: il giudizio finale, a scena completa. Texture e sfondo del
    // record sono nello stato dalla testa di applyCommonData, quindi il
    // giudizio la' dentro li vede gia' (prima arrivavano DOPO, e si giudicava
    // sui codici della SCENA PRECEDENTE: "Wireframe Rec", F=3 nel JSON, si
    // apriva con F=1; "Hybrid Hyperbolic" teneva accesa la F dello sfondo
    // provato prima -- e serviva una seconda assegnazione dei valori del file
    // qui). Resta il giudizio, una volta, con le fasce del record ormai
    // nel motore, e il motore allineato ai campi.
    refreshConstants(/*restoreTextOnNegative=*/false);
    SE_TEXP("record:costanti-rigiudicate");

    // Svuota forzatamente gli shader procedurali "incastrati" prima di caricare il nuovo!
    if (ui->glWidget) {
        ui->glWidget->clearTexture();
        commitSurfaceTextureCode(QString());   // ricompila lo shader standard
    }

    // --- APPLICAZIONE TEXTURE SUPERFICIE ---
    if (texEnabled) {
        // Trasformazione GLOBALE del preset: va nello stato globale, non nella
        // fascia selezionata. I setFlat*() passano da
        // commitFlatTransformToActivePart e, con una mesh attiva, l'avrebbero
        // scritta come trasformazione PROPRIA di quella parte -- stesso schema
        // del colore globale che finiva nella fascia.
        ui->glWidget->setGlobalTexTransform(surfZoom, QVector2D(surfPanX, surfPanY), surfRot);

        // RAY MARCHING: qui texCode e' vuoto per costruzione, quindi il blocco
        // parametrico sotto non gira -- ed e' giusto, la sua pipeline (script
        // custom, checkbox, editor) e' gia' stata percorsa dal ramo implicito.
        // Restava fuori pero' anche il CARICAMENTO DELL'IMMAGINE, che serve a
        // entrambi i modi: lo shader triplanar del record campiona il sampler
        // `tex`, e senza questa riga nessuno lo riempiva -- restava la texture
        // tappabuchi, che a schermo sembra la texture di default.
        if (texCode.isEmpty() && !imgPath.isEmpty() && !rmTexCodeForImage.isEmpty()) {
            ui->glWidget->loadTextureFromFile(imgPath);
            ui->glWidget->rebuildShader();   // il sampler e' cambiato
        }

        if (!texCode.isEmpty()) {
            bool hasCustomLogic = texCode.contains("return") || texCode.contains("vec3") || texCode.contains("vec4") || texCode.contains("mainImage");

            // 1. Carica l'immagine se presente (altrimenti la GPU e' gia' vuota:
            // clearTexture in testa a questo blocco)
            if (!imgPath.isEmpty()) {
                ui->glWidget->loadTextureFromFile(imgPath);
            }

            // 2. Carica lo script indipendentemente dall'immagine. Qui si e'
            // solo in PARAMETRICO: in Ray Marching texCode e' vuoto per
            // costruzione (la texture sta in lineTexture / m_textureCode).
            // La copia applicata la scrive commitSurfaceTextureCode, solo se il
            // motore compila: prima si scriveva a prescindere, e un codice che
            // non compilava risultava applicato con il motore vuoto.
            if (imgPath.isEmpty()) generateTexture();
            if (!commitSurfaceTextureCode(texCode)) {
                // Non compila: nulla di applicato, si vede la scacchiera (o
                // l'immagine) e lo script resta nel suo slot, dove lo si puo'
                // correggere.
                qWarning() << "applyMotionExample: texture del record non compilata:"
                           << ui->glWidget->getShaderError();
                if (imgPath.isEmpty()) applyDefaultCheckerShader();
            } else if (!hasCustomLogic && imgPath.isEmpty()) {
                applyDefaultCheckerShader();   // nessuna logica e nessuna immagine
            }
        }
        else {
            // La scacchiera nel sampler solo se non c'e' l'immagine del ramo Ray
            // Marching appena caricata qui sopra: la copriva, e la rimetteva poi
            // il Run in coda al load.
            if (!surfaceHasImage()) generateTexture();
            applyDefaultCheckerShader();
            ui->glWidget->rebuildShader();
        }
    } else {
        // Preset SENZA texture: la copia applicata e' gia' vuota (reset qui
        // sopra). La trasformazione 2D va riportata a neutra come
        // il codice qui sopra. Il ramo texEnabled la imposta sempre (riga con
        // setGlobalTexTransform), questo la lasciava invece stantia: caricando un
        // record senza texture dopo uno con zoom salvato, la scacchiera di
        // default accesa a mano appariva ingrandita. Gemello del reset in
        // clearTextureMemory (spegnimento del checkbox).
        ui->glWidget->setGlobalTexTransform(1.0f, QVector2D(0.0f, 0.0f), 0.0f);
    }
}

void MainWindow::applyRecordBackgroundTexture(const LibraryItem &data)
{
    const bool bgTexEnabled = data.bgTextureEnabled;
    // (Ancore del suono e dello sfondo: in testa ad applyCommonData.)
    // Messaggio dello sfondo: scritto qui, PRIMA del showSceneHint in coda
    // alla funzione, che lo compone con gli altri due.
    m_currentBgTextureHintText    = data.bgHintText;
    m_currentBgTextureHintSeconds = data.bgHintSeconds;
    const float bgZoom = data.bgZoom;
    const float bgPanX = data.bgPanX, bgPanY = data.bgPanY;
    const float bgRot = data.bgRotation;
    const int bgSkyMode = GLWidget::bgSkyModeFromName(data.bgSkyMode);

    // SUONO E SFONDO: slot e codici gia' nello stato (textureTextsFromItem). Il
    // suono e' estratto dai codici GREZZI, anche dallo sfondo spento; i codici
    // grafici sono senza suono (Kerr Spin Animated lo porta tra marcatori
    // spaziati: rimasto nella texture, questa non compilava); lo sfondo spento
    // non ha codice (vedi forgetBackgroundTexture: tenerlo era lo "sfondo-
    // immagine perso"); il tag //IMG: di un'immagine mancante e' tolto, o tutto
    // il codice se c'era solo quello (il popup e' gia' stato dato, in cima).
    const QString bgCode = m_scene.bgTextureCode;

    // --- APPLICAZIONE TEXTURE BACKGROUND ---
    ui->glWidget->setBackgroundTextureEnabled(bgTexEnabled);
    // Sfondo spento: via anche ancora, messaggio e immagine in GPU (quella del
    // record precedente, altrimenti, ricomparirebbe alla riaccensione).
    if (!bgTexEnabled) forgetBackgroundTexture();
    // Forma dello sfondo: SEMPRE, anche quando il record non ha sfondo o non ha
    // la chiave. E' uno stato appiccicoso del motore: saltandolo, un record
    // caricato dopo uno solidale si terrebbe il cielo del precedente.
    applyBackgroundSkyMode(bgSkyMode);

    // L'immagine di sfondo riparte SEMPRE da zero a ogni record: ogni ramo qui
    // sotto rimette nel motore la sua, o la default (a sfondo spento lo fa
    // forgetBackgroundTexture qui sopra). Un record senza immagine non deve
    // ereditare quella del record aperto prima -- visto su Paths/Clover, che
    // si prendeva l'8.png, quando il percorso era una copia in MainWindow.

    // Inquadratura dello sfondo: SEMPRE quella del record, anche a sfondo SPENTO.
    // Stava dentro il ramo qui sotto, quindi un record senza sfondo si teneva
    // zoom/pan/rotazione del record aperto prima, e il Save li scriveva nel
    // file (trovato dal test di andata e ritorno: 10 record Paths). Un record
    // senza le chiavi riparte da 1/0/0, i default di bgZoom/bgPan/bgRot.
    if (ui->glWidget) {
        ui->glWidget->setBackgroundFraming(bgZoom, QVector2D(bgPanX, bgPanY), bgRot);
    }

    if (bgTexEnabled && !bgCode.isEmpty()) {
        if (ui->glWidget) {
            ui->glWidget->setBackgroundTexColors(m_scene.bgTexColor1, m_scene.bgTexColor2);
        }

        QString bgImgPath = TextureCode::resolveImagePath(bgCode);
        if (!bgImgPath.isEmpty() && !bgImgPath.startsWith("NOT_FOUND|")) {
            // Il motore ricorda l'immagine caricata: al prossimo salvataggio il
            // tag //IMG: viene riscritto da li' (vedi PresetSerializer, ramo
            // background), cosi' un record aperto e risalvato non perde lo sfondo.
            ui->glWidget->setBackgroundTexture(bgImgPath);
        } else {
            // Sfondo procedurale: nel sampler torna la default. Restava
            // l'immagine del record aperto prima, che uno script che campiona
            // iChannel0 avrebbe mostrato (test degli scenari).
            ui->glWidget->setBackgroundImage("background.png");
        }

        bool bgHasCustomLogic = bgCode.contains("return") || bgCode.contains("vec3") || bgCode.contains("vec4") || bgCode.contains("mainImage");
        if (bgHasCustomLogic || bgImgPath.isEmpty() || bgImgPath.startsWith("NOT_FOUND|")) {
            ui->glWidget->loadBackgroundScript(bgCode);
        }
    }
    else if (bgTexEnabled) {
        // bgTexEnabled ma NIENTE codice: lo sfondo va riportato alla texture di
        // DEFAULT. Senza questo ramo non si entrava affatto nel blocco sopra e
        // nessuno toccava lo sfondo, che restava quello del record PRECEDENTE
        // -- visibile caricando un record la cui immagine di sfondo non esiste
        // piu' (bgCode svuotato qui sopra): primo record della sessione ->
        // default, altrimenti lo sfondo di quello prima.
        // clearBackgroundScript() spegne m_bgIsScript e rimette la pipeline
        // immagine, poi si carica la texture di default.
        if (ui->glWidget) {
            ui->glWidget->setBackgroundFraming(1.0f, QVector2D(0.0f, 0.0f), 0.0f);
            ui->glWidget->setBackgroundTexture("background.png");
        }
    }
}

void MainWindow::syncTextureControlsAfterRecordLoad(const LibraryItem &data)
{
    const bool texEnabled = data.textureEnabled;
    const bool bgTexEnabled = data.bgTextureEnabled;
    const QString &bgCode = m_scene.bgTextureCode;
    if (editingBackground()) {
        refreshTextureCheckbox();

        // Picker Colore attivi solo se lo sfondo è acceso E usa quel colore (indipendenti).
        bool bgCol1 = bgTexEnabled && bgCode.contains("u_col1");
        bool bgCol2 = bgTexEnabled && bgCode.contains("u_col2");
        ui->radioTexColor1->setEnabled(bgCol1);
        ui->radioTexColor2->setEnabled(bgCol2);
        if ((bgCol1 || bgCol2) && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
            QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
            bool oldRad = target->blockSignals(true);
            target->setChecked(true);
            target->blockSignals(oldRad);
        }
        // Surface resta SEMPRE abilitato anche in editing sfondo: è il modo per tornare
        // alla superficie (la coppia Surface/Background va sempre navigabile) e
        // l'indicatore di target non deve mai sparire. (Prima: setEnabled(!texEnabled).)
        ui->radioSurface->setEnabled(true);
        if (!texEnabled && ui->btnFlatPreview->isChecked()) {
            ui->btnFlatPreview->setChecked(false);
        }
    } else {
        refreshTextureCheckbox();   // segue l'intenzione (texEnabled)
        updateTextureUIState(texEnabled);
    }

    if(ui->radioTexColor1->isChecked() || ui->radioTexColor2->isChecked() || editingBackground()) {
        onColorTargetChanged();
    }

    updateRenderState();
}

void MainWindow::applyRecordSpeedsAndAngles(const LibraryItem &data)
{
    const bool isImplicit = data.isImplicitMode;
    // 6. VELOCITÀ E ANGOLI
    ui->glWidget->setNutationSpeed(data.speedNut);
    ui->glWidget->setPrecessionSpeed(data.speedPrec);
    ui->glWidget->setSpinSpeed(data.speedSpin);

    // VELOCITA' 4D (le rotazioni 4D del dock 3D): azzerate in ray marching SOLO
    // fuori dal Cross Section, dove non avrebbero effetto. Nel Cross Section sono
    // il MOTO del record -- fanno evolvere nel tempo la sezione
    // dell'ipersuperficie mostrata -- quindi azzerarle ricaricava un record FERMO
    // al posto di quello registrato. Stesso criterio del salvataggio
    // (keep4DAngles in presetserializer.cpp) e degli angoli statici piu' sotto:
    // le due meta' devono concordare, o lo stato si perde comunque da un lato.
    const bool keep4DSpeeds = !isImplicit || crossSectionTab();
    float spdOmega = keep4DSpeeds ? data.speedOmega : 0.0f;
    float spdPhi   = keep4DSpeeds ? data.speedPhi   : 0.0f;
    float spdPsi   = keep4DSpeeds ? data.speedPsi   : 0.0f;

    ui->glWidget->setOmegaSpeed(spdOmega);
    ui->glWidget->setPhiSpeed(spdPhi);
    ui->glWidget->setPsiSpeed(spdPsi);

    ui->lightSlider->setValue(qRound(data.lightIntensity * 100.0f));
    setFillLightUI(data.fillLight);
    setBorderUI(borderStyleOf(data));

    int savedMode = (data.lightingMode != -1) ? data.lightingMode : 0;
    bool want4D = false;
    if (data.hasLightingState) {
        want4D = data.use4DLighting;
    } else {
        QString wText = data.w.trimmed();
        want4D = (!wText.isEmpty() && wText != "0" && wText != "0.0");
    }

    m_scene.lightingMode4D = savedMode;
    ui->glWidget->set4DLighting(want4D);
    ui->glWidget->setLightingMode4D(savedMode);

    QString btnText;
    switch(savedMode) {
    case 0: btnText = "Directional Lighting"; break;
    case 1: btnText = "Observer Lighting"; break;
    case 2: btnText = "Slice Lighting"; break;
    default: btnText = "Directional Lighting"; break;
    }
    if (ui->btnLightMode) ui->btnLightMode->setText(btnText);
    update4DButtonState();

    refreshRotationSpeedLabels();

    if (data.restoreAngles) {
        // Angoli statici azzerati in Ray Marching SOLO fuori dal Cross Section.
        // Lo zero secco risale a quando il ray marching non leggeva affatto
        // omega/phi/psi. Nel sotto-tab Cross Section quei tre angoli sono lo STATO
        // PRINCIPALE della superficie -- decidono QUALE sezione dell'ipersuperficie
        // 4D si vede (li usa %CROSS_SECTION_P%) -- e azzerarli riportava ogni
        // record alla sezione frontale, perdendo l'inquadratura registrata.
        // Stesso criterio del salvataggio (keep4DAngles in presetserializer.cpp):
        // le due meta' devono concordare, o si perde comunque.
        const bool keep4DAngles = !isImplicit || crossSectionTab();
        float stOmega = keep4DAngles ? data.startOmega : 0.0f;
        float stPhi   = keep4DAngles ? data.startPhi   : 0.0f;
        float stPsi   = keep4DAngles ? data.startPsi   : 0.0f;
        ui->glWidget->setRotation4D(stOmega, stPhi, stPsi);
    }

    // TRASLAZIONE DEL PIANO DI SEZIONE lungo p: l'altra meta' dello stato 4D del
    // Cross Section, insieme ai tre angoli qui sopra. Fuori da restoreAngles: e'
    // stato APPICCICOSO del motore, quindi va scritta SEMPRE (anche a 0), o un
    // record caricato dopo averne visto un altro sezionato eredita la p precedente.
    if (ui->glWidget)
        ui->glWidget->setCrossSectionP(isImplicit ? data.crossSectionP : 0.0f);

    ui->glWidget->update();
}

void MainWindow::startRecordCameraMotion(const LibraryItem &data)
{
    auto isReal = [](const QString &s) {
        QString t = s.trimmed();
        return !t.isEmpty() && t != "0" && t != "0.0";
    };

    // In Ray Marching FUORI dal Cross Section le rotazioni 4D (omega/phi/psi) non
    // hanno effetto e vengono azzerate (applyRecordSpeedsAndAngles).
    // hasRotation deve guardare le velocità EFFETTIVE applicate al motore, non
    // quelle grezze del record: altrimenti un preset RM con omega/phi/psi salvati
    // faceva partire il rotationTimer (isAnimating()==true) pur senza alcuna
    // rotazione visibile, tenendo il master bloccato su STOP anche a tutto fermo.
    // Leggendo le effettive, il ramo si adatta da solo al Cross Section, dove
    // quelle velocità NON vengono azzerate e il moto deve davvero ripartire.
    bool hasRotation = (std::abs(data.speedPrec) > 0.001f ||
                        std::abs(data.speedNut)  > 0.001f ||
                        std::abs(data.speedSpin) > 0.001f ||
                        std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                        std::abs(ui->glWidget->getPhiSpeed())   > 0.001f ||
                        std::abs(ui->glWidget->getPsiSpeed())   > 0.001f);
    bool hasPath4D = isReal(data.path4D_x) || isReal(data.path4D_y) || isReal(data.path4D_z) || isReal(data.path4D_w) ||
            isReal(data.path4D_alpha) || isReal(data.path4D_beta) || isReal(data.path4D_gamma);

    bool hasPath3D = isReal(data.path3D_x) || isReal(data.path3D_y) || isReal(data.path3D_z) || isReal(data.path3D_roll);

    // Moto camera del record: riparte SOLO quello attivo al salvataggio
    // (m_scene.lastCameraMotion, letto dal JSON "activeMotion" piu' sopra). La
    // vecchia sequenza fissa (rotazioni, poi 4D con precedenza sul 3D) faceva
    // sempre vincere il path 4D. Record storici senza chiave: sequenza di prima.
    QString pick = m_scene.lastCameraMotion;
    if (pick == "rotation" && !hasRotation) pick.clear();
    if (pick == "path4D" && !hasPath4D) pick.clear();
    if (pick == "path3D" && !hasPath3D) pick.clear();

    if (pick == "rotation") {
        if (ui->btnStart_2) ui->btnStart_2->setText("STOP");
        ui->glWidget->resumeMotion();
    } else if (pick == "path4D") {
        if (!pathRunning(CameraPaths::Path4D)) onDepartureClicked();
    } else if (pick == "path3D") {
        if (!pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();
    } else {
        if (hasRotation) {
            if (ui->btnStart_2) ui->btnStart_2->setText("STOP");
            ui->glWidget->resumeMotion();
            m_scene.lastCameraMotion = "rotation";
        }
        if (hasPath4D) onDepartureClicked();
        else if (hasPath3D) onDeparture3DClicked();
    }

    // Sincronizzazione del PRIMO FRAME al path: applyCommonData ha appena piazzato la
    // camera3D SALVATA nel preset (l'istantanea del momento in cui il record fu creato),
    // ma il path la muove da t=0 su una traiettoria del tutto diversa (es. Gyroid Race:
    // camera salvata a x=-2.12, path(t=0) a x=1.5). Senza questo, il primo frame mostra
    // la camera salvata e poi al primo tick la vista SALTA sul path -> discontinuità
    // secca (più visibile su mobile, dove il primo frame resta a schermo più a lungo).
    // Eseguendo subito un tick, la camera è già sul path prima del primo paint, così il
    // moto parte fluido dal punto iniziale della traiettoria.
    // Il FOV va caricato PRIMA del tick di sincronizzazione, perche' il tick
    // ridisegna gia' con la proiezione corrente.
    applyCameraFov(resolveSavedFov(data.cameraFov, data.fov3D, data.fov4D));
    if (pathRunning(CameraPaths::Path4D)) m_paths->tick(CameraPaths::Path4D);
    else if (pathRunning(CameraPaths::Path3D)) m_paths->tick(CameraPaths::Path3D);

    ui->glWidget->setProjectionMode(data.projectionMode);
    updateProjectionButtonText();
}

bool MainWindow::runRecordGeometry(const LibraryItem &data)
{
    const bool isImplicit = data.isImplicitMode;
    const bool texEnabled = data.textureEnabled;
    // 7. AVVIO AUTOMATICO GRAFICA
    // 1. Nuova logica di validazione sicura
    bool hasValidEquations;
    if (data.isImplicitMode) {
        // In modalità Ray Marching, è valida solo se non è il segnaposto dello script
        hasValidEquations = !data.implicitEq.trimmed().isEmpty() &&
                !data.implicitEq.contains("// Controlled by Script");
    } else {
        // In modalità Parametrica, controlliamo la X
        hasValidEquations = (data.x.trimmed().length() > 0 && data.x != "0" && data.x != "0.0");
    }

    // 2. Deduzione corretta
    bool isScript = data.isScript || (!data.scriptCode.isEmpty() && !hasValidEquations);

    snapshotActiveEquations();

    // Script metrico (return mat3): la mesh NON è parametrica, arriva già pronta
    // dal flusso geodetico (runMetricScript → checkAndTriggerMeshUpdate, custom
    // mesh). updateSurfaceData()/computeMesh() la ricalcolerebbe dalle equazioni
    // x/y/z (qui "0,0,0"), distruggendo l'imbuto geodetico e lasciando un quad
    // degenere. Anche il renderMode 11 (legacy parametrico) non va applicato.
    static const QRegularExpression metricReturnRe(R"(\breturn\s+mat3\s*\()");
    const bool isMetricScript =
            isScript && data.scriptCode.contains(metricReturnRe);

    if (!isScript) {
        // onStartClicked RILEGGE lineTexture e ricommitta lo shader implicito:
        // e' l'ultimo punto che puo' cambiare cio' che si vede. Se la riga PRIMA
        // e quella DOPO differiscono, il colpevole e' qui dentro.
        SE_TEXP("record:pre-onStartClicked");
        runScene(RunOrigin::Load);
        SE_TEXP("record:post-onStartClicked");
    } else if (isMetricScript) {
        // La mesh geodetica è già stata generata e texturizzata da applyCommonData
        // (blocco texture più sopra). Solo refresh visivo, niente recompute mesh.
        if (ui->glWidget) ui->glWidget->update();
        // L'argomento e' "la GEOMETRIA e' animata": e' il valore da cui
        // applyAnimationState decide setSurfaceAnimating. Sommarci texture e
        // sfondo faceva accendere il clock della superficie per il 't' di un
        // ALTRO modulo -- basta uno sfondo animato (Calabi Yau) e il tasto del
        // dock Script diceva "Stop Parametric" su uno script statico, che
        // premuto rieseguiva lo stesso script senza cambiare nulla.
        // I clock di texture e sfondo NON si perdono: applyAnimationState li
        // ricalcola da se' dai rispettivi codici, ognuno col proprio tempo.
        applyAnimationState(hasTimeVariable(m_scene.surfaceScriptApplied));
    } else {
        if (ui->glWidget) {

            // Applica lo shader personalizzato se presente
            // (qui c'era 'ui->glWidget->setGlobalRenderMode(11);', rimosso: 11 come render
            // mode non e' interpretato dal motore — equivaleva a 0; la update() era
            // gia' coperta sotto. NB: il 11 SALVATO nei preset ray marching e' altra
            // cosa, vedi decodifica '>= 10' in applyCommonData.)
            // !isImplicit: loadCustomShader e' il canale PARAMETRICO e in ray
            // marching non ha alcun effetto utile -- nemmeno quando riesce.
            // Scrive m_customFragmentCode, che la pipeline implicita non legge:
            // quella si costruisce da createImplicitFragmentShader() sopra
            // m_textureCode, popolato altrove (setTextureCode /
            // validateAndApplyImplicitShader). Sono due canali separati.
            //
            // Senza questa guardia la condizione decideva solo sul TESTO della
            // texture: surfaceTextureIsCustom() e' vero appena il codice contiene
            // "return"/"vec3"/"vec4"/"mainImage", cosa vera per quasi ogni
            // texture procedurale. Cosi' un record RM da script finiva per far
            // compilare il proprio scriptCode -- una SDF che opera su 'p' --
            // dentro getRawPosition() del vertex parametrico, dove 'p' non
            // esiste: "ERROR: 'p' : undeclared identifier", una compilazione
            // GLSL completa buttata a ogni caricamento e il log inquinato da
            // errori che sembrano gravi e non lo sono (il codice viene
            // rifiutato e la superficie si vede lo stesso). Misurati 11 record
            // su 142 gia' affetti; ogni nuovo record RM da script con texture
            // attiva sarebbe nato con lo stesso difetto.
            //
            // Di nuovo, se il primo tentativo (APPLICAZIONE TEXTURE SUPERFICIE)
            // non l'ha applicata: dallo SCRIPT del record (il suo slot), non
            // dalla copia applicata, che dopo un tentativo fallito e' vuota. Se
            // non compila nemmeno ora resta tutto com'e': nulla di applicato, lo
            // script nel suo slot, dove lo si puo' correggere. Gia' applicata:
            // niente seconda prova a secco.
            if (!isImplicit && texEnabled) {
                const QString texSrc = surfaceTextureScript();
                if (TextureCode::hasLogic(texSrc) && m_scene.surfaceTextureCode != texSrc)
                    commitSurfaceTextureCode(texSrc);
            }

            ui->glWidget->rebuildShader();

            ui->glWidget->updateSurfaceData();
            ui->glWidget->update();
        }

        // Come il ramo metrico qui sopra: l'argomento e' il tempo della sola
        // GEOMETRIA, non la somma dei tre moduli.
        applyAnimationState(hasTimeVariable(m_scene.surfaceScriptApplied));
    }
    return isScript;
}

void MainWindow::restartRecordClocks(const LibraryItem &data)
{
    // =======================================================
    // RIATTIVAZIONE ANIMAZIONI TEXTURE AL CARICAMENTO
    // =======================================================
    if (ui->glWidget) {
        if (data.textureEnabled && hasTimeVariable(allSurfaceTextureCode())) {
            ui->glWidget->setSurfaceTextureAnimating(true);
        }
        if (data.bgTextureEnabled && hasTimeVariable(m_scene.bgTextureCode)) {
            ui->glWidget->setBackgroundTextureAnimating(true);
        }
    }

    // RUN "ONE-SHOT" DELLA TEXTURE RAY MARCHING: il record e' appena stato
    // applicato e renderizzato, quindi non c'e' nulla da rieseguire -> il tasto
    // deve nascere DISABILITATO, come dopo il caricamento di una texture dalla
    // Library (~7214) e come all'avvio/reset (~3774, ~4815).
    //
    // Serve una riasserzione esplicita: fin qui il flag e' quello della scena
    // di prima, e un record RM deve nascere col Run spento (niente da
    // applicare), o acceso se la sua texture e' animata.
    //
    // In RM la texture NON vive in m_scene.surfaceTextureCode (svuotato a ~11060) ma
    // nei campi dedicati: l'animazione va cercata li', come fa il ramo Library.
    // Con una texture animata il flag resta false -- il tasto e' un Run/Stop
    // legittimo, non un one-shot gia' consumato.
    if (data.isImplicitMode) {
        const bool rmTexAnim = hasTimeVariable(m_scene.rm.texture)
                               || hasTimeVariable(m_scene.rm.displacement);
        m_rmTextureApplied = !rmTexAnim;
    }

    updateMasterButtonState();
}

void MainWindow::startRecordSound()
{
    // 8. AVVIO AUDIO
    if (!m_scene.soundScriptText.isEmpty()) {
        QString audioErr;
        bool audioOk = m_audioController->playFromScript(soundCode(), &audioErr);
        if (!audioOk) {
            if (audioErr.startsWith("MISSING_FILE|")) {
                // FILE AUDIO MANCANTE. Prima il record partiva muto e basta:
                // playMusic faceva un return silenzioso sul file inesistente,
                // mentre le IMMAGINI mancanti hanno sempre avuto il loro avviso.
                // Il record si carica lo stesso, senza suono -- come prima --
                // ma ora l'utente sa perche'.
                // Percorso nell'informativeText: sta su una riga tutta sua e
                // non ha spazi dove spezzarsi (stessa ragione dell'avviso
                // immagini).
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle("Record Sound Not Found");
                box.setText("The sound file used in this record was not found.");
                box.setInformativeText(audioErr.section('|', 1) +
                                       "\n\nThe animation will be loaded without sound.");
                box.exec();
            } else {
                showShaderError("Syntax Error (Sound Script)",
                                                           audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
            }
            // niente return: animazione gia' avviata resta attiva, l'audio no
        }
        if (m_currentScriptMode == ScriptModeSound) {
            // Il testo del pulsante riflette l'esito reale, non la sola presenza di codice
            ui->btnRunCurrentScript->setText(audioOk ? "Stop Sound" : "Run Sound");
        }
    } else {
        m_audioController->stopAll();
        if (m_currentScriptMode == ScriptModeSound) {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
    }

    if (m_currentScriptMode == ScriptModeSound) {
        if (!m_scene.soundScriptText.isEmpty()) {
            ui->btnRunCurrentScript->setText("Stop Sound");
        } else {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
    }
}

void MainWindow::applyMotionExample(LibraryItem data)
{
    // All'uscita la scena e' quella del file: e' il momento pulito.
    SceneCleanGuard sceneCleanGuard{this};
    discardPendingLimitEdits();

    SE_TEXP("record:ENTRATA");

    // IMMAGINI MANCANTI: SI CHIEDE PRIMA DI TOCCARE LA SCENA.
    // L'avviso stava in mezzo alla funzione (dopo il caricamento di texture e
    // sfondo), quindi il popup si apriva su una scena IBRIDA: geometria,
    // equazioni, camera e tab erano gia' quelli del record NUOVO, mentre le
    // texture -- che si applicano piu' sotto -- erano ancora quelle del record
    // PRECEDENTE. exec() e' modale e rientra nel ciclo di eventi: la finestra si
    // ridisegna, e l'utente vedeva quel mezzo record finche' non premeva OK.
    //
    // Qui non e' stato modificato ancora nulla: si guardano solo data e il JSON
    // (TextureCode::resolveImagePath legge il disco e non tocca lo stato), si
    // avvisa, e solo al ritorno da exec() parte il caricamento vero. Sullo
    // schermo resta il record precedente, intatto, per tutta la durata del
    // popup.
    //
    // L'esito della scansione viene passato al codice piu' sotto (che deve
    // comunque togliere il tag //IMG: da texCode/bgCode) invece di essere
    // ricalcolato: la scansione e' UNA, il popup e' UNO.
    const MissingImageScan missingScan = scanRecordForMissingImages(data);
    warnMissingRecordImages(missingScan);

    // LA SCENA DEL FILE, calcolata una volta (con la scansione appena fatta):
    // applyCommonData la assegna in testa, e da qui la leggono sotto-tab e moti.
    const SceneState file = sceneFromItem(data, /*isRecord=*/true, missingScan);

    // CARICAMENTO IN CORSO (m_populatingFields, vedi mainwindow.h): da qui,
    // non solo da applyCommonData piu' sotto. Lo legge soltanto il test degli
    // scenari (nessun campo scritto a segnali vivi durante il load); il Run che
    // il load lancia in coda si riconosce dalla sua origine (RunOrigin::Load).
    m_populatingFields = true;
    struct MotionLoadGuard {
        MainWindow *w;
        ~MotionLoadGuard() { w->m_populatingFields = false; }
    } motionLoadGuard{this};

    // ASPETTO PER-MESH DURANTE IL LOAD.
    // Per tutta la durata del caricamento i setter globali (colore, alpha, luce,
    // renderMode del preset) NON devono essere dirottati sulla mesh selezionata:
    // sono lo stato della superficie, non una scelta dell'utente su una parte.
    // Senza questo, con una mesh ancora attiva dalla sessione precedente quella
    // parte si prendeva i valori globali come propri (tornava verde e solida) e
    // il renderMode finiva per propagarsi a tutte le mesh che ereditano.
    // La selezione viene azzerata qui e reimpostata a 1 da
    // updateMeshSelectorRange quando le parti della nuova superficie esistono.
    // Alla fine il valore PRECEDENTE (vedi la guardia gemella in resetScene).
    const bool prevBypass = ui->glWidget && ui->glWidget->meshAppearanceBypass();
    if (ui->glWidget) {
        ui->glWidget->setMeshAppearanceBypass(true);
        ui->glWidget->setActiveMeshPart(-1);
    }
    struct MeshBypassGuard {
        MainWindow *w;
        bool prev;
        ~MeshBypassGuard() { if (w->ui->glWidget) w->ui->glWidget->setMeshAppearanceBypass(prev); }
    } meshBypassGuard{this, prevBypass};

    stopMotionForRecordLoad();

    // =================================================================
    // 1.5 SANIFICAZIONE E SEPARAZIONE DEI MODI (Parametrico vs Ray Marching)
    // =================================================================
    // Il tab forzato qui sotto e' un cambio di modalita' a tutti gli effetti:
    // se il record e' di modo opposto a quello a schermo, il setCurrentIndex fa
    // scattare applyModeTabReset -> resetScene, che RICHIUDE i rami della
    // Library e ne azzera la selezione. Il risultato era che caricando un
    // record RM con un Parametric a video (e viceversa) l'albero Record si
    // chiudeva e il record appena scelto perdeva il focus. Qui l'utente non sta
    // scartando niente, sta CARICANDO: stesso flag e stessa guardia RAII del
    // cambio tab provocato da una texture incompatibile (~6434).
    // Alzato attorno a ENTRAMBI i rami: il tab da forzare dipende dal record,
    // non da quale ramo dell'if si prende.
    m_texModeSwitchInProgress = true;
    m_modeSwitchSourceTree = ui->treeMotions;
    struct ModeSwitchGuard {
        MainWindow *w;
        ~ModeSwitchGuard() {
            w->m_texModeSwitchInProgress = false;
            w->m_modeSwitchSourceTree = nullptr;
        }
    } modeSwitchGuard{this};

    applyRecordMode(data, file);

    SE_TEXP("record:pre-reset-shader");

    // Reset sicuro di default per disinnescare vecchi shader bloccati. La
    // texture del record di prima esce dal motore E dalla copia applicata
    // (commitSurfaceTextureCode): da qui fino all'applicazione piu' sotto il
    // giudizio delle costanti la vede vuota, e conta lo script del record.
    if (ui->glWidget) {
        ui->glWidget->clearTexture();
        commitSurfaceTextureCode(QString());
        ui->glWidget->setTextureCode(0);
    }

    SE_TEXP("record:post-reset-shader");

    // (I testi dei path li assegna applyCommonData in testa, prima di ogni
    // giudizio sulle costanti: una costante usata solo dal path va tenuta
    // sbloccata, e coi campi del record VECCHIO verrebbe resettata a 1.)

    // 3. Dati Comuni (Surface)
    // NB per la sonda: e' applyCommonData a portare F (e le altre costanti) dal
    // JSON ai campi e all'UBO. Se F cambia fra queste due righe, la resa cambia
    // anche a codice texture IDENTICO.
    SE_TEXP("record:pre-applyCommonData");
    applyCommonData(data, file);
    SE_TEXP("record:post-applyCommonData");

    // Record che da qui in poi E' la scena. Lo legge "Sync Focused Texture" per
    // sapere se il click destro e' caduto sul record caricato: la selezione
    // dell'albero segue il click e non direbbe cosa c'e' davvero a schermo.
    //
    // DOPO applyCommonData, non prima: il caricamento passa da applyModeTabReset
    // -> resetScene (vedi ~13864), che azzera questo campo perche' un cambio tab
    // o un NEW devono farlo. Scrivendolo in cima veniva quindi cancellato dal
    // reset del caricamento stesso, e il comando restava disabilitato su ogni
    // record -- la voce appariva cliccabile ma Qt non emette nulla su un'azione
    // disabilitata.
    m_currentRecordPath = data.filePath;

    applyRecordColors(data);

    applyRecordCamera(data, file);
    applyRecordSurfaceTexture(data, missingScan);
    applyRecordBackgroundTexture(data);
    syncTextureControlsAfterRecordLoad(data);

    applyRecordSpeedsAndAngles(data);

    startRecordCameraMotion(data);

    const bool isScript = runRecordGeometry(data);

    restartRecordClocks(data);

    startRecordSound();

    // 9. HIGHLIGHT AUTOMATICO: SELEZIONA TEXTURE E SUONI NELL'ALBERO
    // A. Sincronizzazione Suoni (Cerca l'audio in TUTTI gli script attivi!)
    const QString fullAudioSearchCode = m_scene.soundScriptText + "\n" + m_scene.surfaceTextureCode
                                        + "\n" + m_scene.bgTextureCode;
    // Il nome vale solo se in scena c'e' davvero un audio: il testo cercato
    // contiene anche i codici delle texture, che non sono un suono.
    LibraryTreeFocus::selectSound(ui->treeSounds, m_libraryManager, fullAudioSearchCode,
                                  m_scene.soundScriptText.trimmed().isEmpty()
                                      ? QString() : m_scene.soundLibName);

    // B. Sincronizzazione Texture: stessa funzione dei cambi di mesh e di modalita'.
    // Qui c'era una COPIA della scelta "quale texture cercare" senza il ramo
    // per-mesh: usava sempre la texture GLOBALE col suo libName. Un record
    // multi-mesh si apre in ambito "Mesh" con la mesh 1 attiva, e per un record
    // script meshPartsChanged scatta SINCRONO dentro questo load (updateSurfaceData):
    // il sync per-mesh aveva gia' evidenziato la texture della fascia, e questo
    // blocco, girando dopo, la sostituiva con quella globale -- che in multi-mesh
    // non si disegna su nessuna fascia. Caso: Hopf Half Tori, mesh 1 con
    // "Quasicrystal GIF in HD", albero su "Squished Image" (la globale del record);
    // tornava giusto solo cambiando mesh e rientrando.
    // Lo stato che syncTextureTreeSelection legge (m_scene.surfaceTextureState,
    // chkBoxTexture, m_scene.bgTextureCode, lineTexture) e' gia' scritto piu' sopra.
    syncTextureTreeSelection();

    updateScriptButtonText();

    // isScript: da runRecordGeometry.
    QTimer::singleShot(20, this, [this, isScript]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(m_scene.steps);
            ui->glWidget->setRaySteps(m_scene.steps);

            if (!isScript) {
                checkAndTriggerMeshUpdate();
            } else {
                ui->glWidget->update(); // Facciamo solo un refresh visivo
            }
        }
    });

    m_textureModified = false;

    // Suggerimento d'uso del record ("hintText"): mostrato in coda al load,
    // quando la scena e' gia' quella nuova. Un record senza la chiave nasconde
    // comunque il messaggio del record precedente.
    //
    // Il record porta ENTRAMBI i messaggi: il suo e quello della sua texture.
    // Va assegnato anche quando e' VUOTO, per scacciare il messaggio della
    // texture caricata a mano prima del record: quella se n'e' andata insieme
    // alla scena. Il record applica la texture INLINE (non passa da
    // handleTextureSelection), quindi qui non lo azzera nessun altro.
    m_currentTextureHintText    = data.textureHintText.trimmed();
    m_currentTextureHintSeconds = data.textureHintSeconds;

    // Il record caricato diventa il riferimento dei campi del dock 4D, come per
    // le superfici. In coda: setRotation4D/setCrossSectionP scrivono poco sopra.
    resetNav4DBaseline();

    m_sceneHintFromSurfacePreset = false;
    showSceneHint(data.hintText, data.hintSeconds);

    // COSTANTI: giudizio finale a scena completa (vedi applySurfaceExample).
    refreshConstants(/*restoreTextOnNegative=*/false);

    // Lettere A-F condivise fra superficie, texture e sfondo del record: in coda
    // all'evento, a scena completa (vedi la funzione).
    warnSharedConstantsOnRecordLoad(data.filePath);

    SE_TEXP("record:USCITA");
}

// LIMITI SPAZIALI X/Y/Z e COSTANTI A..F/S al cambio di sotto-tab implicito.
// Questi controlli sono UNA SOLA istanza fisica condivisa fra "3D" e "Cross
// Section" (decisione esplicita: solo equazione e radio Shell/Solid sono
// separati). Senza questo reset i valori dell'altro sotto-tab restavano addosso
// alla superficie appena aperta: i limiti la TAGLIANO (un box stretto lasciato
// dal 3D poteva far sparire il T^3, che e' piu' grande della sfera unitaria) e
// le costanti la DEFORMANO (il T^3 vuole A=0.9 B=0.3 C=0.2; con A=B=C=1 degenera).
//
// Le costanti NON si toccano qui: chi chiama sa quali vuole
// (loadCrossSectionDefaultSurface scrive subito le sue), e scriverle due volte
// farebbe lampeggiare valori diversi. Questa funzione porta a casa la parte
// davvero comune — i limiti — piu' le costanti che il chiamante non imposta.
// Cambio di sotto-tab implicito (3D <-> Cross Section): carica la superficie di
// default del sotto-tab richiesto e riporta ai default i controlli condivisi.
// E' un METODO e non una lambda nel connect perche' serve da due punti, come
// applyModeTabReset per il tab principale: il cambio vero (currentChanged) e il
// RICLIC sulla linguetta gia' attiva, che currentChanged non emette affatto.
void MainWindow::applyImplicitSubTabReset(int subIndex)
{
    // Cambio rifiutato nel dialogo del lavoro non salvato: la linguetta
    // e' gia' cambiata e sta per tornare indietro da sola, ma la scena
    // non va toccata. Gemello di m_suppressNextModeTabReset.
    if (m_suppressNextSubTabReset) return;

    // DELEGA A resetScene, la stessa che usa il tab principale
    // (applyModeTabReset). Prima questa funzione REPLICAVA a mano la pulizia,
    // e la replica era per forza di cose parziale: nata per il passaggio fra
    // due superfici di DEFAULT (che non hanno moto, suono, sfondo ne' script),
    // si e' rivelata incompleta appena ci si e' arrivati da un RECORD. Ogni
    // residuo scoperto -- path, rotazioni, velocita' 4D, texture di superficie,
    // suono, sfondo, colore di sfondo, script, modalita' del motore -- era un
    // pezzo in piu' da aggiungere, senza mai sapere quale fosse l'ultimo.
    //
    // resetScene(1, true) fa gia' tutto, SOTTO-TAB COMPRESO: il suo ramo
    // implicito legge subTabImplicit e sceglie da se' fra la sfera del "3D" e
    // il T^3 del Cross Section (chiamando loadCrossSectionDefaultSurface).
    // L'unica cosa che non fa e' riportare al default i controlli CONDIVISI fra
    // i due sotto-tab -- limiti X/Y/Z, costanti, Step Relax, Ray Steps -- che
    // qui vanno azzerati perche' cambiare sotto-tab cambia superficie:
    // resetImplicitSharedFields se ne occupa PRIMA, cosi' la superficie di
    // default non trova addosso il box o le costanti dell'altro sotto-tab.
    //
    // subIndex non serve piu': la linguetta e' GIA' cambiata quando questo
    // handler gira (currentChanged), e resetScene la rilegge da subTabImplicit.
    // Resta nella firma perche' e' la signature dello slot.
    Q_UNUSED(subIndex);

    resetImplicitSharedFields();
    resetScene(1, /*loadDefaultSurface=*/true);

    // GATING DEI CONTROLLI. I tasti Omega/Phi/Psi (dock 3D) sono abilitati nel
    // sotto-tab Cross Section e spenti nel "3D", quindi il loro stato dipende da
    // QUALE sotto-tab e' attivo: senza questa chiamata restavano com'erano fino
    // al primo evento che per altri motivi faceva girare updateRenderState.
    updateRenderState();
}

void MainWindow::applyParametricSubTabReset()
{
    // Cambio rifiutato nel dialogo del lavoro non salvato: la linguetta sta
    // per tornare indietro da sola, la scena non va toccata (vedi
    // m_suppressNextSubTabReset, lo stesso schema).
    if (m_suppressNextTubesTabReset) return;

    // DELEGA A resetScene, come il sotto-tab implicito: il suo ramo
    // parametrico legge m_scene.tubesTab (gia' aggiornato da chi chiama) e
    // disegna il toro o il trifoglio di default. I limiti v e w, che su Tubes
    // non si vedono, tornano al default con il resto del dominio.
    resetScene(0, /*loadDefaultSurface=*/true);
    updateRenderState();
}

void MainWindow::resetImplicitSharedFields()
{
    // SPESSORE DEL GUSCIO al default (0.005). E' condiviso fra i due sotto-tab
    // come i limiti e le manopole del marcher: senza, una superficie di default
    // erediterebbe lo spessore tarato su quella precedente -- e su un'equazione
    // scritta a scala naturale un guscio da 0.26 (il valore che serve a Steiner)
    // inghiottirebbe l'intero oggetto.
    setShellThicknessUI(0.005f);

    // MARCHER: "Fast" (sphere tracing storico) e' il default condiviso, come per
    // ogni altra manopola qui. Il sotto-tab Cross Section lo rialza a "Precise"
    // subito dopo, in loadCrossSectionDefaultSurface: stessa sequenza dello Step
    // Relax, che qui torna a 0.4 e li' viene riscritto (oggi con lo stesso 0.4).
    setMarcherUI(false);

    // Limiti spaziali: campi VUOTI = nessun taglio, che e' il default di avvio
    // (vedi il costruttore e il ramo implicito di resetScene, stessa scrittura).
    // A segnali bloccati: textEdited qui significherebbe "l'utente ha modificato
    // la scena" e accenderebbe il Run one-shot / l'avviso lavoro non salvato.
    m_scene.lim.clearSpaceCut();
    showLineFields();
    applySpaceLimits(/*notify=*/false);   // campi vuoti: nessun taglio

    // STEP RELAX e RAY STEPS: sono le due manopole del MARCHER, non della
    // superficie, e come i limiti hanno una sola istanza fisica condivisa fra i
    // due sotto-tab. Vanno riportate al default insieme alla superficie perche'
    // governano se e come la si vede: uno Step Relax basso lasciato dall'altro
    // sotto-tab rallenta la marcia fino a far apparire la forma incompleta, e
    // pochi Ray Steps la troncano a mezz'aria. I valori sono i default di avvio:
    // Step Relax 0.4 e Ray Steps 400 (vedi l'inizializzazione della memoria).
    //
    // Ray Steps usa la COSTANTE 400, non m_lastImplicitSteps: quella variabile e'
    // la memoria del valore corrente, aggiornata quando si esce verso Parametric
    // (resetScene), quindi dopo un giro in Parametric conterrebbe il valore che
    // l'utente aveva impostato -- e riscriverlo non sarebbe un reset. Stessa
    // ragione per cui lo Step Relax scrive 0.4 esplicito e ALLINEA la memoria.
    //
    // Attenzione al doppio ruolo di S: in Ray Marching "lineS"/"sSlider" NON e'
    // la costante S delle equazioni, e' lo Step Relax (lblS viene rietichettato,
    // e lo shader lo legge da u_mathParams.w). Per questo si scrive qui e non
    // insieme ad A..F.
    m_lastImplicitS = 0.4;
    setConstValue(&ConstantTexts::s, m_lastImplicitS);
    refreshConstantSliders();   // Step Relax: 0..1 (vedi syncConstantSliders)

    // VISTA: zoom, pan e orientamento della camera. Come i limiti e le manopole
    // del marcher e' stato GLOBALE, non per-sotto-tab, quindi aprendo l'altro
    // sotto-tab la sua superficie di default si presentava con l'inquadratura
    // lasciata dalla precedente -- tipicamente uno zoom di rotella, che nessun
    // altro percorso qui azzerava.
    //
    // setCameraPos(0,0,4) DOPO resetTransformations, non al posto suo: quel
    // reset PRESERVA deliberatamente una distanza >= 2.5 (il "salvavita zoom",
    // per non risucchiare la camera dentro la figura), quindi da solo lascerebbe
    // in piedi proprio lo zoom che vogliamo togliere. E' lo stesso accoppiamento
    // dei due rami di resetScene.
    if (ui->glWidget) {
        ui->glWidget->resetTransformations();
        ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
    }
    // FOV al default: resetTransformations riporta il campo visivo del MOTORE a
    // 45, ma slider ed etichetta della UI restano dov'erano. applyCameraFov
    // allinea in un colpo membri, label, slider e motore.
    applyCameraFov(45.0f);

    const int kDefaultRaySteps = 400;
    m_lastImplicitSteps = kDefaultRaySteps;   // memoria allineata a cio' che si vede
    setSteps(kDefaultRaySteps);
    if (ui->glWidget) ui->glWidget->setRaySteps(kDefaultRaySteps);
}

// Trasparenza GLOBALE del preset, per superfici e record. Il record si fermava
// al setValue dello slider, che NON emette valueChanged se il valore coincide
// con quello a schermo: l'alpha globale del motore restava quella del preset
// PRECEDENTE, ereditata dalle mesh senza alpha propria e riscritta dal Save
// (trovato dal test di andata e ritorno: record Hopf multi-mesh, 1 o 0.85 a
// seconda di cosa era aperto prima). La superficie aveva gia' il setAlpha
// esplicito; ora e' una sola implementazione.
// Chiamata a MeshBypass acceso (guardie dei due load): va sul globale.
void MainWindow::applyPresetAlpha(float alpha)
{
    // Set PROGRAMMATICO: il flag evita che valueChanged scambi questo per
    // un'interazione utente e faccia scattare il blocco/popup del campo a
    // prodotto. qRound: 0.29f * 100 = 28.99, troncato darebbe 28.
    m_settingAlphaProgrammatic = true;
    ui->alphaSlider->setValue(qRound(alpha * 100.0f));
    m_settingAlphaProgrammatic = false;
    if (ui->glWidget) ui->glWidget->setAlpha(alpha);
}

void MainWindow::loadCrossSectionDefaultSurface()
{
    // T^3 (3-toro, toro-di-tori): S=x^2+y^2+z^2+A^2, M=S+p^2+B^2-C^2,
    // (M^2 + 4(A^2-B^2)(x^2+y^2) - 4B^2(z^2+A^2))^2 = 16A^2(x^2+y^2)(M-2B^2)^2
    // A p=0 NON si riduce al toro 2D standard: e' la sezione dell'oggetto 4D nel
    // suo riferimento. Le rotazioni 4D sono agganciate (vedi %CROSS_SECTION_P%) e
    // questa superficie parte con omega != 0: la sezione di default non e' quella
    // centrale, e' l'inquadratura scelta piu' sotto.
    // Moltiplicata per 0.5 (fuori parentesi): era 0.01, per evitare artefatti
    // di precisione sui valori grandi che l'espressione elevata al quadrato/quarta
    // potenza produce, ma schiacciava anche il gradiente (|grad| ~ 0.013) sotto il
    // clamp 0.2 del marcher, falsando la stima di distanza. Valore scelto a
    // schermo dall'utente insieme allo Step Relax 0.4 (vedi sotto).
    static const QString kT3Equation =
        "0.5*(((x*x+y*y+z*z+p*p+A*A+B*B-C*C)^2 + 4*(A*A-B*B)*(x*x+y*y) - 4*B*B*(z*z+A*A))^2 "
        "- 16*A*A*(x*x+y*y)*(x*x+y*y+z*z+p*p+A*A-B*B-C*C)^2)";

    if (ui->lineEquationCrossSection) {
        setRmText(&ImplicitTexts::crossSection, kT3Equation);
    }

    // Costanti di default del T^3: A=raggio esterno, B=raggio intermedio,
    // C=raggio tubo (A>B>C, condizione di non degenerazione). D/E/F/S non
    // sono usate da questa equazione: restano quelle correnti.
    // B a 0.4 (era 0.3): valori del preset "T3_1" salvato dall'utente, che e'
    // questa stessa superficie con l'inquadratura 4D scelta come default.
    const float valA = 0.9f, valB = 0.4f, valC = 0.2f;
    // STEP RELAX a 0.4, come il default condiviso appena scritto da
    // resetImplicitSharedFields. Era 0.7 ("il T^3 e' stratificato e con passi piu'
    // lunghi la marcia arriva piu' a fondo a parita' di Ray Steps"), ma con passi
    // lunghi l'avvicinamento scavalcava pezzi di superficie anche col marcher
    // Precise: portato a 0.4 dall'utente insieme al fattore di scala 0.5, che
    // correggeva i difetti a schermo. La riga resta esplicita, come Ray Steps
    // sotto: se il default condiviso cambiasse, questa superficie tiene il suo.
    // PRIMA di spingere le costanti nel motore qui sotto: in Ray Marching
    // lineS/sSlider NON e' la costante S dell'equazione ma proprio lo Step
    // Relax, e finisce allo shader con le costanti (u_mathParams.w). Scrivendolo
    // dopo, il motore avrebbe continuato a marciare con 0.4.
    const double kCrossSectionStepRelax = 0.4;
    m_lastImplicitS = kCrossSectionStepRelax;
    setConstValue(&ConstantTexts::s, kCrossSectionStepRelax);

    setConstValue(&ConstantTexts::a, valA);
    setConstValue(&ConstantTexts::b, valB);
    setConstValue(&ConstantTexts::c, valC);

    // Dai campi appena scritti (A/B/C e lo Step Relax in S) e da quelli rimasti
    // com'erano (D/E/F): slider e motore.
    pushConstantsToEngine(/*restoreTextOnNegative=*/false, /*always=*/true);

    // Shell/Solid tornano al default (Shell). applyImplicitShellMode muove
    // entrambe le coppie di radio e scrive il motore: non serve piu' una copia
    // locale per i radio dedicati, ed e' la stessa via del sotto-tab 3D.
    applyImplicitShellMode(true);

    // MARCHER "Precise" (ibrido). Come lo Step Relax qui sopra, e' un default
    // specifico di QUESTA superficie, scritto DOPO resetImplicitSharedFields che
    // riporta la manopola condivisa a "Fast".
    //
    // Non e' una preferenza: il T^3 e' una quartica di quartiche, |grad| ~ 0.013
    // contro il clamp 0.2 del marcher storico, che quindi falsifica la stima di
    // distanza su OGNI raggio. Misurato sulla posa di default (griglia 161x161):
    // 1768 hit su 8105 sono FALSI, a 0.104 unita' di media dalla superficie, e
    // riempiono lo spazio fra i tubi -- e' la "saldatura" che si vedeva in Solid
    // e in Shell, e che spariva appena si accendeva un filo di trasparenza
    // (il ramo trasparente usa un altro marcher, immune per costruzione).
    // Con "Precise" i falsi hit sono ZERO e nessun pixel di superficie si perde.
    // NB: misura fatta col fattore di scala 0.01. Con 0.5 il gradiente e' ~50x
    // piu' grande e il clamp scatta molto meno: i numeri qui sopra non sono stati
    // rimisurati.
    setMarcherUI(true);

    // RAY STEPS a 350. Erano 600 finche' questa superficie partiva TRASPARENTE:
    // li' il raggio attraversa TUTTE le falde del T^3 e i passi si accumulano. Da
    // quando parte OPACA si ferma alla prima falda colpita.
    //
    // Il valore e' MISURATO, non stimato: simulando il loop di marchField (stesso
    // stepRelax 0.7, stesso max(gradLen,0.2), stesso clamp del passo a 0.5, con la
    // rotazione 4D e la p di questa posa) su una griglia 161x161 di raggi, alla
    // posa di default servono al massimo 315 passi; mediana 60, p99 193. 350 e'
    // quel massimo piu' un filo di margine: nessun raggio troncato.
    //
    // NB: e' un TETTO, non un costo. Il loop esce al primo hit (la mediana sta a
    // 60), quindi alzarlo non rallenta i pixel facili e abbassarlo sotto il
    // massimo non fa risparmiare: toglie solo i raggi radenti sui bordi delle
    // anse, che sono esattamente quelli che ne hanno bisogno (a 250 se ne
    // perderebbero 8 su 7882, a 200 sessantuno). Per questo non si scende oltre.
    //
    // Due casi che richiedono di rialzarlo: rimettere la TRASPARENZA, e lo ZOOM
    // ravvicinato (a camera z=2.6 il peggiore sale a 357, a z=2.0 a 486) -- ma lo
    // zoom e' una scelta dell'utente, che ha lo slider per rimediare, mentre
    // l'apertura deve solo essere corretta e leggera.
    // NB: tutti i numeri di questo commento sono misurati col fattore di scala
    // 0.01 e lo Step Relax 0.7. Con 0.5 e 0.4 i passi cambiano (piu' corti per il
    // relax, piu' lunghi e fedeli per il gradiente non piu' clampato): non
    // rimisurati.
    // La riga resta esplicita invece di affidarsi al reset condiviso (400): se
    // quel default cambiasse, questa superficie mantiene il valore tarato su di
    // lei, e m_lastImplicitSteps resta allineata a cio' che si vede.
    const int kCrossSectionRaySteps = 350;
    m_lastImplicitSteps = kCrossSectionRaySteps;
    setSteps(kCrossSectionRaySteps);
    if (ui->glWidget) ui->glWidget->setRaySteps(kCrossSectionRaySteps);

    if (ui->glWidget) {
        ui->glWidget->validateAndApplyImplicitShader(kT3Equation, "", "", /*useCrossSection=*/true);
        ui->glWidget->rebuildShader();
    }

    // OPACA (slider a 100). Fino al 2026-09-17 questa superficie partiva ad alpha
    // 0.75 per far leggere la stratificazione del T^3, ma era il "peccato
    // originale" della modalita': l'unica superficie di default che nasceva
    // trasparente, per giunta su una quartica di quartiche a 600 Ray Steps. Il
    // ramo trasparente del marcher costa MAX_FACES x 3 marchNextLayer x
    // MAX_LAYER_STEPS x 4 map() per pixel, e siccome l'alpha era scritta in modo
    // PROGRAMMATICO non passava da nessuna delle guardie: una texture con rilievo
    // calata qui sopra portava al collasso della GPU (schermo magenta, app
    // bloccata anche su desktop) prima che il watchdog potesse accorgersene.
    // L'inquadratura 4D qui sotto rende leggibile la struttura senza trasparenza.
    // m_settingAlphaProgrammatic resta comunque alzato: e' il contratto di questo
    // slider per le scritture non-utente (vedi setAlphaSliderProgrammatic).
    if (ui->alphaSlider) {
        m_settingAlphaProgrammatic = true;
        ui->alphaSlider->setValue(100);
        m_settingAlphaProgrammatic = false;
        if (ui->glWidget) ui->glWidget->setAlpha(1.0f);
    }

    // POSA DEL PRESET "T3_1" (salvato dall'utente, 2026-09-17). Sostituisce
    // l'inclinazione di cortesia 30/30 del toro parametrico: quella mostrava la
    // sezione frontale, che senza trasparenza si legge male perche' le falde
    // esterne nascondono le interne. Questa inquadratura apre il 3-toro sulle sue
    // anse e ne rende visibile la struttura a superficie PIENA, che e' cio' che
    // permette di togliere la trasparenza qui sopra.
    //
    // Sono TRE pezzi di stato distinti e servono tutti e tre insieme:
    //  - il quaternione 3D dell'OGGETTO (m_rotationQuat -> m_model): l'angolo da
    //    cui si guarda;
    //  - la rotazione 4D omega (W-X): decide QUALE sezione dell'ipersuperficie il
    //    piano della camera taglia (vedi %CROSS_SECTION_P%);
    //  - la traslazione p del piano di sezione: quanto lontano dal centro taglia.
    // Valori presi dal JSON del preset (camera3D.rot_*, angles.omega, crossSectionP).
    //
    // ASSOLUTI, non incrementali (setRotationQuat / setRotation4D / setCrossSectionP,
    // nessun addObjectRotation o moveCrossSectionP): questa funzione e' chiamata
    // anche dall'handler currentChanged di subTabImplicit, che NON azzera nulla, e
    // accumulando ogni andata e ritorno 3D <-> Cross Section avrebbe spostato la
    // superficie un po' piu' in la' ogni volta.
    if (ui->glWidget) {
        ui->glWidget->setRotationQuat(QQuaternion(0.46578074f, -0.23254406f,
                                                  0.32567424f, 0.78924501f));
        ui->glWidget->setRotation4D(0.50000006f, 0.0f, 0.0f);
        ui->glWidget->setCrossSectionP(0.015f);
    }

    // Gli slider A/B/C sono stati scritti sopra con blockSignals (niente
    // textChanged), quindi updateConstantsUIState non ha mai visto che il T^3
    // le usa: senza questa chiamata esplicita restavano nello stato
    // enable/disable ereditato dalla sfera precedente (dove A/B/C non
    // comparivano affatto) -- valore giusto, slider bloccato.
    updateConstantsUIState();
}

// ---- Passi di applyCommonData (la guardia del load resta a lei) ----

void MainWindow::resetEngineBeforePresetLoad(const LibraryItem &d)
{
    m_geoInitialLoad = true;

    // Nuovo stato di partenza: la scala della texture geodetica va rifissata sul
    // dominio di QUESTO preset, cosi' si apre con l'aspetto con cui e' stato
    // salvato. Da qui in poi il riferimento resta fermo, e modificare i limiti
    // taglia la texture invece di comprimerla (vedi setCustomMesh).
    if (ui->glWidget) ui->glWidget->resetGeodesicUvReference();

    onStopClicked();

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet("");
        m_statusLabel->clear();
    }

    onStopClicked();

    if (pathRunning(CameraPaths::Path4D)) onDepartureClicked();
    if (pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();

    // Gli stop qui sopra passano dai tasti e alzano m_userStoppedCameraMotion,
    // ma sono stop PROGRAMMATICI di pre-caricamento: il nuovo preset/record
    // decide da solo il proprio moto (activeMotion in applyMotionExample).
    // Stesso riarmo dei flag gemelli (m_userStoppedSound & c.) al load.
    m_userStoppedCameraMotion = false;

    // Rete di sicurezza: se un path fosse stato fermato altrove senza passare
    // dai tasti (timer già inattivo -> le due righe sopra non scattano), il
    // testo resterebbe congelato su "STOP" pur a tasto disabilitato. Al load
    // di un preset le etichette ripartono comunque da "DEPARTURE"; se il
    // preset ha un path, l'avvio in coda ad applyMotionExample le rimette a
    // "STOP" via onDeparture(3D)Clicked.
    if (ui->btnDeparture) ui->btnDeparture->setText("DEPARTURE");
    if (ui->btnDeparture3D) ui->btnDeparture3D->setText("DEPARTURE");

    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
    }

    // Densità wireframe: se il preset la contiene (hasWireframe) ripristiniamo il numero
    // di linee salvato, così a schermo riappare l'aspetto scelto; altrimenti (preset
    // vecchi senza il campo) torniamo al default per non ereditare quella precedente. In
    // entrambi i casi impostiamo solo wfStepU/V: la geometria verrà (ri)costruita con
    // questi valori quando la nuova mesh è pronta.
    if (ui->glWidget) {
        if (d.hasWireframe)
            ui->glWidget->setWireframeDensity(d.wireframeUStep, d.wireframeVStep);
        else
            ui->glWidget->resetWireframeDensity();
    }

    // ASPETTO PER-MESH: qui le parti non esistono ancora (la griglia si genera
    // piu' avanti, da checkAndTriggerMeshUpdate), percio' l'aspetto resta in
    // sospeso e viene riversato sulle parti da applyPendingMeshAppearance()
    // subito dopo la rigenerazione. Va azzerato SEMPRE, anche quando il preset
    // non lo contiene, o l'aspetto del preset precedente sopravviverebbe.
    m_pendingMeshParts = d.meshParts;
    // IMMAGINI DELLE FASCE: il tag //IMG: dello script di una fascia porta il
    // percorso del dispositivo che ha salvato il record. Stesso Smart Path
    // Resolver della superficie e dello sfondo: se il file non e' li', lo si
    // cerca per nome nella libreria, e il tag prende il percorso trovato (e' il
    // renderer a caricarlo, da quel percorso). Non trovato: il tag resta
    // com'e' e la fascia disegna con l'immagine della superficie.
    for (MeshPart &mp : m_pendingMeshParts) {
        if (!mp.hasCustomTexture) continue;
        const QString raw = GLWidget::imagePathInTextureCode(mp.textureCode);
        if (raw.isEmpty()) continue;
        const QString resolved = TextureCode::resolveImagePath(mp.textureCode);
        if (resolved.isEmpty() || resolved == raw || resolved.startsWith("NOT_FOUND|")) continue;
        mp.textureCode = TextureCode::withImageTagPath(mp.textureCode, resolved);
    }

    // DOMINIO DELL'AMBITO "ALL" del preset. Si applica SUBITO all'engine, non
    // differito come l'aspetto per-mesh: quello deve attendere che le parti
    // esistano (vive dentro le MeshPart), mentre questo e' un campo dell'engine
    // e le parti lo leggono quando vengono generate -- che avviene dopo.
    // Va scritto SEMPRE, anche quando il preset non ne ha uno: in quel caso si
    // azzera, o il taglio di All della superficie PRECEDENTE sopravviverebbe e
    // taglierebbe quella nuova (lo stesso difetto del cutout che restava
    // iniettato fra un preset e l'altro).
    if (ui->glWidget && ui->glWidget->getEngine()) {
        if (d.hasAllDomain)
            ui->glWidget->getEngine()->setAllDomain(d.allUMin, d.allUMax,
                                                    d.allVMin, d.allVMax);
        else
            ui->glWidget->getEngine()->clearAllDomain();
    }
    // Ambito All/Mesh con cui il preset e' stato salvato: deciso qui, applicato
    // da applyPendingMeshAppearance quando le parti esistono.
    m_pendingMeshScopeAll = d.meshScopeAll;
    m_meshScopePending = true;   // da consumare al primo meshPartsChanged

    // SELEZIONE MESH AZZERATA AL LOAD. Una superficie nuova va presentata dalla
    // sua prima mesh: lo spinbox conservava invece l'indice della superficie
    // PRECEDENTE (es. si lasciava la mesh 4 e la successiva si apriva sulla 4,
    // o su un indice clampato al suo numero di parti). Non e' lo stato del
    // preset: e' residuo di quello prima.
    // Va azzerato QUI e non in updateMeshSelectorRange, che al contrario deve
    // PRESERVARE la selezione: gira a ogni rigenerazione della griglia (ogni
    // movimento di costante) e resettare li' riporterebbe alla mesh 1 sotto le
    // dita mentre si edita la mesh 4.
    // Si scrive sullo spinbox e non su setActiveMeshPart perche' qui le parti
    // non esistono ancora (la griglia si genera piu' avanti, da
    // checkAndTriggerMeshUpdate): setActiveMeshPart clampa su getMeshPartCount()
    // e l'indice finirebbe a -1. Lo spinbox e' comunque la fonte da cui
    // applyPendingMeshScope rilegge la parte attiva quando le parti ci sono.
    if (ui->spinMeshSel) {
        QSignalBlocker bMesh(ui->spinMeshSel);
        ui->spinMeshSel->setValue(1);
    }

    // Reset dello stato d'errore geodetico: m_geodesicErrorPending è "appiccicoso"
    // (resettato solo da updateGeodesicMesh in caso di successo). Se il preset
    // precedente è degenerato in una singolarità il flag resta true e farebbe
    // abortire updateGeodesicMesh (riga ~8748) per OGNI preset successivo,
    // bloccando i caricamenti. Caricare un nuovo preset è proprio l'azione che
    // deve ripulirlo, quindi lo azzeriamo qui insieme alle sue proprietà.
    m_geodesicErrorPending = false;
    m_geoErrorShown = false;
    m_geoErrorType = GeoError::None;

    // CRUCIALE per lo sblocco: quando updateGeodesicMesh parte con isInitialLoad
    // disabilita gli update del glWidget (riga ~8798) e li riabilita SOLO in caso
    // di successo (riga ~8931). Se il preset precedente è degenerato in
    // singolarità, updateGeodesicMesh è uscito prima di riabilitarli → il
    // glWidget resta congelato sull'ultima superficie valida e NESSUN preset
    // successivo viene più disegnato (specie quelli non-geodetici, che non
    // ripassano da updateGeodesicMesh). Riabilitiamoli qui, all'inizio di ogni
    // caricamento.
    if (ui->glWidget && !ui->glWidget->updatesEnabled())
        ui->glWidget->setUpdatesEnabled(true);

    // Reset Telecamera e Rotazioni 3D/4D
    ui->glWidget->resetTransformations();
    ui->glWidget->resetTime();
    ui->glWidget->setRotation4D(0.0f, 0.0f, 0.0f);

    // Etichette delle rotazioni: dal motore
    refreshRotationSpeedLabels();
}

void MainWindow::applyPresetRenderAndLimits(const LibraryItem &d, bool isShell)
{
    // Shell/Solid e resa sono gia' nello stato (in testa, choicesFromItem: il
    // renderMode dei preset ray marching e' composito, decine = Shell, unita' =
    // modo base; da NON confondere col vecchio renderMode 11 "parametrico",
    // rimosso). Qui il resto del ramo implicito.
    if (d.isImplicitMode) {
        // SPESSORE DEL GUSCIO. Il motore riceve il valore vero; lo slider si
        // posiziona con la funzione INVERSA della sua curva quadratica, o
        // mostrerebbe una posizione che non corrisponde al valore applicato.
        // Preset senza la chiave -> 0.005 (il parser mette gia' quel default),
        // cioe' l'aspetto con cui sono stati salvati.
        // IN SOLID pero' si forza SEMPRE al minimo, qualunque valore porti il
        // file: lo spessore ha senso solo insieme a un guscio, e alcuni preset
        // Solid portano ancora un thickness non-zero salvato prima che il
        // passaggio a Solid lo azzerasse (vedi updateImplicitRenderMode). Senza
        // questa guardia il caricamento riesumava quel residuo.
        setShellThicknessUI(isShell ? d.shellThickness : 0.005f);

        // MARCHER (Fast/Precise). Per i record senza la chiave il parser ha gia'
        // deciso in base al sotto-tab (3D -> Fast, Cross Section -> Precise), cosi'
        // i record esistenti non vanno risalvati uno per uno: vedi librarymanager.
        setMarcherUI(d.hybridMarcher);
    }

    if (editingBackground()) {
        if (m_currentScriptMode == ScriptModeTexture)
            ui->btnRunCurrentScript->setText("Run Shape Texture");
    }
    // Carico una nuova scena: esco dall'editing sfondo, riporto il bersaglio
    // sulla superficie, e il checkbox Texture -- etichetta e spunta -- lo
    // segue. Prima si riscriveva la sola etichetta: caricando una superficie
    // dal bersaglio Background il checkbox restava acceso (mostrava ancora lo
    // sfondo di prima) su una scena senza texture.
    showSurfaceTarget();

    refreshRenderRadios();

    // La modalita' GLOBALE del preset va scritta esplicitamente nel motore.
    // Prima ci pensava updateRenderState rileggendo i radio, ma ora quella
    // funzione non tratta piu' i radio come sorgente quando una mesh e'
    // selezionata (mostrano LEI, non il globale). Il bypass copre il caso in cui
    // il load avvenga con una parte gia' attiva: e' uno stato del preset, non
    // una scelta dell'utente su quella parte.
    if (ui->glWidget) {
        const bool oldBypass = ui->glWidget->meshAppearanceBypass();
        ui->glWidget->setMeshAppearanceBypass(true);
        ui->glWidget->setGlobalRenderMode(m_scene.renderMode);
        ui->glWidget->setMeshAppearanceBypass(oldBypass);
    }

    // 2. Risoluzione e Limiti (Sovrascrive i default se il preset li contiene)
    setSteps(d.steps);

    ui->glWidget->setResolution(d.steps);
    ui->glWidget->setRaySteps(d.steps);

    updateULimits();
    updateVLimits();
    updateWLimits();

    // Forza immediatamente i limiti sulla GPU cancellando le reminiscenze vecchie
    if (ui->glWidget) {
        ui->glWidget->setRangeX(d.xMin, d.xMax);
        ui->glWidget->setRangeY(d.yMin, d.yMax);
        ui->glWidget->setRangeZ(d.zMin, d.zMax);
    }

    // 3. Costanti: i testi sono gia' quelli del preset (in testa); qui slider e
    // motore, dai valori risolti della cascata.
    pushConstantsToEngine(/*restoreTextOnNegative=*/false, /*always=*/true);

    // Il taglio dai campi appena scritti, con le costanti del preset: registra
    // anche i testi applicati, che lo slider di una costante rivaluta. Il
    // setRange qui sopra (i numeri salvati) resta come base se un campo non
    // fosse valutabile.
    applySpaceLimits(/*notify=*/false);
}

void MainWindow::preparePresetScriptEngine(const LibraryItem &d, bool isScript)
{
    // PRE-ARMO DELLO STATO SCRIPT (fix ergosfera "puntino verde").
    // setEngineMode(ModeImplicit) chiama update(): Qt puo' dispatchare un render()
    // PRIMA che il blocco script piu' sotto (riga ~7755) installi script-mode e il
    // corpo GLSL. Quel render lazy chiamerebbe buildImplicitPipeline() leggendo
    // engine->isScriptModeActive()==false e il VECCHIO m_eqImplicitF (es. il toro
    // del preset precedente): risultato, una superficie sbagliata e collassata
    // con hasInner=0. Installiamo qui lo stato dell'engine cosi' la prima build
    // implicita vede gia' lo script corretto.
    // Il rebuildShader() autorevole piu' sotto resta (ridondante ma innocuo).
    if (d.isImplicitMode && isScript && !d.scriptCode.isEmpty() && ui->glWidget->getEngine()) {
        QString preBody;
        QString preCopy = d.scriptCode;
        QTextStream preStream(&preCopy);
        while (!preStream.atEnd()) {
            QString line = preStream.readLine();
            if (line.contains(":=")) continue;
            preBody.append(line + "\n");
        }
        preBody = GlslTranslator::translateEquation(preBody);
        ui->glWidget->getEngine()->setScriptCodeGLSL(preBody);
        ui->glWidget->getEngine()->setScriptMode(true);
    }

    // CUTOUT: riallinea SEMPRE la sezione //CUTOUT dello script del preset che
    // stiamo caricando — o la azzera se il preset non e' uno script o non ha il
    // blocco. Senza questo, il cutout di un preset script precedente (es. Klein
    // 3D Racing) sopravviveva nel motore e continuava a fare discard su ogni
    // superficie caricata dopo, tagliando strisce dove le sue cutHere(u,v)
    // scattavano sulla nuova geometria (spariva solo riavviando, perche'
    // m_cutoutCode ripartiva vuoto). NB: non passa da onRunScriptClicked, che
    // e' l'unico altro punto che ripuliva il cutout.
    if (ui->glWidget->getEngine()) {
        QString cutoutGlsl;
        // Il cutout esiste solo per il parametrico (il Ray Marching non usa
        // getRawPosition): per gli script impliciti resta comunque azzerato.
        std::vector<MeshPart> meshParts;   // vuoto = una mesh sola
        if (isScript && !d.isImplicitMode && !d.scriptCode.isEmpty()) {
            extractCutoutSection(d.scriptCode, &cutoutGlsl);
            // MULTI-MESH: identico ragionamento del cutout. Senza questo
            // riallineamento le parti di un preset script precedente
            // sopravviverebbero, spezzando in rami una superficie che non li ha.
            extractMeshSections(d.scriptCode, &meshParts);
        }
        // Le copie del campo Meshes (gia' nello stato: assignSceneTexts),
        // prima di fondere l'aspetto salvato per ogni mesh. La sorgente in cui
        // cercare `mesh`: lo script, oppure cio' che va a schermo -- la curva
        // se il preset e' un tubo, altrimenti le equazioni.
        if (!d.isImplicitMode) {
            const EquationTexts &e = m_scene.eq;
            const TubeTexts &t = m_scene.tube;
            const QString meshSource = isScript ? d.scriptCode
                : m_scene.tubesTab
                    ? QStringList{ t.x, t.y, t.z, t.p, t.thickness }.join(QLatin1Char('\n'))
                    : QStringList{ e.x, e.y, e.z, e.p, e.u, e.v, e.w,
                                   e.explicitU, e.explicitV, e.explicitW }.join(QLatin1Char('\n'));
            meshParts = countMeshParts(meshParts, meshSource);
        }
        ui->glWidget->getEngine()->setCutoutCodeGLSL(cutoutGlsl);

        // ASPETTO PER-MESH: le parti appena estratte dallo script portano solo
        // dominio e risoluzione. Se il preset salva anche un aspetto (blocco
        // "meshParts"), va fuso QUI, prima di consegnarle al motore: questa
        // chiamata e' l'ultima che tocca le parti dichiarate, quindi scrivere
        // l'aspetto dopo verrebbe sovrascritto al primo computeMesh().
        for (int k = 0; k < (int)meshParts.size() && k < (int)m_pendingMeshParts.size(); ++k) {
            const MeshPart &src = m_pendingMeshParts[k];
            MeshPart &dst = meshParts[k];
            dst.colorR = src.colorR;
            dst.colorG = src.colorG;
            dst.colorB = src.colorB;
            dst.alpha = src.alpha;
            dst.lightIntensity = src.lightIntensity;
            dst.renderMode = src.renderMode;
            dst.hasCustomRenderMode = src.hasCustomRenderMode;
            dst.wfStepU = src.wfStepU;
            dst.wfStepV = src.wfStepV;
            // TEXTURE PER-MESH: vanno fuse QUI come tutto il resto. Mancavano, e
            // il buco non si vede caricando un preset texturizzato (li'
            // applyPendingMeshAppearance le riscrive tutte), ma caricandone uno
            // SENZA texture dopo uno CHE LE HA: senza queste righe le parti
            // uscivano da qui con hasCustomTexture ancora falso, e la texture
            // della superficie precedente restava nel motore.
            dst.textureCode = src.textureCode;
            dst.textureLibName = src.textureLibName;
            dst.textureEnabled = src.textureEnabled;
            dst.hasCustomTexture = src.hasCustomTexture;
            dst.texCol1R = src.texCol1R;
            dst.texCol1G = src.texCol1G;
            dst.texCol1B = src.texCol1B;
            dst.texCol2R = src.texCol2R;
            dst.texCol2G = src.texCol2G;
            dst.texCol2B = src.texCol2B;
            dst.texZoom = src.texZoom;
            dst.texPanX = src.texPanX;
            dst.texPanY = src.texPanY;
            dst.texRotation = src.texRotation;
            // OROLOGIO DELLA PARTE: **derivato dal codice**, non letto dal file.
            // texAnimating e' stato di RUNTIME e non si salva (come il clock
            // globale, che il load ricalcola da hasTimeVariable); restando al
            // default false, ogni fascia texturizzata nasceva FERMA.
            // Derivarlo invece di serializzarlo fa funzionare anche i preset
            // salvati prima di questa feature.
            dst.texAnimating = dst.hasCustomTexture && dst.textureEnabled
                               && !dst.textureCode.isEmpty()
                               && hasTimeVariable(dst.textureCode);
            dst.timeTex = 0.0f;   // il preset riparte dall'inizio, non da un tempo ereditato

            // DOMINIO PROPRIO salvato nel preset. Va fuso QUI come il resto
            // dell'aspetto: le parti appena estratte dallo script portano il
            // dominio DICHIARATO, e senza questa fusione il taglio scelto
            // dall'utente non verrebbe mai riapplicato al caricamento.
            // Il flag si copia con i valori: e' lui a far sopravvivere il
            // dominio ai successivi START/Run (vedi setMeshParts).
            if (src.hasCustomDomain) {
                dst.uMin = src.uMin;
                dst.uMax = src.uMax;
                dst.vMin = src.vMin;
                dst.vMax = src.vMax;
                dst.hasCustomDomain = true;
            }
        }
        // setMeshParts preserva l'aspetto delle parti GIA' dichiarate (serve a non
        // perderlo quando lo script viene ri-estratto a ogni cambio di costante).
        // Qui pero' stiamo caricando un preset NUOVO: l'aspetto giusto e' quello
        // appena fuso, non quello della superficie precedente. Svuotiamo prima,
        // cosi' non c'e' nulla da preservare e vince il preset.
        //
        // clearALLMeshParts, non clearMeshParts: va svuotata anche la lista
        // GENERATA. Quella sopravvive fino al primo computeMesh(), che qui non
        // e' immediato (arriva piu' tardi da checkAndTriggerMeshUpdate), e in
        // quella finestra il render legge ancora le parti della superficie
        // precedente. Con un dominio scelto a mano il residuo si vedeva: una
        // singola mesh restava tagliata mentre le altre tornavano intere.
        ui->glWidget->getEngine()->clearAllMeshParts();
        ui->glWidget->getEngine()->setMeshParts(meshParts);
    }
}

void MainWindow::applyPresetScript(const LibraryItem &d, bool isShell)
{
    // (Slot e applicato dello script: in testa.)

    // NB: l'uscita dalla modalità metrica (exitMetricScriptMode) avviene più
    // sotto, DOPO che campi ed editor contengono il preset NUOVO: la sua
    // checkParametricDependency -> updateConstantsUIState resetta a 1 le
    // costanti "non usate", e giudicarle sulle equazioni VECCHIE (es. la
    // display map Kruskal, che usa solo A) azzerava le costanti del preset
    // appena scritte (B=0.17 -> 1, superficie deformata).

    // Mappa di visualizzazione (X/Y/Z/P) e composizione/vincoli VUOTI li ha
    // gia' assegnati la testa (equationTextsFromItem): con uno script
    // X/Y/Z/P sono la mappa di uno script metrico -- quella dedicata del
    // file, o le equazioni del file se citano U/V/W -- e composizione e
    // vincoli non sono geometria (Wormhole ha nel file V = B*cos(u), che
    // spegnendo i limiti di v faceva fallire la geodetica).

    if (ui->glWidget) {
        // 1. Spegne m_isCustomMesh interno e azzera le funzioni base
        ui->glWidget->setParametricEquations("0", "0", "0", "0");

        // 2. Svuota la memoria delle variabili composte U, V, W, e il vincolo
        // torna quello neutro (W, equazione vuota) come nei rami a equazioni:
        // dopo un preset a vincolo (l'Oloid ha explicitU) il motore restava in
        // ConstraintU, il vertex dava allo script sempre u = 0 e la superficie
        // collassava (Calabi-Yau vuoto, Symmetrized Double Enneper rotto).
        if (ui->glWidget->getEngine()) {
            ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
            ui->glWidget->getEngine()->setExplicitU("");
            ui->glWidget->getEngine()->setExplicitV("");
            ui->glWidget->getEngine()->setExplicitW("");
        }
    }

    // Ora campi X/Y/Z/P (display map) e script riflettono il preset nuovo:
    // l'uscita dalla modalità metrica può rivalutare le costanti sul testo
    // giusto. Se lo script caricato è metrico la riattiva onRunScriptClicked
    // più sotto (runMetricScript riscrive m_metricScriptBody da sé).
    exitMetricScriptMode();

    if (d.isImplicitMode) {
        // Shell/Solid anche qui: e' lo stesso stato salvato nel renderMode
        // composito (>= 10 = Shell), decodificato in isShell piu' sopra.
        // Mancava solo in questo ramo, ed e' il ramo di quasi tutti i
        // record RM (equazione = "// Controlled by Script").
        applyImplicitShellMode(isShell);

        // Domini, MESH_VISIBLE e suono; i valori no (vedi m_scriptRunFromLoad).
        m_scriptRunFromLoad = true;
        parseAndApplyScriptParams(d.scriptCode);
        m_scriptRunFromLoad = false;

        QString glslBody;
        QString scriptCopy = d.scriptCode;
        QTextStream stream(&scriptCopy);

        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.contains(":=")) continue;
            glslBody.append(line + "\n");
        }
        glslBody = GlslTranslator::translateEquation(glslBody);

        ui->glWidget->getEngine()->setScriptCodeGLSL(glslBody);
        ui->glWidget->getEngine()->setScriptMode(true);
        ui->glWidget->setRaySteps(m_scene.steps);

        // Il campo di uno script implicito e' GLSL grezzo, non valutabile su CPU:
        // la rilevazione "campo a prodotto" (Chain) non si applica. Azzeriamo quel
        // flag come fa validateAndApplyImplicitScript (questo ramo di load NON ci
        // passa) per non ereditare il blocco+opaco da un Chain caricato prima.
        ui->glWidget->clearImplicitIllConditioned();
        // SOLO ANDROID: non potendo contare le facce di uno script su CPU, avvisiamo
        // preventivamente per OGNI script RM (Gyroid1 & co. si tagliano/spariscono
        // con alpha<1 sul budget facce ridotto di Android). Slider USABILE, popup di
        // avviso al primo tocco. Sempre false su desktop/iOS (trasparenza piena).
#if defined(Q_OS_ANDROID)
        ui->glWidget->setImplicitTransparencyWarn(true);
#endif

        ui->glWidget->rebuildShader();


        applyAnimationState(hasTimeVariable(d.scriptCode));
    } else {
        // Esecuzione Parametrica standard. Durante il load di un preset lo
        // stato salvato (limiti, costanti, steps e condizioni iniziali,
        // già ripristinati più sopra, eventualmente modificati dall'utente
        // dopo il Run) ha la precedenza sulle direttive := dello script
        // (vedi m_scriptRunFromLoad).
        m_scriptRunFromLoad = true;
        onRunScriptClicked();
        m_scriptRunFromLoad = false;
    }

    updateScriptButtonText();

    // Editor ed equazione sono stati riempiti a segnali bloccati: nessun
    // textChanged è scattato, quindi ricalcoliamo qui l'abilitazione degli
    // slider A-F/S in base al codice dello script appena caricato.
    updateConstantsUIState();
}

void MainWindow::applyPresetEquations(const LibraryItem &d, bool isShell)
{
    ui->glWidget->setScriptCheck(false);
    // (Slot e applicato dello script vuoti: in testa.)
    // NB: exitMetricScriptMode è spostata più sotto, a campi già popolati:
    // chiamarla QUI (con lineX/Y/Z/P ancora del preset VECCHIO) faceva
    // resettare a 1 dalla sua updateConstantsUIState le costanti che le
    // equazioni vecchie non usano — es. dopo un metric script Kruskal
    // (display map con la sola A) la B=0.17 di H^2xR, appena scritta dal
    // blocco costanti, veniva riportata a 1: p=B*(u+v)+0.5 sforava
    // l'osservatore 4D e la superficie collassava in "lenzuola" giganti.

    if (d.isImplicitMode) {
        m_implicitApplied = false;   // il Run del load la applica
        ui->glWidget->setImplicitEquation(d.implicitEq);

        // Ripristina lo stile Shell o Solid (implementazione condivisa
        // col ramo script e con resetScene).
        applyImplicitShellMode(isShell);
    } else {
        setImplicitMode(false);
        ui->glWidget->setEngineMode(GLWidget::ModeParametric);
    }

    // Uscita dalla modalità metrica A CAMPI NUOVI (vedi nota a inizio ramo):
    // la macchina a stati interna giudica ora le equazioni del preset appena
    // caricato, quindi le costanti realmente usate restano intatte. Se non
    // eravamo in modalità metrica è un no-op (early return), e la
    // checkParametricDependency sotto copre comunque il caso.
    exitMetricScriptMode();

    checkParametricDependency();

    if (!d.explicitU.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
        ui->glWidget->getEngine()->setExplicitU(d.explicitU);
        ui->glWidget->getEngine()->setExplicitV("");
        ui->glWidget->getEngine()->setExplicitW("");
    }
    else if (!d.explicitV.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
        ui->glWidget->getEngine()->setExplicitV(d.explicitV);
        ui->glWidget->getEngine()->setExplicitU("");
        ui->glWidget->getEngine()->setExplicitW("");
    }
    else {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
        ui->glWidget->getEngine()->setExplicitW(d.explicitW);
        ui->glWidget->getEngine()->setExplicitU("");
        ui->glWidget->getEngine()->setExplicitV("");
    }

    ui->glWidget->setParametricEquations(
                GlslTranslator::translateEquation(d.x),
                GlslTranslator::translateEquation(d.y),
                GlslTranslator::translateEquation(d.z),
                GlslTranslator::translateEquation(d.w)
                );
}

void MainWindow::finishPresetLoad(const LibraryItem &d, bool isScript)
{
    // --- 5. RESET E CARICAMENTO PATH ---

    // Comandi dei path ai default: una superficie non li porta, un record li
    // riscrive subito dopo (applyMotionExample).
    resetMotionControls();

    // Path 3D e 4D nel motore, dai testi assegnati in testa (m_scene.path).
    const PathTexts &pt = m_scene.path;
    ui->glWidget->getEngine()->compilePath3DEquations(pt.x3D, pt.y3D, pt.z3D, pt.roll3D);
    ui->glWidget->getEngine()->compilePathEquations(pt.x, pt.y, pt.z, pt.p,
                                                    pt.alpha, pt.beta, pt.gamma);

    // Reset Variabili Tempo Locali
    m_paths->resetTimes();
    m_geoTime = 0.0;
    if (ui->glWidget) ui->glWidget->resetTime();

    updateRenderState();
    // In coda, dopo ogni altro setup: allinea etichette/range degli slider S e Steps
    // al modo del preset. Il tab è stato cambiato a segnali bloccati, quindi il
    // gestore currentChanged (che normalmente lo fa) non è scattato.
    applyModeDependentStepUI(d.isImplicitMode);

    // Il preset appena caricato e' su file: non c'e' lavoro da proteggere finche'
    // l'utente non lo modifica (il momento pulito lo segna SceneCleanGuard
    // all'uscita di applySurfaceExample / applyMotionExample).
    // Riarmato l'avviso cross-dock: la situazione e' cambiata.
    m_warnedEditedDock = OriginDefault;
    m_warnedOrigin     = OriginDefault;
    // A schermo c'e' il preset, non il default: la superficie ora "appartiene"
    // al dock da cui il preset la definisce. E' l'unico punto, con il reset di
    // modalita', in cui la sorgente cambia -- il Run non la sposta.
    // Uno script METRICO (return mat3) non "appartiene" al solo dock Script: la
    // metrica sta li', ma carta e condizioni iniziali del flusso geodetico stanno
    // nelle Equations, e il preset riempie legittimamente entrambi -- vedi i
    // preset di Black Hole e i record di Rotations. Marcarlo OriginScript faceva
    // scattare l'avviso di conflitto sul semplice caricamento. Stesso criterio
    // gia' usato per il routing della mesh geodetica (~riga 9930).
    static const QRegularExpression kMetricReturnRe(R"(\breturn\s+mat3\s*\()");
    const bool isMetricPreset = isScript && d.scriptCode.contains(kMetricReturnRe);
    m_surfaceOrigin = isMetricPreset ? OriginBoth
                    : isScript       ? OriginScript
                                     : OriginEquations;

    updateMasterButtonState();

    // Mobile: se lo stato appena caricato e' RM + trasparenza + displacement,
    // forza opaco e avvisa (falla del record con alpha<1 nel JSON, che aggira i
    // due preventivi interattivi). No-op su desktop e sugli altri casi.
    guardTransparencyOnImplicitLoad();
}

void MainWindow::applyCommonData(LibraryItem d, const SceneState &file)
{
    // CARICAMENTO IN CORSO (m_populatingFields, vedi mainwindow.h). RAII: il
    // flag cade anche sui return anticipati piu' sotto. RIPRISTINO del valore
    // precedente, non "false": applyMotionExample chiama questa funzione avendo
    // gia' alzato il flag, e un azzeramento secco lo spegnerebbe a meta' del
    // caricamento del record.
    const bool wasPopulating = m_populatingFields;
    m_populatingFields = true;
    struct LoadGuard {
        MainWindow *w;
        bool prev;
        ~LoadGuard() { w->m_populatingFields = prev; }
    } loadGuard{this, wasPopulating};

    resetEngineBeforePresetLoad(d);

    // LA SCENA DEL FILE, assegnata qui in blocco e PRIMA di ogni giudizio
    // sulle costanti (assignSceneTexts, la stessa strada del reset). Il
    // giudizio legge equazioni, limiti, path, l'equazione del sotto-tab attivo,
    // texture e sfondo: una costante usata solo da uno di loro va tenuta, e
    // prima li vedeva ancora del preset PRECEDENTE (le texture del record
    // arrivavano dopo). Le direttive di VALORE dello script ("A := 1.2",
    // "u_max := 4*pi") al load non si riapplicano (parseAndApplyScriptParams):
    // la scena e' il file. Il motore la riceve piu' sotto (rami del load,
    // updateU/V/WLimits, applySpaceLimits, pushConstantsToEngine, compilePath*)
    // e, per un record, da applyMotionExample (texture, sfondo, suono);
    // l'applicato della texture lo scrive solo commitSurfaceTextureCode.
    // SOTTO-TAB: quello di un preset IMPLICITO lo mettono i chiamanti, con la
    // stessa regola (choicesFromItem), prima di qui: il commit dell'equazione
    // di un record lo legge, e se l'equazione 4D non compila ripiega sul 3D. Un
    // preset PARAMETRICO torna al 3D qui: nessuno lo toccava, e un preset
    // parametrico aperto dopo un record Cross Section in Solid ereditava
    // sotto-tab e Solid (round-trip, Villarceau Tubes Drift nel passaggio
    // rimescolato). Shell/Solid e resa: stato + vista; il motore le riceve
    // piu' sotto (modalita' globale) e dai rami del load (applyImplicitShellMode).
    // MODALITA' per prima (stato + linguetta a segnali bloccati: il load
    // riscrive la scena da se', il reset del clic qui non va fatto): il
    // giudizio sulle costanti dentro assignSceneTexts la legge. Assegnata piu'
    // sotto, una superficie Ray Marching aperta dopo una parametrica veniva
    // giudicata come parametrica e le sue costanti tornavano a 1 (S a 0).
    setImplicitMode(d.isImplicitMode);
    if (!d.isImplicitMode) setCrossSectionTab(false);
    assignSceneTexts(file);

    // Shell/Solid: gia' nello stato (in testa, choicesFromItem).
    const bool isShell = m_scene.implicitShell;
    applyPresetRenderAndLimits(d, isShell);

    // 4. Logica Caricamento Equazioni vs Script
    bool hasValidEquations = false;
    if (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0") hasValidEquations = true;
    // Le superfici implicite da script non hanno equazioni valide: non forzare hasValidEquations.
    if (d.isImplicitMode && d.scriptCode.isEmpty()) hasValidEquations = true;

    // Salvataggio
    bool isScript = d.isScript || (!d.scriptCode.isEmpty() && !hasValidEquations);

    preparePresetScriptEngine(d, isScript);

    // (La modalita' e' gia' nello stato, in testa.) Il motore la segue.
    ui->glWidget->setEngineMode(d.isImplicitMode ? GLWidget::ModeImplicit
                                                 : GLWidget::ModeParametric);

    m_currentScriptMode = ScriptModeSurface;
    // Testo in lavorazione per una fascia: era della scena di prima.
    m_meshTextureScriptPart = -2;

    updateScriptButtonText();

    if (isScript && !d.scriptCode.isEmpty()) applyPresetScript(d, isShell);
    else                                     applyPresetEquations(d, isShell);

    finishPresetLoad(d, isScript);
}

// Scansione delle immagini mancanti di un record, PRIMA di caricarlo.
//
// Esiste per un motivo solo: l'avviso deve poter partire quando a schermo c'e'
// ancora il record PRECEDENTE, e per farlo serve conoscere i percorsi senza aver
// modificato nulla. Ricalcola quindi texCode/bgCode come fa applyMotionExample
// (dalla struttura `data`, stessa pulizia dei blocchi audio), ma sulle proprie
// copie locali: qui non si tocca ne' la UI ne' il glWidget. NON si replica
// invece lo svuotamento di texCode che il caricamento fa in Ray Marching: li'
// serve a non innescare la pipeline parametrica, qui renderebbe cieca la
// scansione proprio sui record implicit -- dove il codice va in lineTexture ma
// PUO' portare un tag //IMG:, perche' il ramo Library lo antepone allo script
// triplanare quando la si sceglie da un'immagine.
//
// Il risultato viene passato al caricamento, che deve comunque togliere il tag
// //IMG: dal codice: la scansione e' UNA e l'avviso e' UNO.
MainWindow::MissingImageScan MainWindow::scanRecordForMissingImages(const LibraryItem &data)
{
    MissingImageScan scan;

    // Dalla struttura, come il caricamento (che non rilegge piu' il file).
    const bool texEnabled = data.textureEnabled;
    QString texCode = data.textureCode;
    const bool bgTexEnabled = data.bgTextureEnabled;
    QString bgCode = data.bgTextureCode;

    // Le direttive audio vengono tolte dai codici grafici prima del controllo,
    // come nel caricamento: un //MUSIC: in mezzo non c'entra con le immagini,
    // ma la pulizia cambia il trimmed() e quindi l'esito degli isEmpty() qui
    // sotto.
    texCode = stripAudioDirectives(texCode).trimmed();
    bgCode  = stripAudioDirectives(bgCode).trimmed();

    // Manca l'IMMAGINE, non lo script: se il codice porta anche logica
    // procedurale (il mix che il ramo Library compone anteponendo il tag //IMG:
    // a uno script) sopravvive lo script, che rende comunque -- campiona la
    // scacchiera procedurale al posto della foto. Stessa euristica usata dal
    // caricamento per decidere se azzerare il codice o togliere il solo tag.
    auto keepsScript = [](const QString &code) {
        return code.contains("return") || code.contains("vec3")
            || code.contains("vec4")   || code.contains("mainImage");
    };

    if (bgTexEnabled && !bgCode.isEmpty()) {
        const QString bgImg = TextureCode::resolveImagePath(bgCode);
        if (bgImg.startsWith("NOT_FOUND|")) {
            scan.bgMissing = true;
            scan.bgKeptScript = keepsScript(bgCode);
            scan.paths << bgImg.split("|").last();
        }
    }

    // La superficie non e' filtrata da texEnabled: il controllo del caricamento
    // guarda il solo texCode, e cambiarlo qui vorrebbe dire avvisare per un
    // record e non per l'altro.
    Q_UNUSED(texEnabled);
    const QString texImg = TextureCode::resolveImagePath(texCode);
    if (texImg.startsWith("NOT_FOUND|")) {
        scan.surfaceMissing = true;
        scan.surfaceKeptScript = keepsScript(texCode);
        scan.paths << texImg.split("|").last();
    }

    // IMMAGINI DELLE FASCE: stessa risoluzione per nome del load
    // (resetEngineBeforePresetLoad), che non trovandole lascia la fascia
    // sull'immagine della superficie. L'avviso c'era solo per superficie e
    // sfondo: la fascia cambiava aspetto in silenzio.
    for (const MeshPart &mp : data.meshParts) {
        if (!mp.hasCustomTexture) continue;
        const QString img = TextureCode::resolveImagePath(mp.textureCode);
        if (!img.startsWith("NOT_FOUND|")) continue;
        scan.meshKeptScript = scan.meshKeptScript || keepsScript(mp.textureCode);
        scan.paths << img.split("|").last();
    }

    // Lo STESSO file puo' essere citato da superficie e sfondo (es. Clifford
    // Tori Labyrinth), o da piu' fasce: va nominato una volta sola.
    scan.paths.removeDuplicates();
    return scan;
}
