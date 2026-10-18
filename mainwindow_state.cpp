// mainwindow_state.cpp - MainWindow: le sedi dello stato della scena e i loro setter --
// testi di equazioni, campi RM, costanti, limiti e path, Steps, modalita',
// sotto-tab, bersaglio, ambito mesh, slot del dock Script.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


// Equazione implicita del sotto-tab ATTIVO. I due sotto-tab hanno editor
// separati (lineEquation / lineEquationCrossSection) ma un solo motore: ogni
// punto che chiede "qual e' l'equazione a schermo?" deve passare da qui.
// Leggere ui->lineEquation fisso era il difetto che faceva applicare le texture
// RM alla sfera del 3D mentre si era in Cross Section, e che rendeva invisibile
// al rilevamento dell'animazione una 't' scritta nell'equazione 4D.
QString MainWindow::activeImplicitEquationText() const
{
    const bool crossSectionActive = crossSectionTab();
    return crossSectionActive ? m_scene.rm.crossSection : m_scene.rm.equation;
}

void MainWindow::showSurfaceTarget()
{
    setEditTarget(EditTarget::Surface);
    ui->radioSurface->setEnabled(true);
    refreshTextureCheckbox();
}

void MainWindow::setImplicitMode(bool on)
{
    m_scene.implicitMode = on;
    if (!ui->tabModeSelector) return;
    // A segnali bloccati: il gestore currentChanged fa il reset della scena.
    const QSignalBlocker blocker(ui->tabModeSelector);
    ui->tabModeSelector->setCurrentIndex(on ? 1 : 0);
    refreshTubeControls();   // l'altezza del dock dipende dalla pagina visibile
}

void MainWindow::setMeshScopeAll(bool all)
{
    m_scene.meshScopeAll = all;
    if (!ui->radioMeshAll || !ui->radioMeshOne) return;
    // Si accende solo il radio voluto: l'altro lo spegne il QButtonGroup
    // esclusivo, e lo spegnimento emette comunque toggled(false): segnali
    // bloccati su ENTRAMBI. Il loro toggled e' il clic dell'utente.
    QSignalBlocker b1(ui->radioMeshAll), b2(ui->radioMeshOne);
    (all ? ui->radioMeshAll : ui->radioMeshOne)->setChecked(true);
}

void MainWindow::setCrossSectionTab(bool on)
{
    m_scene.crossSectionTab = on;
    if (!ui->subTabImplicit) return;
    // A segnali bloccati: il gestore currentChanged fa il reset del clic
    // (superficie di default), che qui non va fatto.
    const QSignalBlocker blocker(ui->subTabImplicit);
    ui->subTabImplicit->setCurrentIndex(on ? 1 : 0);
}

void MainWindow::setTubesTab(bool on)
{
    m_scene.tubesTab = on;
    if (ui->subTabParametric) {
        // A segnali bloccati: il gestore currentChanged e' quello del clic.
        const QSignalBlocker blocker(ui->subTabParametric);
        ui->subTabParametric->setCurrentWidget(on ? ui->subTabParametricTubes
                                                  : ui->subTabParametricSurface);
    }
    refreshTubeControls();
}

void MainWindow::refreshTubeControls()
{
    // Tutto vale solo mentre Tubes SI VEDE (tab Parametric davanti): col tab
    // Implicit davanti la pagina Parametric torna com'era, perche' anche
    // nascosta conta nell'altezza del dock (vedi sotto).
    const bool shown = tubesShown();

    const QList<QWidget *> limitsVW = { ui->lbla_2, ui->vMinEdit, ui->lblb_2, ui->vMaxEdit,
                                        ui->label_2, ui->wMinEdit, ui->label_3, ui->wMaxEdit };
    for (QWidget *w : limitsVW)
        w->setVisible(!shown);
    ui->panelTubeThickness->setVisible(shown);

    // ALTEZZA. Un QTabWidget e' alto quanto la sua pagina PIU' ALTA, anche se
    // nascosta: la pagina Tubes ereditava l'altezza della Surface (che ha in
    // piu' Constraints/Composition/Geodesic Flow) e quella del tab Implicit
    // (su mobile i due editor delle texture), e la spartiva fra quattro campi
    // -- enormi su desktop, distanziati su iOS dove l'altezza dei campi ha un
    // massimo. Mentre si vede Tubes le pagine nascoste non contano (Ignored in
    // verticale); in ogni altro caso tutto resta Preferred, come prima: le
    // pagine Surface e Implicit non cambiano di un pixel.
    auto fit = [](QWidget *page, bool ignored) {
        page->setSizePolicy(page->sizePolicy().horizontalPolicy(),
                            ignored ? QSizePolicy::Ignored : QSizePolicy::Preferred);
    };
    fit(ui->subTabParametricSurface, shown);
    fit(ui->tabImplicit, shown);
    // Pagine e righe cambiano spesso mentre sono NASCOSTE (col tab Implicit
    // davanti), e cio' che e' nascosto non avvisa i layout che lo contengono:
    // senza questi avvisi il tab Implicit, aperto lasciando il sotto-tab su
    // Tubes, teneva i minimi calcolati prima e risultava piu' basso del solito.
    ui->intervalLimits->updateGeometry();
    ui->panelParametricBottom->updateGeometry();
    ui->subTabParametric->updateGeometry();
    ui->tabModeSelector->updateGeometry();

    // Limiti u (il parametro della curva) accesi su Tubes, quelli di Surface
    // al ritorno: li decide updateConstraintState, solo quando la vista
    // cambia davvero (durante un load gira coi campi ancora del preset prima).
    if (shown != m_tubeControlsShown) {
        m_tubeControlsShown = shown;
        updateScriptButtonText();   // il dock Script parla di curva sui tubi
        updateConstraintState();
        updateMasterButtonState();
    }
}

QWidget *MainWindow::tubeFieldEdit(TubeField field) const
{
    if (field == &TubeTexts::x) return ui->lineTubeX;
    if (field == &TubeTexts::y) return ui->lineTubeY;
    if (field == &TubeTexts::z) return ui->lineTubeZ;
    if (field == &TubeTexts::p) return ui->lineTubeP;
    if (field == &TubeTexts::thickness) return ui->lineTubeThickness;
    return nullptr;
}

void MainWindow::setTubeText(TubeField field, const QString &text)
{
    QWidget *edit = tubeFieldEdit(field);
    if (!edit) { m_scene.tube.*field = text; return; }
    // A segnali bloccati: e' il programma che scrive. m_scene.tube segue dal
    // documento (curva) o lo si scrive qui (spessore, QLineEdit).
    const QSignalBlocker blocker(edit);
    if (auto *pe = qobject_cast<QPlainTextEdit *>(edit)) pe->setPlainText(text);
    else if (auto *le = qobject_cast<QLineEdit *>(edit)) { le->setText(text); m_scene.tube.*field = text; }
    if (field == &TubeTexts::thickness) pushTubeThickness();
}

float MainWindow::tubeThicknessValue(bool *ok) const
{
    bool valid = false;
    const float r = ExpressionParser::evaluateSimple(m_scene.tube.thickness.trimmed(), valid);
    if (ok) *ok = valid;
    return valid ? std::max(r, kTubeThicknessMin) : kTubeThicknessMin;
}

void MainWindow::commitTubeThicknessOnEnter()
{
    if (!tubesShown()) return;
    // Vuoto: torna al default 1, deciso con l'utente (10-07) al posto del
    // popup -- lo slider restava fermo sul valore di prima, disallineato.
    if (m_scene.tube.thickness.trimmed().isEmpty()) {
        setTubeText(&TubeTexts::thickness, QStringLiteral("1"));
        updateMasterButtonState();
        return;
    }
    bool ok = false;
    tubeThicknessValue(&ok);
    if (ok || !tubesShown() || m_constantPopupActive) return;
    m_constantPopupActive = true;
    InputValidator::showInvalidThicknessError(this, m_scene.tube.thickness);
    {
        // Focus di nuovo sul campo, senza un altro editingFinished.
        const QSignalBlocker blocker(ui->lineTubeThickness);
        ui->lineTubeThickness->setFocus();
        ui->lineTubeThickness->selectAll();
    }
    // Reset rimandato a fine ciclo di eventi, come per le costanti.
    QTimer::singleShot(0, this, [this] { m_constantPopupActive = false; });
}

void MainWindow::pushTubeThickness()
{
    bool ok = false;
    const float thickness = tubeThicknessValue(&ok);
    if (!ok) return;   // illeggibile: slider e tubo restano dove sono
    if (QSlider *s = ui->tubeThicknessSlider) {
        // Range come gli slider delle costanti (syncConstantSliders): il
        // massimo standard, o il valore scritto se e' piu' grande; tornati
        // sotto, di nuovo lo standard. Mentre lo slider si trascina il range
        // non si stringe (salterebbe sotto il dito).
        const QSignalBlocker blocker(s);
        const int value = qRound(thickness * 100.0f);
        const int lo = qRound(kTubeThicknessMin * 100.0f);
        int hi = std::max(qRound(kTubeThicknessMax * 100.0f), value);
        if (s->isSliderDown() || s->hasFocus() || s->underMouse()) hi = std::max(hi, s->maximum());
        s->setRange(lo, hi);
        s->setValue(value);
    }
    if (ui->glWidget) ui->glWidget->setTubeRadius(thickness * kTubeRadiusUnit);
}

void MainWindow::setEditTarget(EditTarget target)
{
    m_editTarget = target;
    const bool bg = (target == EditTarget::Background);
    // I radio sono esclusivi, ma si toccano a segnali bloccati: il gestore
    // toggled di radioBackground fa la transizione del dock, che qui non va
    // fatta (la fa chi chiama). Dal gestore del clic sono gia' giusti.
    QAbstractButton *shown = bg ? static_cast<QAbstractButton*>(ui->radioBackground)
                           : target == EditTarget::Border ? static_cast<QAbstractButton*>(ui->radioBorder)
                                                          : static_cast<QAbstractButton*>(ui->radioSurface);
    if (!shown->isChecked()) {
        const QSignalBlocker bBg(ui->radioBackground), bSurf(ui->radioSurface),
                             bBorder(ui->radioBorder);
        shown->setChecked(true);
    }
    // Lo spessore del bordo si regola solo col bersaglio Border.
    const bool border = (target == EditTarget::Border);
    if (ui->lblBorderThickness)   ui->lblBorderThickness->setEnabled(border);
    if (ui->panelBorderThickness) ui->panelBorderThickness->setEnabled(border);
    // Base/Phong/WireFrame mostrano la modalita' del bersaglio.
    refreshRenderRadios();
    // Su cosa agiscono zoom, pan e rotazione 2D (mouse in vista 2D, Save
    // Texture, inquadratura data dai load). Prima la scriveva solo il gestore
    // del clic: dopo un load dal bersaglio Background il dock tornava su
    // Surface e la vista 2D restava sullo sfondo, e quattro punti la
    // forzavano a mano sulla superficie prima di scrivere l'inquadratura.
    if (ui->glWidget) ui->glWidget->setFlatViewTarget(bg ? 1 : target == EditTarget::Border ? 2 : 0);
}

MainWindow::ScriptSlot MainWindow::targetTextureSlot() const
{
    if (editingBackground()) return SlotBackgroundTexture;
    if (editingBorder()) return SlotBorderTexture;
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0) return SlotMeshTexture;
    return SlotSurfaceTexture;
}

MainWindow::ScriptSlot MainWindow::shownScriptSlot() const
{
    switch (m_currentScriptMode) {
    case ScriptModeSurface: return SlotSurface;
    case ScriptModeSound:   return SlotSound;
    case ScriptModeTexture: {
        const ScriptSlot target = targetTextureSlot();
        // In Ray Marching la texture di superficie si scrive nel dock Equations
        // (lineTexture): il dock Script non la mostra e non la fa scrivere.
        if (target == SlotSurfaceTexture && implicitMode())
            return SlotNone;
        return target;
    }
    }
    return SlotNone;
}

void MainWindow::syncMeshTextureSlot() const
{
    // La texture della fascia sta nella MeshPart. Questo slot e' il testo in
    // lavorazione per la fascia selezionata: riparte dalla texture EFFICACE
    // della fascia (propria E accesa -- in wireframe o a checkbox tolto lo
    // script resta conservato ma non si mostra) ogni volta che cambia la fascia
    // o cio' che disegna; finche' restano quelle, tiene cio' che l'utente scrive.
    const int part = ui->glWidget ? ui->glWidget->activeMeshPart() : -1;
    const QString engineCode = part >= 0 ? ui->glWidget->activeMeshEffectiveTextureCode() : QString();
    if (part == m_meshTextureScriptPart && engineCode == m_meshTextureScriptBase) return;
    m_meshTextureScriptPart = part;
    m_meshTextureScriptBase = engineCode;
    m_meshTextureScriptText = engineCode;
}

void MainWindow::syncBorderTextureSlot() const
{
    // La texture EFFICACE del bordo (accesa): spenta, lo script resta nel
    // motore ma non si mostra, come per le fasce.
    const QString engineCode = (ui->glWidget && ui->glWidget->borderTextureEnabled())
                               ? ui->glWidget->borderTextureCode() : QString();
    if (engineCode == m_borderTextureScriptBase) return;
    m_borderTextureScriptBase = engineCode;
    m_borderTextureScriptText = engineCode;
}

QString MainWindow::scriptText(ScriptSlot slot) const
{
    switch (slot) {
    case SlotSurface:           return m_scene.surfaceScriptText;
    case SlotSurfaceTexture:    return m_scene.surfaceTextureScriptText;
    case SlotMeshTexture:       syncMeshTextureSlot(); return m_meshTextureScriptText;
    case SlotBorderTexture:     syncBorderTextureSlot(); return m_borderTextureScriptText;
    case SlotBackgroundTexture: return m_scene.bgTextureScriptText;
    case SlotSound:             return m_scene.soundScriptText;
    case SlotNone:              break;
    }
    return QString();
}

QString MainWindow::appliedScriptText(ScriptSlot slot) const
{
    switch (slot) {
    case SlotSurface:           return m_scene.surfaceScriptApplied;
    case SlotSurfaceTexture:    return m_scene.surfaceTextureCode;
    case SlotMeshTexture:       syncMeshTextureSlot(); return m_meshTextureScriptBase;
    case SlotBorderTexture:     syncBorderTextureSlot(); return m_borderTextureScriptBase;
    case SlotBackgroundTexture: return m_scene.bgTextureCode;
    // Il suono non ha un "applicato" distinto: si suona cio' che e' scritto.
    case SlotSound:             return m_scene.soundScriptText;
    case SlotNone:              break;
    }
    return QString();
}

void MainWindow::setScriptText(ScriptSlot slot, const QString &text)
{
    switch (slot) {
    case SlotSurface:           m_scene.surfaceScriptText = text; break;
    case SlotSurfaceTexture:    m_scene.surfaceTextureScriptText = text; break;
    case SlotMeshTexture:       syncMeshTextureSlot(); m_meshTextureScriptText = text; break;
    case SlotBorderTexture:     syncBorderTextureSlot(); m_borderTextureScriptText = text; break;
    case SlotBackgroundTexture: m_scene.bgTextureScriptText = text; break;
    case SlotSound:             m_scene.soundScriptText = text; break;
    case SlotNone:              return;
    }
    if (slot == shownScriptSlot()) refreshScriptEditor();
}

void MainWindow::refreshScriptEditor()
{
    if (!ui->txtScriptEditor) return;
    const QString text = scriptText(shownScriptSlot());
    if (text == m_scriptEditorText) return;
    m_scriptEditorText = text;
    // A SEGNALI BLOCCATI: e' la vista che si allinea allo stato, non una
    // digitazione (textChanged riscriverebbe lo slot, segnerebbe la texture
    // come modificata a mano e riaccenderebbe i tasti Run).
    const QSignalBlocker blocker(ui->txtScriptEditor);
    ui->txtScriptEditor->setPlainText(text);
}

void MainWindow::clearSurfaceScript()
{
    setScriptText(SlotSurface, QString());
    m_scene.surfaceScriptApplied.clear();
}

// Moto GO (rotazioni superficie/4D) davvero in corsa: il timer attivo non
// basta, perche' fermando il moto il timer puo' restare vivo col tasto
// tornato su "GO". Quello stop oggi e' codificato SOLO nel testo del bottone:
// questo predicato e' l'unico punto autorizzato a leggerlo.
// Il MODULO EQUAZIONI e' in moto: la geometria e' animata da 't' con il suo
// orologio acceso, oppure il flusso geodetico sta integrando. E' esattamente il
// criterio con cui il tasto Run del dock diventa "Stop" -- e NON e' il master
// button, che va su STOP anche quando si muovono solo rotazioni, path, texture
// o audio, con la geometria ferma.
// Sede unica: la usano updateMasterButtonState (testo e abilitazione dei Run) e
// i filtri tastiera (l'Invio applica al volo solo a modulo in moto).
bool MainWindow::mapEquationsMatchSnapshot() const
{
    // Nessuno snapshot: non c'e' un "gia' applicato" con cui confrontarsi.
    if (!m_eqApplied.has_value()) return false;

    // ...composizione e vincoli compresi: sono nello snapshot come X/Y/Z/P.
    const EquationTexts &a = *m_eqApplied;
    return a.x == m_scene.eq.x && a.y == m_scene.eq.y && a.z == m_scene.eq.z && a.p == m_scene.eq.p
        && a.u == m_scene.eq.u && a.v == m_scene.eq.v && a.w == m_scene.eq.w
        && a.explicitU == m_scene.eq.explicitU && a.explicitV == m_scene.eq.explicitV
        && a.explicitW == m_scene.eq.explicitW;
}


 // --- Geometry & Geodesic Flow ---

QPlainTextEdit *MainWindow::equationFieldEdit(EqField f) const
{
    if (f == &EquationTexts::x) return ui->lineX;
    if (f == &EquationTexts::y) return ui->lineY;
    if (f == &EquationTexts::z) return ui->lineZ;
    if (f == &EquationTexts::p) return ui->lineP;
    if (f == &EquationTexts::u) return ui->lineU;
    if (f == &EquationTexts::v) return ui->lineV;
    if (f == &EquationTexts::w) return ui->lineW;
    if (f == &EquationTexts::explicitU) return ui->lineExplicitU;
    if (f == &EquationTexts::explicitV) return ui->lineExplicitV;
    if (f == &EquationTexts::explicitW) return ui->lineExplicitW;
    if (f == &EquationTexts::geoU)  return ui->lnU;
    if (f == &EquationTexts::geoV)  return ui->lnV;
    if (f == &EquationTexts::geoW)  return ui->lnW;
    if (f == &EquationTexts::geoDU) return ui->lndU;
    if (f == &EquationTexts::geoDV) return ui->lndV;
    if (f == &EquationTexts::geoDW) return ui->lndW;
    if (f == &EquationTexts::conform) return ui->lineConform;
    return nullptr;
}

void MainWindow::bindEquationFields()
{
    const EqField fields[] = {
        &EquationTexts::x, &EquationTexts::y, &EquationTexts::z, &EquationTexts::p,
        &EquationTexts::u, &EquationTexts::v, &EquationTexts::w,
        &EquationTexts::explicitU, &EquationTexts::explicitV, &EquationTexts::explicitW,
        &EquationTexts::geoU, &EquationTexts::geoV, &EquationTexts::geoW,
        &EquationTexts::geoDU, &EquationTexts::geoDV, &EquationTexts::geoDW,
        &EquationTexts::conform,
    };
    for (EqField f : fields) {
        QPlainTextEdit *edit = equationFieldEdit(f);
        if (!edit) continue;
        auto sync = [this, f, edit] { m_scene.eq.*f = edit->toPlainText(); };
        // DUE agganci, stesso gestore. textChanged: connesso qui per primo,
        // gira prima di ogni altro gestore del campo (che m_scene.eq lo legge).
        // contentsChanged del DOCUMENTO: arriva anche quando il campo e'
        // scritto a segnali bloccati, perche' il documento e' un altro oggetto.
        connect(edit, &QPlainTextEdit::textChanged, this, sync);
        connect(edit->document(), &QTextDocument::contentsChanged, this, sync);
        sync();
    }
    // La curva del sotto-tab Tubes, allo stesso modo, in m_scene.tube; lo
    // spessore (una riga) come i campi limite: textEdited e textChanged.
    for (TubeField f : { &TubeTexts::x, &TubeTexts::y, &TubeTexts::z, &TubeTexts::p }) {
        auto *edit = qobject_cast<QPlainTextEdit *>(tubeFieldEdit(f));
        if (!edit) continue;
        auto sync = [this, f, edit] { m_scene.tube.*f = edit->toPlainText(); };
        connect(edit, &QPlainTextEdit::textChanged, this, sync);
        connect(edit->document(), &QTextDocument::contentsChanged, this, sync);
        sync();
    }
    if (ui->lineTubeThickness) {
        const auto write = [this](const QString &t) { m_scene.tube.thickness = t; };
        connect(ui->lineTubeThickness, &QLineEdit::textEdited, this, write);
        connect(ui->lineTubeThickness, &QLineEdit::textChanged, this, write);
        m_scene.tube.thickness = ui->lineTubeThickness->text();
    }
    // I campi Ray Marching, allo stesso modo, in m_scene.rm.
    for (RmField f : { &ImplicitTexts::equation, &ImplicitTexts::crossSection,
                       &ImplicitTexts::texture, &ImplicitTexts::displacement }) {
        QPlainTextEdit *edit = implicitFieldEdit(f);
        if (!edit) continue;
        auto sync = [this, f, edit] { m_scene.rm.*f = edit->toPlainText(); };
        connect(edit, &QPlainTextEdit::textChanged, this, sync);
        connect(edit->document(), &QTextDocument::contentsChanged, this, sync);
        sync();
    }
}

QPlainTextEdit *MainWindow::implicitFieldEdit(RmField f) const
{
    if (f == &ImplicitTexts::equation)     return ui->lineEquation;
    if (f == &ImplicitTexts::crossSection) return ui->lineEquationCrossSection;
    if (f == &ImplicitTexts::texture)      return ui->lineTexture;
    if (f == &ImplicitTexts::displacement) return ui->lineVariations;
    return nullptr;
}

void MainWindow::setRmText(RmField field, const QString &text)
{
    QPlainTextEdit *edit = implicitFieldEdit(field);
    if (!edit) { m_scene.rm.*field = text; return; }
    // A segnali bloccati: e' il programma che scrive, non l'utente. m_scene.rm segue
    // dal documento (bindEquationFields).
    const QSignalBlocker blocker(edit);
    edit->setPlainText(text);
}

void MainWindow::setEqText(EqField field, const QString &text)
{
    QPlainTextEdit *edit = equationFieldEdit(field);
    if (!edit) { m_scene.eq.*field = text; return; }
    // A segnali bloccati: e' il programma che scrive, non l'utente. m_scene.eq segue
    // dal documento (bindEquationFields).
    const QSignalBlocker blocker(edit);
    edit->setPlainText(text);
}

const std::array<MainWindow::ConstField, 7> &MainWindow::constantFields()
{
    static const std::array<ConstField, 7> fields = {
        &ConstantTexts::a, &ConstantTexts::b, &ConstantTexts::c, &ConstantTexts::d,
        &ConstantTexts::e, &ConstantTexts::f, &ConstantTexts::s,
    };
    return fields;
}

QString MainWindow::constantName(ConstField field)
{
    static const char *const names[7] = { "A", "B", "C", "D", "E", "F", "S" };
    const auto &fields = constantFields();
    for (int i = 0; i < 7; ++i)
        if (fields[i] == field) return QLatin1String(names[i]);
    return QString();
}

QLineEdit *MainWindow::constantFieldEdit(ConstField field) const
{
    if (field == &ConstantTexts::a) return ui->lineA;
    if (field == &ConstantTexts::b) return ui->lineB;
    if (field == &ConstantTexts::c) return ui->lineC;
    if (field == &ConstantTexts::d) return ui->lineD;
    if (field == &ConstantTexts::e) return ui->lineE;
    if (field == &ConstantTexts::f) return ui->lineF;
    if (field == &ConstantTexts::s) return ui->lineS;
    return nullptr;
}

QSlider *MainWindow::constantSlider(ConstField field) const
{
    if (field == &ConstantTexts::a) return ui->aSlider;
    if (field == &ConstantTexts::b) return ui->bSlider;
    if (field == &ConstantTexts::c) return ui->cSlider;
    if (field == &ConstantTexts::d) return ui->dSlider;
    if (field == &ConstantTexts::e) return ui->eSlider;
    if (field == &ConstantTexts::f) return ui->fSlider;
    if (field == &ConstantTexts::s) return ui->sSlider;
    return nullptr;
}

void MainWindow::bindConstantFields()
{
    for (ConstField f : constantFields()) {
        QLineEdit *edit = constantFieldEdit(f);
        if (!edit) continue;
        // Connesso qui per primo: gira prima di ogni altro gestore del campo,
        // che m_scene.constants lo legge. Le scritture a segnali bloccati non arrivano:
        // passano da setConstText. Anche su textEdited, che precede
        // textChanged (vedi bindLineFields).
        const auto write = [this, f](const QString &t) { m_scene.constants.*f = t; };
        connect(edit, &QLineEdit::textEdited, this, write);
        connect(edit, &QLineEdit::textChanged, this, write);
        m_scene.constants.*f = edit->text();
    }
    m_scene.steps = ui->stepSlider->value();
}

QList<QPair<QLineEdit *, QString *>> MainWindow::lineFieldTable()
{
    return {
        { ui->uMinEdit, &m_scene.lim.uMin }, { ui->uMaxEdit, &m_scene.lim.uMax },
        { ui->vMinEdit, &m_scene.lim.vMin }, { ui->vMaxEdit, &m_scene.lim.vMax },
        { ui->wMinEdit, &m_scene.lim.wMin }, { ui->wMaxEdit, &m_scene.lim.wMax },
        { ui->lineXMin, &m_scene.lim.xMin }, { ui->lineXMax, &m_scene.lim.xMax },
        { ui->lineYMin, &m_scene.lim.yMin }, { ui->lineYMax, &m_scene.lim.yMax },
        { ui->lineZMin, &m_scene.lim.zMin }, { ui->lineZMax, &m_scene.lim.zMax },
        { ui->lineX_P, &m_scene.path.x }, { ui->lineY_P, &m_scene.path.y },
        { ui->lineZ_P, &m_scene.path.z }, { ui->lineP_P, &m_scene.path.p },
        { ui->lineAlpha_P, &m_scene.path.alpha }, { ui->lineBeta_P, &m_scene.path.beta },
        { ui->lineGamma_P, &m_scene.path.gamma },
        { ui->lineX_P3D, &m_scene.path.x3D }, { ui->lineY_P3D, &m_scene.path.y3D },
        { ui->lineZ_P3D, &m_scene.path.z3D }, { ui->lineR_P3D, &m_scene.path.roll3D },
    };
}

void MainWindow::bindLineFields()
{
    for (const auto &f : lineFieldTable()) {
        QLineEdit *edit = f.first;
        QString *state = f.second;
        if (!edit) continue;
        // Connesso qui per primo: gira prima di ogni altro gestore del campo.
        // Anche su textEdited, che Qt emette PRIMA di textChanged: i gestori
        // della digitazione (Run one-shot dei limiti, completezza del dominio)
        // leggono lo stato, e col solo textChanged lo trovavano indietro di un
        // tasto -- u max svuotato accendeva il Run, riscritto lo spegneva.
        const auto write = [state](const QString &t) { *state = t; };
        connect(edit, &QLineEdit::textEdited, this, write);
        connect(edit, &QLineEdit::textChanged, this, write);
        *state = edit->text();
    }
}

QString MainWindow::lineText(const QLineEdit *edit) const
{
    for (const auto &f : const_cast<MainWindow *>(this)->lineFieldTable())
        if (f.first == edit) return *f.second;
    return edit ? edit->text() : QString();
}

void MainWindow::setLineText(QString &state, const QString &text)
{
    state = text;
    for (const auto &f : lineFieldTable()) {
        if (f.second != &state) continue;
        if (QLineEdit *edit = f.first) {
            // A segnali bloccati: e' il programma che scrive, non l'utente.
            const QSignalBlocker blocker(edit);
            edit->setText(text);
        }
        return;
    }
}

void MainWindow::showLineFields()
{
    for (const auto &f : lineFieldTable()) {
        QLineEdit *edit = f.first;
        if (!edit) continue;
        // Testo scritto dal PROGRAMMA, non dall'utente: nessuna conferma
        // pendente. Senza, un preset caricato mentre un limite era in attesa di
        // conferma farebbe validare all'uscita dal campo un testo che l'utente
        // non ha mai scritto (e su un dominio 0/0 uscirebbe un popup a sproposito).
        setUserEditPending(edit, false);
        if (edit->text() == *f.second) continue;
        const QSignalBlocker blocker(edit);
        edit->setText(*f.second);
        edit->setCursorPosition(0);   // l'inizio di un'espressione lunga
    }
    // Cio' che i textChanged dei campi farebbero, una volta sola sullo stato
    // intero: costanti in uso (limiti e path le citano) e tasti Departure.
    updateConstantsUIState();
    checkPathFields();
    checkPath3DFields();
}

// FORMATO "shortest round-trip": la rappresentazione piu' CORTA che, riletta,
// ridia lo STESSO float bit per bit.
//
// Con 'g',12 un limite digitato "1.3" ricompariva come "1.29999995232". Non
// e' un errore di salvataggio: 1.3 non e' rappresentabile in binario e il
// float piu' vicino vale 1.2999999523162842. Chiedendo 12 cifre a un tipo
// che ne porta ~7 si stampano cifre che il valore non ha mai avuto.
//
// Abbassare a 'g',7 NON va bene: perde precisione dove 7 cifre non bastano
// a distinguere due float (2*pi -> "6.283185" rilegge un float DIVERSO).
// Qui si prova da 1 a 9 cifre e ci si ferma alla prima che rilegge identico:
// "1.3" resta "1.3", mentre 6.2831855 conserva tutte le cifre che gli
// servono. Il campo viene RILETTO e riconvertito a float (parseLimitField,
// e il salvataggio rilegge il testo), quindi il round-trip esatto e' la
// condizione che rende la modifica sicura: verificata su 1.992.204 float
// casuali, zero fallimenti.
static QString shortestFloat(float v)
{
    for (int prec = 1; prec <= 9; ++prec) {
        QString s = QString::number(v, 'g', prec);
        if (s.toFloat() != v) continue;
        // 'g' passa all'esponenziale appena l'esponente supera la precisione
        // chiesta: con prec=1 il numero 10 diventerebbe "1e+01". Per le
        // magnitudini normali si preferisce la forma piatta, che e' quella
        // che l'utente aveva digitato.
        if (s.contains('e')) {
            const float a = qAbs(v);
            if (a >= 1e-4f && a < 1e7f) {
                QString flat = QString::number(v, 'f', 9);
                while (flat.contains('.') && (flat.endsWith('0') || flat.endsWith('.')))
                    flat.chop(1);
                if (flat.toFloat() == v) return flat;
            }
        }
        return s;
    }
    return QString::number(v, 'g', 9);
}

MainWindow::LimitTexts MainWindow::limitTextsFromItem(const LibraryItem &d)
{
    // La formula, se il preset ne ha una, vince sul numero: e' l'originale
    // scritto dall'utente, mentre il float e' la sua valutazione al momento
    // del salvataggio (e non seguirebbe piu' le costanti). Preset vecchi:
    // formula vuota -> si usa il numero.
    auto domain = [](float val, const QString &expr) {
        return expr.isEmpty() ? shortestFloat(val) : expr;
    };
    // Taglio x/y/z: il valore di default estremo (+-1000, nessun taglio) lascia
    // il campo vuoto.
    auto cut = [](float val, float defVal, const QString &expr) {
        if (!expr.isEmpty()) return expr;
        if (std::abs(val - defVal) < 0.001f) return QString();
        return QString::number(val, 'g', 6);
    };
    LimitTexts l;
    l.uMin = domain(d.uMin, d.uMinExpr);  l.uMax = domain(d.uMax, d.uMaxExpr);
    l.vMin = domain(d.vMin, d.vMinExpr);  l.vMax = domain(d.vMax, d.vMaxExpr);
    l.wMin = domain(d.wMin, d.wMinExpr);  l.wMax = domain(d.wMax, d.wMaxExpr);
    l.xMin = cut(d.xMin, -1000.0f, d.xMinExpr);  l.xMax = cut(d.xMax, 1000.0f, d.xMaxExpr);
    l.yMin = cut(d.yMin, -1000.0f, d.yMinExpr);  l.yMax = cut(d.yMax, 1000.0f, d.yMaxExpr);
    l.zMin = cut(d.zMin, -1000.0f, d.zMinExpr);  l.zMax = cut(d.zMax, 1000.0f, d.zMaxExpr);

    // Con uno script, un asse SENZA dominio nel file (0/0, nessuna formula:
    // preset scritti a mano, che il dominio lo dichiarano nello script) lo
    // prende dalle direttive "u_max := 4*pi;", col valore calcolato a 12
    // cifre. Un asse con un dominio salvato lo tiene: e' il file, magari
    // ritoccato dall'utente dopo il Run, e le direttive valgono al Run
    // dell'utente (vedi m_scriptRunFromLoad).
    if (loadsFromScript(d)) {
        auto noDomain = [](float lo, float hi, const QString &loExpr, const QString &hiExpr) {
            return lo == 0.0f && hi == 0.0f && loExpr.isEmpty() && hiExpr.isEmpty();
        };
        const bool freeU = noDomain(d.uMin, d.uMax, d.uMinExpr, d.uMaxExpr);
        const bool freeV = noDomain(d.vMin, d.vMax, d.vMinExpr, d.vMaxExpr);
        const bool freeW = noDomain(d.wMin, d.wMax, d.wMinExpr, d.wMaxExpr);
        for (const auto &dv : parseScriptDirectives(d.scriptCode).values) {
            const QString &k = dv.first;
            QString *lim = (freeU && k == QLatin1String("u_min")) ? &l.uMin
                         : (freeU && k == QLatin1String("u_max")) ? &l.uMax
                         : (freeV && k == QLatin1String("v_min")) ? &l.vMin
                         : (freeV && k == QLatin1String("v_max")) ? &l.vMax
                         : (freeW && k == QLatin1String("w_min")) ? &l.wMin
                         : (freeW && k == QLatin1String("w_max")) ? &l.wMax : nullptr;
            if (lim) *lim = QString::number(ExpressionParser::evaluateSimple(dv.second), 'g', 12);
        }
    }
    return l;
}

MainWindow::SceneState MainWindow::sceneFromItem(const LibraryItem &d, bool isRecord)
{
    return sceneFromItem(d, isRecord,
                         isRecord ? scanRecordForMissingImages(d) : MissingImageScan());
}

MainWindow::SceneState MainWindow::sceneFromItem(const LibraryItem &d, bool isRecord,
                                                 const MissingImageScan &scan)
{
    SceneState s;
    s.eq = equationTextsFromItem(d);
    s.rm = implicitTextsFromItem(d);

    s.constants = constantTextsFromItem(d);
    constantDomainsFromItem(d, &s.discreteConsts, &s.minConsts);

    s.lim = limitTextsFromItem(d);
    s.path = pathTextsFromItem(d);
    s.steps = d.steps;
    s.meshCount = d.meshCount;
    // Tubo: la linguetta e i campi del sotto-tab Tubes (vuoti se la scena
    // non e' un tubo: passando a Tubes il reset mette il trifoglio).
    s.tubesTab = d.isTube && !d.isImplicitMode;
    if (s.tubesTab) s.tube = TubeTexts{ d.tubeX, d.tubeY, d.tubeZ, d.tubeP, d.tubeThickness };

    choicesFromItem(d, &s);
    // FOV dei path: lo stesso ripiego dei file vecchi per superfici e record.
    s.fov = qBound(20.0f, resolveSavedFov(d.cameraFov, d.fov3D, d.fov4D), 110.0f);
    // Moti della camera: una superficie riparte dai default, un record porta i suoi.
    if (isRecord) motionFromItem(d, &s);
    textureTextsFromItem(d, isRecord, scan, &s);
    return s;
}

MainWindow::SceneState MainWindow::defaultScene(int index, bool loadDefaultSurface,
                                                bool sameTabRestart) const
{
    SceneState s = m_scene;

    // Comune ai due modi: niente composizione, vincoli, flusso geodetico, path,
    // moti, script della superficie, texture accesa, suono, ancore, domini
    // delle costanti; A..F al default.
    for (EqField f : { &EquationTexts::u, &EquationTexts::v, &EquationTexts::w,
                       &EquationTexts::explicitU, &EquationTexts::explicitV, &EquationTexts::explicitW,
                       &EquationTexts::geoU, &EquationTexts::geoV, &EquationTexts::geoW,
                       &EquationTexts::geoDU, &EquationTexts::geoDV, &EquationTexts::geoDW,
                       &EquationTexts::conform })
        s.eq.*f = QString();
    s.path = PathTexts{};
    s.pathViewMode4D = s.pathViewMode3D = ModeTangential;
    s.pathSpeed3D = s.pathSpeed4D = 10;
    s.lastCameraMotion.clear();
    s.surfaceScriptText.clear();
    s.surfaceScriptApplied.clear();
    // NEW svuota tutti gli slot; il riclic sulla linguetta anche quello della
    // texture, che al CAMBIO di linguetta verso il parametrico sopravvive.
    if (!loadDefaultSurface || sameTabRestart) s.surfaceTextureScriptText.clear();
    s.soundScriptText.clear();
    s.surfaceTextureState = false;
    s.surfaceTextureCode.clear();
    s.bgTextureScriptText.clear();          // forgetBackgroundTexture, in entrambi i modi
    s.bgTextureCode.clear();
    s.textureLibName.clear();
    s.bgTextureLibName.clear();
    s.soundLibName.clear();
    for (ConstField f : { &ConstantTexts::a, &ConstantTexts::b, &ConstantTexts::c,
                          &ConstantTexts::d, &ConstantTexts::e, &ConstantTexts::f })
        s.constants.*f = QStringLiteral("1");
    s.discreteConsts.clear();
    s.minConsts.clear();
    s.meshCount = QStringLiteral("1");
    s.renderMode = 0;
    s.lightingMode4D = 0;
    s.bgColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    s.bgTexColor1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
    s.bgTexColor2 = Qt::black;
    s.fov = 45.0f;

    if (index == 1) {
        // RAY MARCHING. X/Y/Z/P restano (nascosti); u/v/w restano; via il taglio.
        s.implicitMode = true;
        s.rm.equation = loadDefaultSurface ? QStringLiteral("x^2 + y^2 + z^2 = 1.0") : QString();
        if (!loadDefaultSurface) s.rm.crossSection.clear();
        s.rm.texture.clear();
        s.rm.displacement.clear();
        s.surfaceTextureScriptText.clear();
        s.lim.clearSpaceCut();
        s.implicitShell = true;
        s.constants.s = QString::number(m_lastImplicitS <= 0.0 ? 0.4 : m_lastImplicitS);
        s.steps = m_lastImplicitSteps;
        if (loadDefaultSurface && s.crossSectionTab) {
            // La superficie di default del Cross Section: il T^3, che vive su
            // A > B > C (vedi loadCrossSectionDefaultSurface).
            s.rm.crossSection = QStringLiteral(
                "0.5*(((x*x+y*y+z*z+p*p+A*A+B*B-C*C)^2 + 4*(A*A-B*B)*(x*x+y*y) - 4*B*B*(z*z+A*A))^2 "
                "- 16*A*A*(x*x+y*y)*(x*x+y*y+z*z+p*p+A*A-B*B-C*C)^2)");
            s.constants.s = QStringLiteral("0.4");
            s.constants.a = QStringLiteral("0.9");
            s.constants.b = QStringLiteral("0.4");
            s.constants.c = QStringLiteral("0.2");
            s.steps = 350;
        }
    } else {
        // PARAMETRICO. I campi RM restano (nascosti); il dominio torna al default.
        s.implicitMode = false;
        s.lim.resetDomain();
        if (loadDefaultSurface) {
            s.eq.x = QStringLiteral("(0.8 + 0.3*cos(v))*cos(u)");
            s.eq.y = QStringLiteral("(0.8 + 0.3*cos(v))*sin(u)");
            s.eq.z = QStringLiteral("0.3*sin(v)");
            s.eq.p = QStringLiteral("0.0");
        } else {
            s.eq.x.clear();  s.eq.y.clear();  s.eq.z.clear();  s.eq.p.clear();
        }
        s.constants.s = QStringLiteral("0");
        s.steps = m_lastParametricSteps;
        // Il tubo di default, o niente (NEW: curva vuota, spessore di
        // default). E' la superficie che il reset disegna quando la linguetta
        // e' Tubes (resetEngineToParametric).
        if (loadDefaultSurface) s.tube = defaultTubeTexts();
        else { s.tube = TubeTexts{}; s.tube.thickness = QStringLiteral("1"); }
    }
    return s;
}

MainWindow::TubeTexts MainWindow::defaultTubeTexts()
{
    TubeTexts t;
    t.x = QStringLiteral("(sin(u) + 2*sin(2*u))/3");
    t.y = QStringLiteral("(cos(u) - 2*cos(2*u))/3");
    t.z = QStringLiteral("-sin(3*u)/3");
    t.thickness = QStringLiteral("1");
    return t;
}

void MainWindow::assignSceneTexts(const SceneState &s)
{
    for (EqField f : { &EquationTexts::x, &EquationTexts::y, &EquationTexts::z, &EquationTexts::p,
                       &EquationTexts::u, &EquationTexts::v, &EquationTexts::w,
                       &EquationTexts::explicitU, &EquationTexts::explicitV, &EquationTexts::explicitW,
                       &EquationTexts::geoU, &EquationTexts::geoV, &EquationTexts::geoW,
                       &EquationTexts::geoDU, &EquationTexts::geoDV, &EquationTexts::geoDW,
                       &EquationTexts::conform })
        setEqText(f, s.eq.*f);
    setRmText(&ImplicitTexts::equation, s.rm.equation);
    setRmText(&ImplicitTexts::crossSection, s.rm.crossSection);
    setRmText(&ImplicitTexts::displacement, s.rm.displacement);
    setRmText(&ImplicitTexts::texture, s.rm.texture);

    setImplicitShell(s.implicitShell);
    for (TubeField f : { &TubeTexts::x, &TubeTexts::y, &TubeTexts::z, &TubeTexts::p,
                         &TubeTexts::thickness })
        setTubeText(f, s.tube.*f);
    setMeshCountText(s.meshCount);
    setTubesTab(s.tubesTab);
    m_scene.renderMode = s.renderMode;

    m_scene.discreteConsts = s.discreteConsts;
    m_scene.minConsts = s.minConsts;
    setConstTexts(s.constants);

    setScriptText(SlotSurface, s.surfaceScriptText);
    m_scene.surfaceScriptApplied = s.surfaceScriptApplied;
    setScriptText(SlotSurfaceTexture, s.surfaceTextureScriptText);
    setScriptText(SlotBackgroundTexture, s.bgTextureScriptText);
    m_scene.bgTextureCode = s.bgTextureCode;
    m_scene.bgColor = s.bgColor;
    m_scene.bgTexColor1 = s.bgTexColor1;
    m_scene.bgTexColor2 = s.bgTexColor2;
    setScriptText(SlotSound, s.soundScriptText);
    m_scene.surfaceTextureState = s.surfaceTextureState;
    m_scene.textureLibName = s.textureLibName;
    m_scene.bgTextureLibName = s.bgTextureLibName;
    m_scene.soundLibName = s.soundLibName;

    m_scene.lim = s.lim;
    m_scene.path = s.path;
    showLineFields();
}

void MainWindow::textureTextsFromItem(const LibraryItem &d, bool isRecord,
                                      const MissingImageScan &scan, SceneState *s)
{
    if (loadsFromScript(d)) {
        s->surfaceScriptText = d.scriptCode;
        s->surfaceScriptApplied = d.scriptCode;
    }
    // Una superficie: niente texture ne' texture di sfondo; il colore di sfondo
    // il suo (grigio di default se non lo porta), i colori della texture di
    // sfondo al default; il suono e' quello dello script (//MUSIC:,
    // //SOUND_BEGIN..END), se ne ha uno.
    if (!isRecord) {
        s->soundScriptText = extractAudioDirectives(s->surfaceScriptApplied);
        s->bgColor = d.bgColor.isEmpty() ? defaultBackgroundColor() : QColor(d.bgColor);
        s->bgTexColor1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
        s->bgTexColor2 = Qt::black;
        return;
    }
    // Colori dello sfondo: senza la chiave, il grigio di default.
    s->bgColor = d.bgColor.isEmpty() ? defaultBackgroundColor() : QColor(d.bgColor);
    s->bgTexColor1 = QColor(d.bgCol1);
    s->bgTexColor2 = QColor(d.bgCol2);

    // Il suono si estrae dai codici GREZZI di texture e sfondo (anche spento).
    s->soundScriptText = extractAudioDirectives(d.textureCode + "\n" + d.bgTextureCode);

    static const QRegularExpression imgTagRe(QStringLiteral(R"(^\s*//IMG:.*$\n?)"),
                                             QRegularExpression::MultilineOption);
    QString tex = stripAudioDirectives(d.textureCode).trimmed();
    if (scan.surfaceMissing) {
        tex.remove(imgTagRe);
        tex = tex.trimmed();
        if (!scan.surfaceKeptScript) tex.clear();
    }
    if (d.isImplicitMode) s->rm.texture = tex;
    else                  s->surfaceTextureScriptText = tex;
    if (!d.isImplicitMode && d.textureEnabled) s->surfaceTextureCode = tex;

    QString bg = stripAudioDirectives(d.bgTextureCode).trimmed();
    if (!d.bgTextureEnabled) bg.clear();
    if (scan.bgMissing) {
        bg.remove(imgTagRe);
        bg = bg.trimmed();
        if (!scan.bgKeptScript) bg.clear();
    }
    s->bgTextureScriptText = bg;
    s->bgTextureCode = bg;

    s->surfaceTextureState = d.textureEnabled;
    s->textureLibName = d.textureLibName;
    // Sfondo spento: niente codice e niente ancora (forgetBackgroundTexture).
    s->bgTextureLibName = d.bgTextureEnabled ? d.bgLibName : QString();
    s->soundLibName = d.soundLibName;
}

void MainWindow::choicesFromItem(const LibraryItem &d, SceneState *s)
{
    s->implicitMode = d.isImplicitMode;
    s->crossSectionTab = d.isImplicitMode && d.usesCrossSection
                         && !d.crossSectionEq.trimmed().isEmpty() && !loadsFromScript(d);
    // Parametrico: Shell e' il default, e il renderMode non ha la decina.
    s->implicitShell = !d.isImplicitMode || d.renderMode >= 10;
    s->renderMode = (d.isImplicitMode && d.renderMode >= 10) ? d.renderMode - 10 : d.renderMode;
    if (s->renderMode != 1 && s->renderMode != 2) s->renderMode = 0;
}

void MainWindow::motionFromItem(const LibraryItem &d, SceneState *s)
{
    s->pathViewMode4D = static_cast<CameraPathMode>(d.pathMode4D);
    s->pathViewMode3D = static_cast<CameraPathMode>(d.pathMode3D);
    if (d.speedPath3D > 0) s->pathSpeed3D = d.speedPath3D;
    if (d.speedPath4D > 0) s->pathSpeed4D = d.speedPath4D;
    // "none" (salvato a moti fermi) e chiave assente: la cascata del load.
    s->lastCameraMotion = d.activeMotion == QLatin1String("none") ? QString() : d.activeMotion;
}

QString MainWindow::safeImplicitEquation(const QString &eq)
{
    const QString e = eq.trimmed();
    if (e.isEmpty() || !e.contains(QLatin1Char('=')))
        return QStringLiteral("x^2 + y^2 + z^2 = 1.0");
    return e;
}

MainWindow::EquationTexts MainWindow::equationTextsFromItem(const LibraryItem &d)
{
    EquationTexts e;
    const bool fromScript = loadsFromScript(d);
    // Equazioni e flusso geodetico. Il fattore conforme vuoto vale 1.
    e.x = d.x;  e.y = d.y;  e.z = d.z;  e.p = d.w;
    e.u = d.defU;  e.v = d.defV;  e.w = d.defW;
    e.explicitU = d.explicitU;  e.explicitV = d.explicitV;  e.explicitW = d.explicitW;
    e.geoU = d.geoU0;  e.geoV = d.geoV0;  e.geoW = d.geoW0;
    e.geoDU = d.geoDU;  e.geoDV = d.geoDV;  e.geoDW = d.geoDW;
    e.conform = d.geoConform.isEmpty() ? QStringLiteral("1.0") : d.geoConform;
    // Con uno script X/Y/Z/P sono la MAPPA DI VISUALIZZAZIONE: quella dedicata
    // del file (script metrici), o le equazioni del file se citano U/V/W,
    // altrimenti vuote (lo script metrico vi mettera' l'identita').
    if (fromScript) {
        const QString eqMap = d.x + " " + d.y + " " + d.z + " " + d.w;
        if (d.hasMetricMap) {
            e.x = d.metricMapX;  e.y = d.metricMapY;
            e.z = d.metricMapZ;  e.p = d.metricMapP;
        } else if (!eqMap.contains(QRegularExpression(QStringLiteral("\\b[UVW]\\b")))) {
            e.x.clear();  e.y.clear();  e.z.clear();  e.p.clear();
        }
    }
    // Con uno script composizione e vincoli non sono geometria: si svuotano.
    if (fromScript) {
        e.u.clear();  e.v.clear();  e.w.clear();
        e.explicitU.clear();  e.explicitV.clear();  e.explicitW.clear();
    }
    // NB: anche in un preset IMPLICITO X/Y/Z/P vengono dal file. Le superfici
    // Ray Marching vi portano le equazioni del toro di default (e col loro
    // dominio u/v): svuotarle cambiava il Save di 65 superfici. I record
    // impliciti hanno i campi vuoti nel file, e tali restano.
    return e;
}

void MainWindow::constantDomainsFromItem(const LibraryItem &d,
                                         QHash<QString, DiscreteRange> *discrete,
                                         QHash<QString, float> *mins)
{
    discrete->clear();
    mins->clear();
    if (loadsFromScript(d)) {
        const ScriptDirectives sd = parseScriptDirectives(d.scriptCode);
        *discrete = sd.discrete;
        *mins = sd.mins;
        return;
    }
    for (auto it = d.discreteConstants.constBegin(); it != d.discreteConstants.constEnd(); ++it)
        discrete->insert(it.key(), { it->first, it->second });
}

MainWindow::ConstantTexts MainWindow::constantTextsFromItem(const LibraryItem &d)
{
    QHash<QString, DiscreteRange> discrete;
    QHash<QString, float> mins;
    constantDomainsFromItem(d, &discrete, &mins);
    const float values[7] = { d.a, d.b, d.c, d.d, d.e, d.f, d.s };
    ConstantTexts k;
    int i = 0;
    for (ConstField f : constantFields()) {
        // Il formato del load ('g', 6), e lo scatto sul numero COSI' scritto,
        // come applyDiscreteConstants lo rilegge dal campo.
        const QString text = QString::number(values[i++], 'g', 6);
        const float cur = text.toFloat();
        const float snapped = snapConstant(cur, constantName(f), discrete, mins);
        k.*f = qFuzzyCompare(cur, snapped) ? text : QString::number(snapped, 'g', 6);
    }
    return k;
}

float MainWindow::snapConstant(float v, const QString &letter,
                               const QHash<QString, DiscreteRange> &discrete,
                               const QHash<QString, float> &mins)
{
    float target = v;
    const auto it = discrete.constFind(letter);
    if (it != discrete.constEnd()) target = float(qBound(it->lo, qRound(v), it->hi));
    const auto itMin = mins.constFind(letter);
    if (itMin != mins.constEnd() && target < *itMin) target = *itMin;
    return target;
}

bool MainWindow::loadsFromScript(const LibraryItem &d)
{
    const bool hasValidEquations = (!d.x.trimmed().isEmpty() && d.x != QLatin1String("0")
                                    && d.x != QLatin1String("0.0"))
                                   || (d.isImplicitMode && d.scriptCode.isEmpty());
    return !d.scriptCode.isEmpty() && (d.isScript || !hasValidEquations);
}

MainWindow::ImplicitTexts MainWindow::implicitTextsFromItem(const LibraryItem &d)
{
    ImplicitTexts t;
    if (!d.isImplicitMode) return t;
    // Con uno script il campo lo dice. Altrimenti l'equazione del file, e se e'
    // quella del sotto-tab ATTIVO (3D) la versione sicura per il motore. In un
    // preset Cross Section l'equazione 3D non si usa e resta com'e' nel file
    // (spesso vuota): la sfera la scriverebbe il Save.
    if (loadsFromScript(d)) {
        t.equation = QStringLiteral("// Controlled by Script");
    } else {
        t.equation = d.implicitEq.trimmed();
        if (!d.usesCrossSection) t.equation = safeImplicitEquation(t.equation);
    }
    t.crossSection = d.crossSectionEq.trimmed();
    t.displacement = d.displacementCode;
    return t;
}

MainWindow::PathTexts MainWindow::pathTextsFromItem(const LibraryItem &d)
{
    PathTexts p;
    p.x = d.path4D_x;  p.y = d.path4D_y;  p.z = d.path4D_z;  p.p = d.path4D_w;
    p.alpha = d.path4D_alpha;  p.beta = d.path4D_beta;  p.gamma = d.path4D_gamma;
    p.x3D = d.path3D_x;  p.y3D = d.path3D_y;  p.z3D = d.path3D_z;  p.roll3D = d.path3D_roll;
    return p;
}

void MainWindow::setConstText(ConstField field, const QString &text)
{
    m_scene.constants.*field = text;
    if (QLineEdit *edit = constantFieldEdit(field)) {
        // A segnali bloccati: e' il programma che scrive, non l'utente.
        const QSignalBlocker blocker(edit);
        edit->setText(text);
    }
}

void MainWindow::setConstValue(ConstField field, double value)
{
    setConstText(field, QString::number(value, 'g', 6));
}

void MainWindow::setConstTexts(const ConstantTexts &k)
{
    for (ConstField f : constantFields()) setConstText(f, k.*f);
}

void MainWindow::refreshConstantSliders()
{
    syncConstantSliders(resolveCascadeConstants(/*restoreTextOnNegative=*/false));
}

void MainWindow::setSteps(int steps)
{
    m_scene.steps = steps;
    // Vista: slider (il suo range contiene il valore, o setValue lo
    // taglierebbe in silenzio) e campo, a segnali bloccati.
    const QSignalBlocker bs(ui->stepSlider), bl(ui->lineSteps);
    pushStepsRange(steps);
    ui->stepSlider->setValue(steps);
    ui->lineSteps->setText(QString::number(steps));
}

void MainWindow::pushStepsRange(int steps)
{
    QSlider *s = ui->stepSlider;
    const QSignalBlocker blocker(s);
    int hi = std::max(kStepsSliderMax, steps);
    if (s->isSliderDown()) hi = std::max(hi, s->maximum());
    if (s->minimum() > steps) s->setMinimum(steps);
    s->setMaximum(hi);
}

int MainWindow::stepsLimit() const
{
    if (implicitMode()) return GLWidget::kRayStepsMax;
    // Le parti della figura: ciascuna fino a (Steps + 1)^2 punti.
    int parts = 1;
    if (ui->glWidget && ui->glWidget->getEngine())
        parts = std::max(1, (int)ui->glWidget->getEngine()->getMeshParts().size());
    const int fit = int(std::sqrt(kStepsPointBudget / parts)) - 1;
    return std::max(kStepsSliderMax, fit);
}

QString MainWindow::activeEquationsText() const
{
    if (!m_eqApplied) return QString();
    const EquationTexts &a = *m_eqApplied;
    return QStringList{ a.x, a.y, a.z, a.p, a.u, a.v, a.w,
                        a.explicitU, a.explicitV, a.explicitW,
                        a.geoU, a.geoV, a.geoW, a.geoDU, a.geoDV, a.geoDW,
                        a.conform }.join(QLatin1Char(' '));
}

void MainWindow::snapshotActiveEquations() {
    // Composizione (U, V, W in funzione di u, v, w) e vincoli espliciti fanno
    // parte di cio' che e' a schermo quanto X/Y/Z/P, e il flusso geodetico pure:
    // fuori dallo snapshot, il commit di servizio li prendeva dai campi e
    // applicava una composizione o un vincolo ancora in corso di scrittura
    // sulle equazioni di prima.
    m_eqApplied = m_scene.eq;
}
