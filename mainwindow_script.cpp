// mainwindow_script.cpp - MainWindow: dock Script -- Run degli script di superficie,
// texture e suono, sezioni multi-mesh, script metrici, audio.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

// Dock Script. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupScriptDock()
{
    // =========================================================================
    // 9. SCRIPTING & TEXTURE DOCK
    // =========================================================================
    connect(ui->btnScriptMode, &QPushButton::clicked, this, &MainWindow::onToggleScriptMode);
    connect(ui->btnRunCurrentScript, &QPushButton::clicked, this, &MainWindow::onRunCurrentScript);
    connect(ui->btnSaveScript, &QPushButton::clicked, this, &MainWindow::onSaveScriptClicked);

    m_currentScriptMode = ScriptModeSurface;
    updateScriptButtonText();

    ui->btnFlatPreview->setText("2D View");
    ui->btnFlatPreview->setEnabled(false);

    connect(ui->btnFlatPreview, &QPushButton::toggled, this, [this](bool checked){
        if (checked) {
            ui->btnFlatPreview->setText("3D Shape");
            ui->alphaSlider->setEnabled(false); // Blocchiamo solo la trasparenza
        } else {
            updateFlatPreviewButton();
            ui->alphaSlider->setEnabled(true);
        }

        ui->glWidget->setFlatView(checked);
        ui->glWidget->update();
    });

    updateFlatPreviewButton();

    ui->txtScriptEditor->setPlaceholderText("Write GLSL code for custom texture.\nExample: return vec4(0.2 * u - 0.5, 0.2 * v - 0.5, 0.2 * sin(u * v), 1.0);");
}



// ==========================================================
// SCRIPTING ENGINE
// ==========================================================

void MainWindow::onToggleScriptMode()
{
    // Il testo scritto e' gia' nel suo slot (l'editor ne e' la vista): si passa
    // al modulo successivo (0 -> 1 -> 2 -> 0) e la vista lo segue, coi tasti.
    m_currentScriptMode = static_cast<ScriptMode>((m_currentScriptMode + 1) % 3);
    updateScriptButtonText();
}

void MainWindow::onRunCurrentScript()
{
    // Il Run del dock Script applica la superficie tanto quanto quello del dock
    // Equations, ma NON passa da onStartClicked: senza questo guard un Run
    // riuscito qui non marcava nulla, e il reset buttava via lo script senza
    // chiedere niente. Armato solo per lo script di SUPERFICIE -- un Run di
    // texture o suono non porta a schermo la geometria (stesso criterio di
    // noteSceneEdited, che conta txtScriptEditor come dock Script solo qui).
    RunOutcomeGuard runOutcomeGuard(this, m_currentScriptMode == ScriptModeSurface);

    // 1. Il testo del modulo che il dock mostra
    QString currentText = scriptText(shownScriptSlot());

    // --- CONTROLLO DI SICUREZZA (WRONG MODE BLOCK) DELEGATO ---
    // Passiamo l'enum castato a int
    if (!InputValidator::validateScriptModeContext(this, static_cast<int>(m_currentScriptMode), currentText)) {
        // Lo script del modulo sbagliato si scarta: lo slot mostrato si svuota
        // (a segnali bloccati, l'editor e' la sua vista) e tasti e costanti si
        // riallineano, come dopo una cancellazione a mano.
        setScriptText(shownScriptSlot(), QString());
        updateScriptButtonText();
        updateConstantsUIState();
        updateMasterButtonState();
        return;
    }

    // 2. Salva e avvia in base alla modalità attuale
    if (m_currentScriptMode == ScriptModeSurface) {
        if (ui->btnRunCurrentScript->text().startsWith("Stop")) {
            // Ferma SOLO il modulo geometria: orologio shader della superficie
            // e, per gli script metrici, il flusso geodetico (m_geoAnimTimer).
            // Timer condiviso col dock Equations: performEquationsStop() ferma
            // entrambe le sorgenti di 't' e riallinea i due tasti a "Run".
            runOutcomeGuard.surfaceApplied = false;   // uno Stop non applica nulla
            performEquationsStop();
            return;
        }

        // Nessun cambio automatico di modo: il Run rispetta SEMPRE la tab in cui
        // sei. La vecchia euristica sul "DNA" del testo (una 'p' isolata o
        // 'length(' -> passa a Ray Marching) faceva scattare cambi di tab
        // improvvisi e non voluti mentre si lavorava nel dock Equations. Se lo
        // script non e' compatibile con la modalita' corrente, ci pensa la
        // validazione qui sotto (validateImplicitScriptReturn nel ramo RM, e
        // l'equivalente parametrico) a segnalarlo, senza spostare l'utente.

        // BIFORCAZIONE TRA RAY MARCHING (IMPLICIT) E PARAMETRIC
        m_scene.surfaceScriptApplied = currentText;

        if (implicitMode()) {
            // --- RAMO 1: SCRIPT IMPLICITO (RAY MARCHING) MULTI-RIGA ---

            // 1. PRIMA LINEA DI DIFESA: Sanity Check Testuale Minimalista
            QString cleanCode = stripCodeComments(currentText);

            // DELEGA LA VALIDAZIONE E BLOCCA SE FALLISCE
            if (!InputValidator::validateImplicitScriptReturn(this, cleanCode)) {
                return;
            }

            QString glslBody;
            QString scriptCopy = currentText;
            QTextStream stream(&scriptCopy);
            while (!stream.atEnd()) {
                QString line = stream.readLine();
                if (line.contains(":=")) continue;
                glslBody.append(line + "\n");
            }
            glslBody = GlslTranslator::translateEquation(glslBody);

            ui->glWidget->setRaySteps(m_scene.steps);

            // 2. SECONDA LINEA DI DIFESA: dry-run del fragment implicito.
            if (!ui->glWidget->validateAndApplyImplicitScript(glslBody)) {
                showShaderError("Script Compilation Error",
                                                           ui->glWidget->getShaderError());

                return;
            }

            parseAndApplyScriptParams(currentText, false);

            setEqText(&EquationTexts::x, QString());
            setEqText(&EquationTexts::y, QString());
            setEqText(&EquationTexts::z, QString());
            setEqText(&EquationTexts::p, QString());

            // TUTTO OK: ABILITIAMO IL TASTO SALVA
            ui->btnSaveScript->setEnabled(true);

            setRmText(&ImplicitTexts::equation, "// Controlled by Script");

            m_masterStopped = false;
            m_userStoppedGeomClock = false;   // run esplicito del modulo geometria
            if (ui->glWidget) {
                ui->glWidget->setSurfaceAnimating(hasTimeVariable(currentText));
            }
            updateMasterButtonState();
            ui->glWidget->update();

        } else {
            // --- RAMO 2: SCRIPT PARAMETRICO STANDARD ---
            onRunScriptClicked();
        }

    } else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->btnRunCurrentScript->text().startsWith("Stop")) {
            // BORDO: lo Stop ferma il SOLO orologio della texture del bordo, e
            // il gate lo protegge dai ricalcoli (applyAnimationState).
            if (editingBorder() && ui->glWidget) {
                ui->glWidget->setBorderTextureAnimating(false);
                m_userStoppedBorderTexClock = true;
                updateMasterButtonState();
                updateScriptButtonText();
                return;
            }
            // AMBITO "MESH": lo Stop ferma SOLO la texture della mesh
            // selezionata, come colore/alpha/luce agiscono sulla sola parte.
            // Non si tocca il clock globale ne' m_userStoppedTexClock: le altre
            // mesh e la texture di superficie devono continuare a girare.
            if (!editingBackground() && ui->glWidget
                && ui->glWidget->activeMeshPart() >= 0) {
                ui->glWidget->setActiveMeshTextureAnimating(false);
                // Stop ESPLICITO su una fascia: protegge gli orologi per-mesh dai
                // ricalcoli di applyAnimationState (commit di equazione, load,
                // toggle sfondo), che altrimenti li riaccenderebbero tutti al
                // primo giro. Lo riarma il master Start.
                m_userStoppedMeshTexClock = true;
                updateMasterButtonState();
                updateScriptButtonText();
                return;
            }

            // Stop esplicito del clock texture/sfondo: il flag lo tiene fermo
            // anche attraverso i ricalcoli globali (vedi applyAnimationState).
            if (editingBackground()) m_userStoppedBgClock = true;
            else                                  m_userStoppedTexClock = true;
            if (ui->glWidget) {
                if (editingBackground())
                    ui->glWidget->setBackgroundTextureAnimating(false);
                else {
                    // AMBITO "ALL": si ferma la texture di SUPERFICIE, che e'
                    // quella che l'ambito All comanda. Le fasce hanno una
                    // texture PROPRIA e un proprio orologio: fermarle da qui
                    // (setAllMeshTexturesAnimating) significava che lo Stop in
                    // All spegneva il moto delle mesh mentre il tasto continuava
                    // a descrivere lo stato dell'altro ambito -- il "premo su
                    // All e cambia il moto della mesh, non l'aspetto".
                    // Il master resta la via per fermare TUTTO insieme.
                    ui->glWidget->setSurfaceTextureAnimating(false);
                }
            }
            updateMasterButtonState();   // riallinea i pulsanti -> "Run ..."
            return;
        }

        // RUN COL BERSAGLIO BORDER sulla texture gia' applicata e animata: solo
        // il riavvio del suo orologio, senza ricompilare lo shader.
        if (editingBorder() && ui->glWidget
            && !ui->glWidget->isBorderTextureAnimating()
            && ui->glWidget->borderTextureShown()
            && currentText.trimmed() == ui->glWidget->borderTextureCode().trimmed()
            && hasTimeVariable(ui->glWidget->borderTextureCode())) {
            ui->glWidget->setBorderTextureAnimating(true);
            m_userStoppedBorderTexClock = false;
            updateMasterButtonState();
            updateScriptButtonText();
            return;
        }

        // RUN IN AMBITO "MESH" con la texture della parte gia' applicata: e' un
        // riavvio dell'orologio della SOLA parte, non una riapplicazione dello
        // script (che passerebbe da onApplyTextureScriptClicked e ricompilerebbe
        // lo shader per tutti). Solo se lo script della parte e' animato: su una
        // texture statica il Run resta la riapplicazione di sempre.
        if (!editingBackground() && ui->glWidget
            && ui->glWidget->activeMeshPart() >= 0
            && !ui->glWidget->isActiveMeshTextureAnimating()
            && ui->glWidget->activeMeshTextureActive()
            && currentText.trimmed() == ui->glWidget->activeMeshTextureCode().trimmed()
            && hasTimeVariable(ui->glWidget->activeMeshTextureCode())) {
            ui->glWidget->setActiveMeshTextureAnimating(true);
            // Riavvio esplicito di UNA fascia: il gate va riarmato, o resterebbe
            // alzato per uno stop che l'utente ha appena annullato e terrebbe
            // fuori dai ricalcoli anche le altre mesh.
            m_userStoppedMeshTexClock = false;
            updateMasterButtonState();
            updateScriptButtonText();
            return;
        }

        // Lo script e' gia' nello slot del bersaglio (sfondo, fascia o
        // superficie): onApplyTextureScriptClicked lo legge da li'.
        onApplyTextureScriptClicked();

    } else if (m_currentScriptMode == ScriptModeSound) {
        // Il suono resta nel suo slot, cosi' com'e' scritto: la forma da suonare
        // (GLSL nudo avvolto nei marcatori) la da' soundCode(). Qui prima lo si
        // componeva con lo slot della texture dentro m_scene.surfaceTextureCode /
        // m_scene.bgTextureCode: uno script texture in sospeso risultava applicato.
        onRunSoundClicked();
    }

    updateScriptButtonText();
}

QString MainWindow::extractCutoutSection(const QString &fullText, QString *outCutoutGlsl)
{
    static const QRegularExpression cutoutRegex(
        R"(//CUTOUT_BEGIN([\s\S]*?)//CUTOUT_END)");
    QRegularExpressionMatch cutoutMatch = cutoutRegex.match(fullText);
    if (!cutoutMatch.hasMatch()) {
        if (outCutoutGlsl) outCutoutGlsl->clear();
        return fullText;
    }
    if (outCutoutGlsl) *outCutoutGlsl = GlslTranslator::translateEquation(cutoutMatch.captured(1));
    QString bodyWithoutCutout = fullText;
    bodyWithoutCutout.remove(cutoutMatch.capturedStart(0), cutoutMatch.capturedLength(0));
    return bodyWithoutCutout;
}

// ==========================================================
// MULTI-MESH: sezioni //MESH_BEGIN..//MESH_END
// ==========================================================
// Sintassi (una sezione per parte di mesh, ripetibile):
//
//     //MESH_BEGIN
//     u: 0, PI, 200          // uMin, uMax, passi in u
//     v: 0, TAU, 100         // vMin, vMax, passi in v
//     //MESH_END
//
// I passi sono opzionali e sono una PROPORZIONE, non un numero assoluto: la
// risoluzione la governa lo slider Steps (vedi la memoria
// slider-steps-governa-risoluzione-script). La parte col valore dichiarato piu'
// alto prende esattamente il valore dello slider e le altre restano in
// proporzione, cosi' i rapporti voluti (fra parti, e fra u e v) sono rispettati
// su TUTTA la corsa dello slider. Senza passi, la parte segue lo slider su
// entrambi gli assi.
// Nell'esempio sopra: v ha metà dei passi di u a qualunque posizione dello
// slider (200:100), non "esattamente 200 e 100".
// Le espressioni ammettono PI/TAU e aritmetica semplice: sono valutate qui, non
// nello shader, perche' servono al generatore di griglia sulla CPU.
//
// TUBI (script sul sotto-tab Tubes): la sezione dichiara un tubo. "u:" e' il
// tratto di curva; di "v:" contano solo i passi (attorno al tubo, in
// proporzione a quelli di u: il giro e' sempre 0..2pi), e "thickness: 0.5" fa
// quel tubo spesso la meta' del campo Thickness. Una sezione col solo spessore
// prende il dominio u dei limiti (outHasU dice quali sezioni hanno "u:").
//
// Nota: e' il traduttore GLSL a NON dover vedere queste righe, quindi la sezione
// viene rimossa dal testo restituito (come per il CUTOUT).
QString MainWindow::extractMeshSections(const QString &fullText, std::vector<MeshPart> *outParts,
                                        QVector<bool> *outHasU)
{
    if (outParts) outParts->clear();
    if (outHasU) outHasU->clear();

    static const QRegularExpression meshRegex(
        R"(//MESH_BEGIN([\s\S]*?)//MESH_END)");

    // Valuta un'espressione numerica semplice con PI/TAU. Usa lo stesso parser
    // ExprTk gia' impiegato per le equazioni, cosi' "PI/2" o "2*PI" funzionano.
    auto evalNumber = [](QString s, bool *ok) -> double {
        s = s.trimmed();
        if (s.isEmpty()) { if (ok) *ok = false; return 0.0; }

        // Le costanti: ExprTk conosce 'pi', non 'PI'/'TAU'.
        s.replace(QRegularExpression("\\bTAU\\b", QRegularExpression::CaseInsensitiveOption), "(2*pi)");
        s.replace(QRegularExpression("\\bPI\\b",  QRegularExpression::CaseInsensitiveOption), "pi");

        exprtk::symbol_table<double> st;
        st.add_constants();
        exprtk::expression<double> expr;
        expr.register_symbol_table(st);
        exprtk::parser<double> parser;
        if (!parser.compile(s.toStdString(), expr)) { if (ok) *ok = false; return 0.0; }
        if (ok) *ok = true;
        return expr.value();
    };

    QString remaining = fullText;
    QRegularExpressionMatchIterator it = meshRegex.globalMatch(fullText);

    std::vector<MeshPart> parts;
    QVector<bool> hasU;
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const QString body = m.captured(1);

        MeshPart part;
        bool sawAxis = false;
        bool sawU = false;

        // Una riga per asse: "u: min, max[, steps]" / "v: min, max[, steps]".
        static const QRegularExpression axisRegex(
            R"((?im)^\s*([uv])\s*:\s*([^,]+),\s*([^,\n]+)(?:,\s*([^,\n]+))?\s*$)");
        QRegularExpressionMatchIterator ax = axisRegex.globalMatch(body);
        while (ax.hasNext()) {
            QRegularExpressionMatch a = ax.next();
            const QString axis = a.captured(1).toLower();

            bool okMin = false, okMax = false;
            const double lo = evalNumber(a.captured(2), &okMin);
            const double hi = evalNumber(a.captured(3), &okMax);
            if (!okMin || !okMax) continue;

            int steps = -1;
            if (!a.captured(4).isEmpty()) {
                bool okStep = false;
                const double sv = evalNumber(a.captured(4), &okStep);
                if (okStep && sv >= 1.0) steps = (int)(sv + 0.5);
            }

            // I passi vanno in declaredU/declaredV, NON in numU/numV: sono una
            // PROPORZIONE che resolveMeshParts riscala sullo slider Steps (che
            // resta l'autorita' sulla risoluzione). Scriverli direttamente in
            // numU/numV li rendeva un tetto, e oltre quel valore lo slider non
            // faceva piu' nulla.
            if (axis == "u") {
                part.uMin = (float)lo; part.uMax = (float)hi;
                if (steps > 0) part.declaredU = steps;
                sawU = true;
            } else {
                part.vMin = (float)lo; part.vMax = (float)hi;
                if (steps > 0) part.declaredV = steps;
            }
            sawAxis = true;
        }

        // Spessore del tubo (solo i tubi lo leggono): fattore >= 0.
        static const QRegularExpression thicknessRegex(R"((?im)^\s*thickness\s*:\s*([^\n]+?)\s*$)");
        const QRegularExpressionMatch th = thicknessRegex.match(body);
        if (th.hasMatch()) {
            bool okTh = false;
            const double tv = evalNumber(th.captured(1), &okTh);
            if (okTh && tv >= 0.0) part.tubeThickness = (float)tv;
        }

        // Una sezione senza assi validi non descrive nulla: la ignoriamo invece
        // di generare una parte degenere (che sarebbe una superficie invisibile).
        // Per un tubo basta lo spessore: il dominio u e' quello dei limiti.
        if (sawAxis || part.tubeThickness >= 0.0f) {
            part.meshIndex = (int)parts.size();
            parts.push_back(part);
            hasU.append(sawU);
        }
    }

    // Rimuove le sezioni dal corpo (dalla fine, per non invalidare gli offset).
    QList<QRegularExpressionMatch> matches;
    QRegularExpressionMatchIterator it2 = meshRegex.globalMatch(fullText);
    while (it2.hasNext()) matches.append(it2.next());
    for (int k = matches.size() - 1; k >= 0; --k) {
        remaining.remove(matches[k].capturedStart(0), matches[k].capturedLength(0));
    }

    if (outParts) *outParts = parts;
    if (outHasU) *outHasU = hasU;
    return remaining;
}

void MainWindow::onRunScriptClicked()
{
    QString fullText = m_scene.surfaceScriptText;
    if (fullText.trimmed().isEmpty()) return;

    // 1. PRIMA LINEA DI DIFESA: Evita che testo spazzatura faccia crashare il parser
    QString cleanCode = stripCodeComments(fullText);

    // SCRIPT METRICO: se il codice restituisce un mat3 è il tensore metrico
    // g_ij(U,V,W) per il flusso geodetico, non una superficie parametrica.
    static const QRegularExpression metricReturnRegex(R"(\breturn\s+mat3\s*\()");
    if (cleanCode.contains(metricReturnRegex)) {
        runMetricScript(fullText);
        return;
    }

    // DELEGA LA VALIDAZIONE E BLOCCA SE FALLISCE
    if (!InputValidator::validateParametricScriptReturn(this, cleanCode)) {
        return;
    }

    // Sotto-tab TUBES: lo script descrive la CURVA asse (vec4(x, y, z, p)),
    // e attorno il motore costruisce i tubi.
    if (tubesShown()) {
        runTubeScriptFromDock(fullText);
        return;
    }

    m_scene.surfaceScriptApplied = fullText;
    parseAndApplyScriptParams(fullText, false);

    // Sezione opzionale //CUTOUT_BEGIN..//CUTOUT_END: corpo di
    // bool cutHere(float u, float v) per il taglio delle pareti interne nei
    // punti di autointersezione (discard nel fragment). Va estratta PRIMA di
    // costruire glslBody, altrimenti finirebbe iniettata anche in
    // getRawPosition() nel vertex shader.
    QString cutoutGlsl;
    QString bodyWithoutCutout = extractCutoutSection(fullText, &cutoutGlsl);
    ui->glWidget->getEngine()->setCutoutCodeGLSL(cutoutGlsl);

    // Sezioni //MESH_BEGIN..//MESH_END (multi-mesh): stesso trattamento del
    // cutout, vanno via dal corpo prima di costruire glslBody.
    std::vector<MeshPart> meshParts;
    bodyWithoutCutout = extractMeshSections(bodyWithoutCutout, &meshParts);
    ui->glWidget->getEngine()->setMeshParts(countMeshParts(meshParts, fullText));

    QString glslBody;
    QTextStream stream(&bodyWithoutCutout);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.contains(":=")) continue;
        glslBody.append(line + "\n");
    }
    glslBody = GlslTranslator::translateEquation(glslBody);

    ui->glWidget->setResolution(m_scene.steps);
    // Dai CAMPI, non dagli slider: lo slider ha passo 0.01 e mostra il campo.
    setEngineConstants(resolveCascadeConstants(false), /*onlyIfChanged=*/false);

    // 2. SECONDA LINEA DI DIFESA: dry-run del vertex shader script
    if (!ui->glWidget->validateAndApplyParametricScript(glslBody)) {
        showShaderError("Script Compilation Error",
                                                   ui->glWidget->getShaderError());

        return;
    }

    // 3. TERZA LINEA DI DIFESA: Controllo Esecuzione e Collasso Matematico
    ui->glWidget->updateSurfaceData();

    if (!ui->glWidget->getEngine()->isMeshValid()) {
        performMasterStop();
        InputValidator::showMathematicalCollapseError(this);

        ui->glWidget->getEngine()->setScriptMode(false);
        ui->glWidget->getEngine()->setScriptCodeGLSL("");
        ui->glWidget->rebuildShader();
        ui->glWidget->updateSurfaceData();

        return;
    }

    // TUTTO OK: ABILITIAMO IL TASTO SALVA
    ui->btnSaveScript->setEnabled(true);

    // Uno script parametrico attivo esclude la modalità metrica
    exitMetricScriptMode();

    // Tutto OK
    setEqText(&EquationTexts::x, QString());
    setEqText(&EquationTexts::y, QString());
    setEqText(&EquationTexts::z, QString());
    setEqText(&EquationTexts::p, QString());

    // A SEGNALI BLOCCATI come le quattro righe qui sopra. Questi sei clear sono
    // pulizia PROGRAMMATICA (lo script prende il posto delle equazioni), non un
    // edit dell'utente: lasciando vivi i segnali, il textChanged di
    // lineExplicitU arrivava a noteSceneEdited e marcava la scena come
    // MODIFICATA. Da li' il popup "vuoi salvare?" all'uscita dopo un semplice
    // Stop/Start, senza che l'utente avesse toccato nulla (visto su Villarceau
    // Tubes Drift: il master Start passa da onRunScriptClicked).
    {
        for (EqField f : { &EquationTexts::explicitU, &EquationTexts::explicitV, &EquationTexts::explicitW,
                           &EquationTexts::u, &EquationTexts::v, &EquationTexts::w })
            setEqText(f, QString());
    }

    if (ui->glWidget) {
        ui->glWidget->setParametricEquations("0", "0", "0", "0");
        if (ui->glWidget->getEngine()) {
            // Anche il vincolo torna neutro (vedi il load degli script in
            // mainwindow_scene.cpp): uno script lanciato dopo un preset a
            // vincolo riceveva sempre u = 0.
            ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
            ui->glWidget->getEngine()->setExplicitU("");
            ui->glWidget->getEngine()->setExplicitV("");
            ui->glWidget->getEngine()->setExplicitW("");
        }
    }

    m_masterStopped = false;
    m_userStoppedGeomClock = false;   // run esplicito del modulo geometria
    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(hasTimeVariable(fullText));
    }
    updateMasterButtonState();
    ui->glWidget->update();
}

// =============================================================================
// SCRIPT METRICO: lo script restituisce il tensore metrico g_ij come
// mat3(U,V,W). La mesh non viene generata dallo script: arriva dal flusso
// geodetico (setCustomMesh), nel cui compute shader il tensore dello script
// sostituisce la metrica indotta dall'embedding. I campi X/Y/Z/P restano in
// uso solo come mappa di visualizzazione delle coordinate; le condizioni
// iniziali si danno come sempre dal dock Equations (U,V,W / dU,dV,dW).
// =============================================================================
void MainWindow::runMetricScript(const QString& fullText)
{
    QString cleanCode = stripCodeComments(fullText);
    if (!InputValidator::validateParentheses(this, cleanCode)) return;

    m_scene.surfaceScriptApplied = fullText;
    setScriptText(SlotSurface, fullText);

    // MULTI-MESH: uno script metrico produce una mesh CUSTOM (flusso geodetico),
    // che non ha parti. Le parti di uno script multi-mesh precedente vanno
    // azzerate qui: runMetricScript esce da onRunScriptClicked PRIMA
    // dell'estrazione delle sezioni //MESH_BEGIN, quindi sopravviverebbero e un
    // computeMesh() successivo spezzerebbe la superficie in rami inesistenti.
    if (ui->glWidget->getEngine())
        ui->glWidget->getEngine()->clearMeshParts();

    // Direttive := (limiti U/V/W, costanti A..F, steps). Al caricamento di un
    // preset NON vanno riapplicate: limiti/costanti/steps salvati (già
    // ripristinati da applyCommonData) hanno la precedenza. Al Run MANUALE vince
    // lo stato UI corrente, esattamente come il tasto Run del dock Equations
    // (onlyFillEmptyLimits): una direttiva di limite riempie solo il campo
    // ancora vuoto, costanti e steps non vengono mai reimposti dallo script.
    // Così i due tasti producono la stessa superficie (niente rimpicciolimento
    // da v_min/v_max := che sovrascrivevano i limiti del dock).
    if (!m_scriptRunFromLoad)
        parseAndApplyScriptParams(fullText, false, /*onlyFillEmptyLimits=*/true);

    // Condizioni iniziali dichiarate nello script: le direttive case-sensitive
    // U/V/W/dU/dV/dW/Conform := espressione; vengono copiate verbatim nei campi
    // della tab Geodesic Flow (possono citare il parametro di famiglia u). Così
    // lo script metrico è autosufficiente: metrica + condizioni iniziali.
    // Vince SEMPRE lo stato UI corrente (come i limiti e come il Run del dock
    // Equations): una direttiva riempie SOLO il campo ancora vuoto, mai
    // sovrascrive una condizione già presente nel dock — né al caricamento di un
    // preset né al Run manuale. Altrimenti il Run dello script ripristinerebbe le
    // condizioni dello script difformi da quelle correntemente in uso nel dock.
    {
        static const QRegularExpression icRegex(
            R"(\b(U|V|W|dU|dV|dW|Conform|conform)\s*:=\s*([^;]+);)");

        QRegularExpressionMatchIterator it = icRegex.globalMatch(cleanCode);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString name = m.captured(1);
            const QString value = m.captured(2).trimmed();

            EqField field = &EquationTexts::conform;
            if      (name == "U")  field = &EquationTexts::geoU;
            else if (name == "V")  field = &EquationTexts::geoV;
            else if (name == "W")  field = &EquationTexts::geoW;
            else if (name == "dU") field = &EquationTexts::geoDU;
            else if (name == "dV") field = &EquationTexts::geoDV;
            else if (name == "dW") field = &EquationTexts::geoDW;

            if (!equationFieldEdit(field)) continue;
            if (!(m_scene.eq.*field).trimmed().isEmpty())
                continue;

            setEqText(field, value);
        }
    }

    // Corpo GLSL: via le direttive; la traduzione avviene nel GeodesicCalculator
    QString body;
    QString scriptCopy = fullText;
    QTextStream stream(&scriptCopy);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.contains(":=")) continue;
        body.append(line + "\n");
    }
    m_metricScriptBody = body;

    // Da qui la superficie e' METRICA: vive in tutti e due i dock (la g_ij qui,
    // carta e condizioni iniziali del flusso geodetico nelle Equations). Va detto
    // anche per lo script scritto a mano, non solo per i preset: altrimenti resta
    // OriginScript e i Run del dock Equations restano spenti, lasciando le
    // condizioni iniziali modificabili ma non applicabili.
    // NB: e' la sola eccezione alla regola "il Run non sposta la sorgente" --
    // qui non trasferisce la superficie a un altro dock, ne riconosce la natura.
    m_surfaceOrigin = OriginBoth;

    // La modalità script di superficie va spenta: la mesh arriva dal
    // calcolatore geodetico, non dal vertex shader parametrico.
    if (ui->glWidget && ui->glWidget->getEngine() &&
            ui->glWidget->getEngine()->isScriptModeActive()) {
        ui->glWidget->getEngine()->setScriptMode(false);
        ui->glWidget->getEngine()->setScriptCodeGLSL("");
        ui->glWidget->rebuildShader();
    }

    // Mappa di visualizzazione: in modalità metrica i campi x/y/z/p mappano le
    // coordinate (U,V,W) nello spazio 3D/4D. Se non citano U/V/W maiuscole
    // (campi vuoti o ancora occupati da una superficie parametrica in u,v
    // minuscole, es. il toro di default) li prendiamo in consegna con la carta
    // identità. Serve anche al routing: sia onStartClicked che
    // checkAndTriggerMeshUpdate attivano il geodetico sulle maiuscole.
    const QString displayEqs = m_scene.eq.x + " " +
            m_scene.eq.y + " " + m_scene.eq.z + " " +
            m_scene.eq.p;
    if (!displayEqs.contains(kReUpperU) && !displayEqs.contains(kReUpperV) &&
            !displayEqs.contains(kReUpperW)) {
        setEqText(&EquationTexts::x, QStringLiteral("U"));
        setEqText(&EquationTexts::y, QStringLiteral("V"));
        setEqText(&EquationTexts::z, QStringLiteral("W"));
        setEqText(&EquationTexts::p, QStringLiteral("0"));
    }

    // I campi sono stati riempiti a segnali bloccati: la macchina a stati dei
    // tab (che abilita i campi delle condizioni iniziali sul caso "3 maiuscole")
    // va rieseguita a mano, con focus sulla tab Geodesic Flow.
    checkParametricDependency();
    if (ui->panelImplicit && ui->panelImplicit->count() > 2 &&
            ui->panelImplicit->isTabEnabled(2)) {
        ui->panelImplicit->setCurrentIndex(2);
    }

    // Fattore conforme di default, come in onStartClicked
    if (m_scene.eq.conform.trimmed().isEmpty()) {
        setEqText(&EquationTexts::conform, QStringLiteral("1.0"));
    }

    ui->btnSaveScript->setEnabled(true);

    // Senza condizioni iniziali il flusso non può partire: guida l'utente
    // verso il dock Equations e resta in attesa.
    if (!hasGeodesicText()) {
        InputValidator::showMetricMissingConditionsInfo(this);
        return;
    }

    m_masterStopped = false;
    m_userStoppedGeomClock = false;   // run esplicito del modulo geometria
    updateMasterButtonState();

    m_geodesicErrorPending = false;
    m_geoErrorShown = false;
    m_geoErrorType = GeoError::None;

    checkAndTriggerMeshUpdate();
    // Di nuovo a mesh costruita: la chiamata qui sopra la trovava ancora vuota
    // dopo un NEW, e il gate della scena vuota restava acceso.
    updateMasterButtonState();
}

// Codice della SUPERFICIE da cui si deducono le sue costanti: equazione
// implicita (del sotto-tab attivo) o script in Ray Marching; in parametrica le
// equazioni X/Y/Z/P con composizioni e vincoli, piu' lo script. Unica sede per
// il controllo delle costanti contese e per l'avviso al caricamento di un record.
QString MainWindow::surfaceConstantSource() const
{
    if (implicitMode())
        return activeImplicitEquationText() + " " + m_scene.surfaceScriptText + " " + m_scene.surfaceScriptApplied;
    return m_scene.eq.x + " " + m_scene.eq.y + " " +
           m_scene.eq.z + " " + m_scene.eq.p + " " +
           m_scene.eq.u + " " + m_scene.eq.v + " " +
           m_scene.eq.w + " " +
           m_scene.eq.explicitU + " " +
           m_scene.eq.explicitV + " " +
           m_scene.eq.explicitW + " " + m_scene.surfaceScriptText + " " + m_scene.surfaceScriptApplied
           + " " + tubeConstantSource();
}

QString MainWindow::tubeConstantSource() const
{
    // Il campo Meshes (numero di copie) vale per superfici e tubi: un
    // "A*A" usa A come le equazioni.
    const QString meshCount = implicitMode() ? QString() : " " + m_scene.meshCount;
    if (!tubesShown()) return meshCount;
    return m_scene.tube.x + " " + m_scene.tube.y + " " + m_scene.tube.z
           + " " + m_scene.tube.p + " " + m_scene.tube.thickness + meshCount;
}

// Avviso "costante ambigua": A..F è una sola variabile globale. Se la stessa
// costante compare sia nel corpo metrico sia nelle condizioni iniziali (campi
// del dock Geodesic Flow), muovere il relativo slider altera entrambe — es. la
// massa della metrica e l'apertura del fascio insieme. Il controllo legge i
// campi del dock, così intercetta sia le direttive U:=/dU:=/... (già scritte
// nei campi al Run) sia le modifiche fatte a mano nel dock. Sta in
// updateGeodesicMesh, l'imbuto unico di ogni ricalcolo geodetico; una firma
// dell'ultima configurazione evita di ripetere il popup a ogni frame o slider.
bool MainWindow::confirmTextureConstantClash(const QString& texCode, const QString& dispCode,
                                            bool forBackground)
{
    // Le costanti citate da un blocco di codice GLSL. Stesso criterio di
    // updateConstantsUIState: match CASE-SENSITIVE (le iniettate sono maiuscole,
    // salvo 's') e una lettera DICHIARATA come variabile locale non conta --
    // "float S = 512.0" e' la locale dello script, non lo slider.
    const auto constantsIn = [](const QString& raw) { return constantsUsedIn(raw); };

    // Codice della SUPERFICIE: equazione implicita o script, secondo il tab
    // attivo. In parametrica sono le equazioni X/Y/Z/P con composizioni e
    // vincoli.
    const QString surfaceCode = surfaceConstantSource();

    // Chi e' gia' in scena, cioe' contro cosa si confronta la texture in arrivo.
    // I tre moduli (superficie, texture, sfondo) leggono lo STESSO mathParams:
    // il modulo che si sta sostituendo va pero' escluso, o si segnalerebbe un
    // conflitto con se stesso -- ricaricare la stessa texture darebbe l'avviso.
    QSet<QString> inScene = constantsIn(surfaceCode);
    if (forBackground) {
        // Sfondo in arrivo: contano superficie e texture di superficie.
        inScene.unite(constantsIn(m_scene.surfaceTextureCode));
    } else {
        // Texture in arrivo: contano superficie e sfondo.
        inScene.unite(constantsIn(m_scene.bgTextureCode));
    }
    if (inScene.isEmpty()) return true;              // niente da contendere

    const QSet<QString> tex = constantsIn(texCode + "\n" + dispCode);
    QSet<QString> clash = tex;
    clash.intersect(inScene);
    if (clash.isEmpty()) return true;                // caso normale: nessun conflitto

    // Lettere ancora libere: ne' la superficie ne' la texture le usano. E' la
    // via d'uscita concreta da suggerire, invece di un generico "usane un'altra".
    QStringList free;
    for (const QChar c : QStringLiteral("ABCDEF")) {
        const QString L(c);
        if (!inScene.contains(L) && !tex.contains(L)) free << L;
    }

    QStringList names(clash.begin(), clash.end());
    std::sort(names.begin(), names.end());
    return InputValidator::showTextureConstantClashWarning(this, names, free);
}

void MainWindow::checkMetricConstantAmbiguity()
{
    if (m_metricScriptBody.trimmed().isEmpty()) {
        m_lastAmbiguousConstSig.clear();
        return;
    }

    const QString metricBody = stripCodeComments(m_metricScriptBody);
    QString conditions =
            m_scene.eq.geoU + " " + m_scene.eq.geoV + " " +
            m_scene.eq.geoW + " " + m_scene.eq.geoDU + " " +
            m_scene.eq.geoDV + " " + m_scene.eq.geoDW + " " +
            m_scene.eq.conform;

    // Uso COERENTE vs AMBIGUO. Una costante che compare in una condizione DENTRO
    // una chiamata a un solver geometrico (kerrUmin(A,B), kerrRadius(...),
    // solveKruskalR(...) ...) NON e' ambigua: sta calcolando il punto di partenza
    // a partire dalla STESSA geometria della metrica (es. partire fuori
    // dall'orizzonte r_+(M,a)). Ambiguo e' invece l'uso scollegato (es.
    // dV = A*cos(u), dove A apre il fascio mentre nella metrica e' la massa).
    // Per distinguere, rimuoviamo dalle condizioni le chiamate ai solver impliciti
    // (nome + argomenti) prima di cercare i token A..F: cosi' kerrUmin(A,min(B,A))
    // non conta, ma A*cos(u) si'. Usiamo un match a PARENTESI BILANCIATE (non un
    // regex), per gestire annidamenti arbitrari come kerrUmin(A, abs(sin(C*t))).
    {
        const QRegularExpression nameRe(
            "\\b(?:kerrUmin|kerrRadius|kerrEmbedZ|kerrGrr|kerrGpp|kerrRprime|"
            "solveKerrR|kruskalR_U|kruskalGxx|kruskalRprime|kruskalEmbedZ|"
            "solveKruskalR)\\s*\\(");
        QRegularExpressionMatch m;
        while ((m = nameRe.match(conditions)).hasMatch()) {
            const int start = m.capturedStart();   // inizio del nome
            int i = m.capturedEnd() - 1;           // posizione della '(' iniziale
            int depth = 0;
            bool closed = false;
            for (; i < conditions.size(); ++i) {
                const QChar ch = conditions.at(i);
                if (ch == '(') ++depth;
                else if (ch == ')') { if (--depth == 0) { ++i; closed = true; break; } }
            }
            // i e' uno oltre la ')' che chiude. Rimuove il tratto [start, i).
            // Se le parentesi non si chiudono (input incompleto) rimuove fino alla
            // fine e interrompe, per non ciclare all'infinito.
            conditions.remove(start, i - start);
            if (!closed) break;
        }
    }

    QStringList ambiguous;
    for (const QChar c : {'A','B','C','D','E','F'}) {
        const QRegularExpression re("\\b" + QString(c) + "\\b");
        if (metricBody.contains(re) && conditions.contains(re))
            ambiguous << QString(c);
    }

    // Firma = corpo metrico + condizioni: cambia solo quando l'utente edita
    // metrica o condizioni, non quando muove gli slider o avanza l'animazione.
    const QString sig = metricBody + "\x1f" + conditions;
    if (sig == m_lastAmbiguousConstSig) return;
    m_lastAmbiguousConstSig = sig;

    if (!ambiguous.isEmpty())
        InputValidator::showMetricAmbiguousConstantWarning(this, ambiguous);
}

// La mappa di visualizzazione di uno script metrico è "custom" se non è la
// carta identità (x=U, y=V, z=W, p=0). Solo in quel caso va salvata nel preset.
bool MainWindow::metricDisplayMapIsCustom() const
{
    auto norm = [](QString s) { return s.trimmed().remove(' '); };
    const QString x = norm(m_scene.eq.x);
    const QString y = norm(m_scene.eq.y);
    const QString z = norm(m_scene.eq.z);
    const QString p = norm(m_scene.eq.p);
    const bool isIdentity = (x == "U") && (y == "V") && (z == "W") &&
                            (p == "0" || p.isEmpty());
    return !isIdentity;
}

void MainWindow::writeMetricDisplayMap(QJsonObject& root) const
{
    if (m_metricScriptBody.trimmed().isEmpty()) return;   // non in modalità metrica
    if (!metricDisplayMapIsCustom()) return;              // identità: non serve salvarla

    QJsonObject map;
    map["x"] = m_scene.eq.x;
    map["y"] = m_scene.eq.y;
    map["z"] = m_scene.eq.z;
    map["p"] = m_scene.eq.p;
    root["metricDisplayMap"] = map;
}

// Spegne la modalità metrica ovunque essa termini (reset, cambio tab, run di
// uno script non metrico, caricamento preset): oltre ad azzerare il corpo
// dello script, la macchina a stati va rieseguita per riabilitare i campi
// X/Y/Z/P e le tab Constraints/Composition bloccate da runMetricScript.
void MainWindow::exitMetricScriptMode()
{
    if (m_metricScriptBody.isEmpty()) return;
    m_metricScriptBody.clear();
    // La superficie non e' piu' metrica: OriginBoth (i due dock che collaborano)
    // non descrive piu' la situazione. Chi arriva da un load o da un reset
    // riassegna la sorgente per conto suo subito dopo; qui si ricade sul dock che
    // possiede la superficie adesso, cosi' i Run tornano al gate normale.
    if (m_surfaceOrigin == OriginBoth) {
        m_surfaceOrigin = m_scene.surfaceScriptApplied.trimmed().isEmpty()
                        ? OriginEquations : OriginScript;
    }
    checkParametricDependency();
}

void MainWindow::onApplyTextureScriptClicked()
{
    // Lo script della texture su cui agiscono i comandi (sfondo, fascia o
    // superficie), dal suo slot: qualunque modulo il dock stia mostrando.
    QString code = scriptText(targetTextureSlot());

    // Codice applicato PRIMA di questa chiamata: serve piu' sotto per capire se
    // lo script sta davvero cambiando (texture nuova) o se e' un semplice
    // riavvio dello stesso (Run/Stop del dock).
    const QString prevSurfaceTextureCode = m_scene.surfaceTextureCode;

    // Le copie APPLICATE (m_scene.surfaceTextureCode / m_scene.bgTextureCode) si scrivono
    // piu' sotto, solo DOPO che il motore ha compilato il codice. Qui venivano
    // scritte subito: uno script che non compila risultava applicato mentre il
    // motore disegnava il precedente, e in ambito Mesh ci finiva lo script
    // della fascia (trovati dal test degli scenari).

    if (code.trimmed().isEmpty()) return;

    QString imgPath = TextureCode::resolveImagePath(code);
    if (imgPath.startsWith("NOT_FOUND|")) {
        InputValidator::showImageNotFoundError(this, imgPath.split("|").last());
        imgPath = "";
    }

    // Determina se il codice contiene logica procedurale
    const bool hasCustomLogic = TextureCode::hasLogic(code);

    if (editingBackground()) {
        // --- RAMO A: SFONDO ---
        ui->glWidget->setBackgroundTextureEnabled(true);
        refreshTextureCheckbox();

        // I picker Colore servono solo se lo script di sfondo usa quel colore (indipendenti).
        bool bgCol1 = code.contains("u_col1");
        bool bgCol2 = code.contains("u_col2");
        ui->radioTexColor1->setEnabled(bgCol1);
        ui->radioTexColor2->setEnabled(bgCol2);
        if ((bgCol1 || bgCol2) && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
            QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
            bool oldRad = target->blockSignals(true);
            target->setChecked(true);
            target->blockSignals(oldRad);
        }

        if (ui->glWidget) {
            ui->glWidget->setBackgroundTexColors(m_scene.bgTexColor1, m_scene.bgTexColor2);
        }

        // 1. Carica l'immagine (se c'è)
        if (!imgPath.isEmpty()) {
            ui->glWidget->setBackgroundTexture(imgPath);
        }
        const bool dropBgImage = imgPath.isEmpty() && !backgroundImagePath().isEmpty();

        // 2. Applica la logica custom (se c'è) o resetta al default
        if (hasCustomLogic || imgPath.isEmpty()) {
            // Dry-run del fragment shader prima di toccare la pipeline
            if (!ui->glWidget->validateAndApplyBackgroundShader(code)) {
                showShaderError("Background Shader Error",
                                                           ui->glWidget->getShaderError());
                return;
            }
            if (ui->glWidget && imgPath.isEmpty()) {
                ui->glWidget->setBackgroundFraming(1.0f, QVector2D(0.0f, 0.0f), 0.0f);
            }
        }

        // L'immagine segue SEMPRE il codice appena applicato: se lo script non
        // la nomina piu', nel sampler torna la default. Dopo la validazione, e
        // senza cambiare modo (setBackgroundImage): lo script appena applicato
        // resta attivo. Prima si azzerava solo il percorso in MainWindow, e
        // l'immagine restava in GPU -- uno script che campiona iChannel0 la
        // mostrava ancora, ma il Save non la scriveva (test degli scenari).
        if (dropBgImage) ui->glWidget->setBackgroundImage("background.png");

        m_scene.bgTextureCode = code;
        updateRenderState();
        if (ui->glWidget) ui->glWidget->update();
        updateFlatPreviewButton();

    } else if (editingBorder()) {
        // --- RAMO C: BORDO ---
        // Come una fascia: il codice va nel motore (getCustomColor_border nel
        // fragment); se non compila, rebuildShader lascia in piedi lo shader
        // precedente. Il tag //IMG: e' l'immagine del bordo.
        if (!InputValidator::validateParametricScriptContext(this, code)) return;
        GLWidget *gl = ui->glWidget;
        if (!gl) return;
        if (!imgPath.isEmpty()) code = TextureCode::withImageTagPath(code, imgPath);
        // Scritto a mano: non e' piu' la voce della Library da cui veniva.
        if (code.trimmed() != gl->borderTextureCode().trimmed()) gl->setBorderTextureLibName(QString());
        gl->setBorderTexture(code, true);
        // Run = avvio esplicito: riarma lo stop manuale e accende l'orologio
        // se lo script usa il tempo.
        m_userStoppedBorderTexClock = false;
        gl->setBorderTextureAnimating(hasTimeVariable(code));
        refreshTextureCheckbox();
        updateTextureUIState(true, true);
        updateMasterButtonState();
        updateScriptButtonText();
        syncTextureTreeSelection();
        refreshConstants();
        gl->update();
        // FINE: la coda qui sotto e' di superficie e sfondo. Proseguendo, il Run
        // del bordo riarmava e riaccendeva l'orologio della texture di
        // SUPERFICIE (fermata a mano, ripartiva) e riaccendeva il tasto Save.
        return;
    } else {
        // --- RAMO B: SUPERFICIE ---
        if (!InputValidator::validateParametricScriptContext(this, code)) return;

        // --- B0: TEXTURE DELLA SOLA MESH SELEZIONATA ---
        // Con l'ambito su "Mesh" e una parte attiva la texture appartiene a
        // QUELLA parte: stessa logica di colore/alpha/luce, che con una mesh
        // selezionata scrivono sulla parte e non sul globale.
        // In "All" (nessuna parte attiva) si prosegue col ramo di sempre, che
        // applica la texture all'intera superficie.
        if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0) {
            // IL TAG //IMG: DELLO SCRIPT E' L'IMMAGINE DELLA FASCIA (vedi
            // GLWidget::syncPartImages): la carica il renderer, dal percorso
            // del tag. Se il file e' stato ritrovato altrove dallo Smart Path
            // Resolver, il tag prende quel percorso; non trovato (l'avviso e'
            // gia' uscito qui sopra) resta com'e' e la fascia disegna con
            // l'immagine della superficie.
            // La compilazione avviene dentro setActiveMeshTexture (il codice
            // finisce nel fragment shader come getCustomColor_<k>): se lo shader
            // non compila, rebuildShader lascia in piedi il precedente e la
            // superficie non sparisce.
            if (!imgPath.isEmpty()) code = TextureCode::withImageTagPath(code, imgPath);
            ui->glWidget->setActiveMeshTexture(code, true);

            // CHECKBOX "Texture" ACCESO. Applicare una texture a una mesh la
            // ACCENDE su quella mesh, quindi il checkbox -- che di quello stato
            // e' il display -- deve risultare selezionato, come fa il ramo
            // globale poco piu' sotto.
            // A SEGNALI BLOCCATI: il toggled rientrerebbe nel ramo per-mesh e
            // riscriverebbe la parte (con il codice letto da activeMeshTextureCode,
            // cioe' quello appena messo) facendo un secondo rebuildShader inutile.
            refreshTextureCheckbox();

            // OROLOGIO DELLA TEXTURE. Il Run e' un avvio esplicito del modulo:
            // riarma un eventuale stop manuale e rivaluta se il clock serve,
            // guardando anche gli script per-mesh (allSurfaceTextureCode).
            // Senza questo il ramo usciva dalla funzione PRIMA del blocco che
            // governa l'animazione, e una texture con 't' restava ferma.
            m_userStoppedTexClock = false;
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(allSurfaceTextureCode()));
            // Orologio della PARTE: come nel ramo Library, va acceso qui o una
            // texture animata appena applicata alla fascia nascerebbe ferma.
            ui->glWidget->setActiveMeshTextureAnimating(hasTimeVariable(code));
            m_userStoppedMeshTexClock = false;   // comando esplicito: riarma il gate
            updateMasterButtonState();

            updateTextureUIState(true, true);
            updateScriptButtonText();
            // COSTANTI: e' cambiato il codice applicato della fascia.
            refreshConstants();
            ui->glWidget->update();
            return;
        }

        if (!commitSurfaceTextureCode(code)) {
            showShaderError("Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
            return;
        }

        // 1. GESTIONE MEMORIA IMMAGINE: il tag dello script dice quale, o
        // nessuna. PRIMA di updateTextureUIState: activeTextureUsesColors() ha
        // una scorciatoia "scacchiera default -> picker accesi" che guarda se il
        // motore ha un'immagine (surfaceHasImage). Con lo stato della texture
        // PRECEDENTE, alla PRIMA texture procedurale senza u_col1/u_col2 i
        // picker restavano accesi a torto (un giro di ritardo). E prima
        // dell'accensione qui sotto: clearTexture spegne la texture nel motore,
        // e applySurfaceTextureToEngine la riaccende.
        if (!imgPath.isEmpty()) {
            if (ui->glWidget) ui->glWidget->loadTextureFromFile(imgPath);
        } else if (ui->glWidget) {
            ui->glWidget->clearTexture();
        }

        // Applicare la texture di superficie la ACCENDE: intenzione accesa,
        // motore e checkbox la seguono. Prima l'intenzione si scriveva solo se il
        // checkbox era spento, fidandosi che le due copie fossero allineate.
        m_scene.surfaceTextureState = true;
        applySurfaceTextureToEngine();
        refreshTextureCheckbox();


        // Rinfresca lo stato UI ad OGNI applicazione (anche se la texture era già
        // accesa): cambiando texture i picker Colore vanno riallineati a u_col1/u_col2
        // del nuovo script. m_scene.surfaceTextureCode e i flag di modalità sono già aggiornati.
        // resetColorTargetToFirst: caricando una nuova texture il focus torna a Colore 1.
        updateTextureUIState(true, true);

        // 2. GESTIONE COMPILAZIONE SHADER
        if (hasCustomLogic) {
            if (ui->glWidget) {
                if (imgPath.isEmpty()) {
                    generateTexture();
                }

                // TEST E APPLICAZIONE (Solo Parametrica come da nuove regole):
                // di nuovo, ora che il sampler ha l'immagine o la scacchiera.
                bool success = commitSurfaceTextureCode(code);

                if (!success) {
                    showShaderError("Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
                    return;
                }

                // (Qui c'era un passaggio forzato al parametrico se si era in Ray
                // Marching: irraggiungibile -- in RM lo slot della texture di
                // superficie e' vuoto e la funzione esce in testa, scenario
                // "modulo Texture in Ray Marching" -- e, se raggiunto, avrebbe
                // buttato via la scena RM senza avviso.)

                // AZZERAMENTO DELL'INQUADRATURA 2D **SOLO SE LO SCRIPT CAMBIA**.
                // Ripartire da zoom/pan/rotazione neutri ha senso per una texture
                // NUOVA, non per un riavvio: il tasto Run/Stop del dock e' anche
                // il modo per far ripartire l'animazione, e azzerando sempre
                // cancellava le manipolazioni 2D dell'utente ad ogni Start.
                // Il confronto e' col codice applicato PRIMA della chiamata
                // (catturato in cima: m_scene.surfaceTextureCode e' gia' stato
                // sovrascritto), normalizzato come altrove per non contare
                // spazi e commenti.
                if (TextureCode::cleanForComparison(code) != TextureCode::cleanForComparison(prevSurfaceTextureCode)) {
                    ui->glWidget->setFlatPan(0.0f, 0.0f);
                    ui->glWidget->setFlatZoom(1.0f);
                    ui->glWidget->setFlatRotation(0.0f);
                }
            }
        } else {
            // Nessuna logica nel codice: surfaceTextureIsCustom() e' gia' falso,
            // e il motore e' gia' sullo shader standard (commitSurfaceTextureCode
            // in cima a questo ramo). Si ricostruisce ora che il sampler e'
            // cambiato.
            if (ui->glWidget) ui->glWidget->rebuildShader();
        }

        if (ui->glWidget) {
            updateRenderState();
            ui->glWidget->update();
        }
    }

    // Tempo nel codice: hasTimeVariable (commenti esclusi), come i tasti.

    // RUN texture: agisce SOLO sul clock della texture interessata (Problema 3).
    // La superficie e l'altro canale texture NON vengono toccati.
    // NB: NON azzeriamo m_masterStopped. I clock texture (setBackground/
    // SurfaceTextureAnimating) partono da soli nel GLWidget e non sono gated dallo
    // stop globale, quindi la texture si anima comunque. Azzerare il flag globale
    // sbloccava invece la GEOMETRIA: dopo un master STOP, applicare una texture di
    // sfondo e poi spegnerla faceva ripartire la superficie (t nelle composizioni).
    if (editingBackground()) {
        // RUN sulla texture di SFONDO: solo il clock background.
        // Run esplicito del canale: riarma un eventuale stop manuale.
        m_userStoppedBgClock = false;
        bool bgNeedsAnim = hasTimeVariable(m_scene.bgTextureCode);
        if (ui->glWidget) ui->glWidget->setBackgroundTextureAnimating(bgNeedsAnim);
    } else {
        // RUN sulla texture di SUPERFICIE: solo il clock texture superficie.
        // Run esplicito del canale: riarma un eventuale stop manuale.
        m_userStoppedTexClock = false;
        bool surfTexNeedsAnim = false;
        if (surfaceTextureShown()) {
            // Colore e displacement separati: concatenati, un commento in coda
            // al primo si mangerebbe la prima riga del secondo.
            surfTexNeedsAnim = implicitMode()
                    ? (hasTimeVariable(m_scene.rm.texture) || hasTimeVariable(m_scene.rm.displacement))
                    : hasTimeVariable(allSurfaceTextureCode());
        }
        if (ui->glWidget) ui->glWidget->setSurfaceTextureAnimating(surfTexNeedsAnim);

        // OROLOGI PER-MESH: **NON** si toccano da qui. Il Run in ambito "All"
        // comanda la texture di SUPERFICIE; le fasce hanno il proprio script e
        // il proprio orologio, governati dal Run con quella mesh selezionata.
        // Riaccenderle qui faceva ripartire una fascia che l'utente aveva
        // fermato apposta, mentre il tasto continuava a descrivere l'altro
        // ambito. Il gesto simmetrico non serve piu': lo Stop in "All" non le
        // spegne (vedi onRunCurrentScript), quindi non restano fermi per sempre.
        // Per fermare/riavviare TUTTO insieme c'e' il master.
    }
    updateMasterButtonState();

    updateFlatPreviewButton();
    ui->btnSaveScript->setEnabled(true);

    // COSTANTI: il codice applicato e' appena cambiato (superficie o sfondo).
    refreshConstants();
}

void MainWindow::onRunSoundClicked()
{
    // Se sta suonando, ferma
    if (ui->btnRunCurrentScript->text() == "Stop Sound") {
        m_audioController->stopAll();
        // Stop ESPLICITO dell'utente: un successivo commit di equazione/texture non
        // deve riaccendere il suono (vedi gate in applyStartSideEffects).
        m_userStoppedSound = true;
        if (m_currentScriptMode == ScriptModeSound) {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
        updateMasterButtonState();
        return;
    }

    QString audioErr;
    if (!m_audioController->playFromScript(sceneAudioSource(), &audioErr)) {
        // File audio mancante: non e' un errore di sintassi e non va mostrato
        // come tale (senza questo ramo il popup diceva "Syntax Error" con
        // dentro il marcatore MISSING_FILE| grezzo).
        if (audioErr.startsWith("MISSING_FILE|")) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle("Sound File Not Found");
            box.setText("The sound file was not found.");
            box.setInformativeText(audioErr.section('|', 1));
            box.exec();
        } else {
            showShaderError("Syntax Error (Sound Script)",
                                                       audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
        }
        updateMasterButtonState();
        return;
    }

    // Avvio esplicito dell'utente: riarma il riavvio automatico del suono.
    m_userStoppedSound = false;

    // playFromScript ha già impostato "Stop Sound" sul ramo che suona.
    updateMasterButtonState();
}

// LE DIRETTIVE ":=" DI UNO SCRIPT, lette e basta (funzione PURA): i valori di
// limiti e costanti nell'ordine in cui compaiono, le costanti discrete, i minimi
// e MESH_VISIBLE. Le applica parseAndApplyScriptParams; le usa anche
// sceneFromItem per sapere che scena lascera' il load.
MainWindow::ScriptDirectives MainWindow::parseScriptDirectives(const QString &scriptCode)
{
    ScriptDirectives sd;
    // SOLO direttive ":=" (es. "A := 1.2", "u_min := -3.0"), NON il "=" nudo del
    // codice GLSL. Il vecchio "[:=]+" catturava anche righe legittime come
    // "float a = min(B, A);": con CaseInsensitive, "a" -> costante A, e
    // evaluateSimple("min(B, A)") = 0 azzerava A (ergosfera ridotta a un punto).
    // Tutte le vere direttive dei preset usano ":=", quindi richiederlo è sicuro.
    QRegularExpression re(R"(\b(u_min|u_max|v_min|v_max|w_min|w_max|steps|A|B|C|D|E|F|S)\b\s*:=\s*([^;]+);)",
                          QRegularExpression::CaseInsensitiveOption);

    // Rimuoviamo i commenti prima di cercare le assegnazioni di costanti: altrimenti
    // un commento come "// A: numero di buchi" verrebbe interpretato come "A = ..."
    // e azzererebbe la costante (il testo non è valutabile -> evaluateSimple = 0).
    const QString cleanScript = stripCodeComments(scriptCode);

    // --- COSTANTI DISCRETE: "A := int(1,6);" -------------------------------
    // Dichiara che A assume solo valori interi fra 1 e 6. NON è un valore, quindi
    // va riconosciuta PRIMA del ciclo sotto: quello passerebbe "int(1,6)" a
    // evaluateSimple, che non conosce int() e restituirebbe 0 -> costante azzerata.
    // Le lettere trovate qui vengono escluse dal ciclo (skipDiscrete).
    QSet<QString> skipDiscrete;
    {
        QRegularExpression reInt(R"(\b([A-FS])\b\s*:=\s*int\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*;)",
                                 QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = reInt.globalMatch(cleanScript);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            const QString name = m.captured(1).toUpper();
            int lo = m.captured(2).toInt();
            int hi = m.captured(3).toInt();
            if (lo > hi) std::swap(lo, hi);      // "int(6,1)" tollerato
            sd.discrete.insert(name, { lo, hi });
            skipDiscrete.insert(name);
        }

        // "F := min(0.3);" — soglia inferiore su una costante che resta CONTINUA
        // (sotto quel valore la figura degenera). Come sopra: e' una dichiarazione,
        // non un valore, quindi va consumata qui o evaluateSimple("min(0.3)")
        // azzererebbe la costante.
        QRegularExpression reMin(R"(\b([A-FS])\b\s*:=\s*min\s*\(\s*(-?[\d.]+)\s*\)\s*;)",
                                 QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator itm = reMin.globalMatch(cleanScript);
        while (itm.hasNext()) {
            QRegularExpressionMatch m = itm.next();
            const QString name = m.captured(1).toUpper();
            sd.mins.insert(name, m.captured(2).toFloat());
            skipDiscrete.insert(name);
        }

        // "MESH_VISIBLE := E;" — quante mesh sono davvero a schermo quando un
        // //CUTOUT ne spegne una parte. Si conserva l'ESPRESSIONE: dipende dalle
        // costanti e va rivalutata a ogni loro cambio (vedi meshVisibleCount).
        QRegularExpression reMeshVis(R"(\bMESH_VISIBLE\b\s*:=\s*([^;]+);)",
                                     QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch mv = reMeshVis.match(cleanScript);
        if (mv.hasMatch()) sd.meshVisible = mv.captured(1).trimmed();
    }

    QRegularExpressionMatchIterator i = re.globalMatch(cleanScript);
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        const QString varName = match.captured(1).toLower(); // es. "u_min"
        // "A := int(1,6);" e' una DICHIARAZIONE di dominio, gia' consumata sopra:
        // qui va saltata, altrimenti evaluateSimple("int(1,6)") = 0 azzera A.
        if (skipDiscrete.contains(varName.toUpper())) continue;
        sd.values.append({ varName, match.captured(2) });     // es. "-3.14" o "2*PI"
    }
    return sd;
}

void MainWindow::parseAndApplyScriptParams(const QString &scriptCode, bool restartAudio,
                                           bool onlyFillEmptyLimits)
{
    const ScriptDirectives sd = parseScriptDirectives(scriptCode);
    // Azzerate SEMPRE: un preset senza direttive deve tornare a costanti continue,
    // senza minimi e senza MESH_VISIBLE, altrimenti quelli del preset precedente
    // resterebbero attivi (stessa famiglia di bug del cutout che persisteva fra
    // superfici).
    m_scene.discreteConsts = sd.discrete;
    m_scene.minConsts = sd.mins;
    m_meshVisibleExpr = sd.meshVisible;

    bool limitsChanged = false;

    // Run lanciato dal LOAD di un preset: la scena e' il file, gia' assegnata in
    // testa ad applyCommonData (costanti, domini, limiti). Le direttive di
    // VALORE non si riapplicano: valgono al Run dell'utente. Prima passavano
    // dagli slider (troncate al centesimo) e dai campi limite, riscrivendo a
    // meta' load cio' che il file aveva gia' detto.
    static const QList<QPair<QString, QString>> kNoValues;
    for (const auto &directive : m_scriptRunFromLoad ? kNoValues : sd.values) {
        const QString &varName = directive.first;
        const QString &valStr  = directive.second;

        // Usiamo il tuo parser per calcolare il valore (es. "2*PI" -> 6.28)
        float value = ExpressionParser::evaluateSimple(valStr);

        // onlyFillEmptyLimits (Run manuale dello script metrico): vince lo stato
        // UI corrente, come il tasto Run del dock Equations. Una direttiva di
        // limite scrive SOLO nel campo ancora vuoto; costanti A..F/S e steps non
        // vengono mai riapplicate (restano quelle della UI/slider).
        auto setLimitIfAllowed = [&](QLineEdit* edit) {
            if (onlyFillEmptyLimits && !lineText(edit).trimmed().isEmpty()) return;
            edit->setText(QString::number(value, 'g', 12));
            limitsChanged = true;
        };

        // Funzione per impostare valore E range dinamico dagli script
        if (varName == "u_min") { setLimitIfAllowed(ui->uMinEdit); }
        else if (varName == "u_max") { setLimitIfAllowed(ui->uMaxEdit); }
        else if (varName == "v_min") { setLimitIfAllowed(ui->vMinEdit); }
        else if (varName == "v_max") { setLimitIfAllowed(ui->vMaxEdit); }
        else if (varName == "w_min") { setLimitIfAllowed(ui->wMinEdit); }
        else if (varName == "w_max") { setLimitIfAllowed(ui->wMaxEdit); }
        else if (onlyFillEmptyLimits) { continue; }  // costanti/steps: vince la UI
        // 'steps' NON viene mai applicato dallo script: la risoluzione è governata
        // esclusivamente dallo slider stepSlider (inizializzato da d.steps del JSON al
        // caricamento). Prima la direttiva "steps := N" riportava lo slider a N ad ogni
        // Run, scavalcando la regolazione manuale dell'utente (es. preset Otto): lo
        // slider "non modificava" più lo step. La direttiva resta inerte nel testo.
        else if (varName == "steps") { /* ignorata: comanda lo slider */ }
        else if (varName == "a") { ui->aSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "b") { ui->bSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "c") { ui->cSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "d") { ui->dSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "e") { ui->eSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "f") { ui->fSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "s") { ui->sSlider->setValue(static_cast<int>(value * 100.0f)); }
    }

    // Se abbiamo cambiato i limiti, aggiorniamo subito le variabili interne del motore
    if (limitsChanged) {
        updateULimits();
        updateVLimits();
        updateWLimits();
    }

    if (!m_audioController->isPlaying()) {
        if (restartAudio) {
            QString globalCode = m_scene.soundScriptText + "\n" + scriptCode + "\n"
                    + m_scene.surfaceTextureCode + "\n" + m_scene.bgTextureCode;
            m_audioController->playFromScript(globalCode);
        }
    }
}

QString MainWindow::extractAudioDirectives(const QString& fullText) {
    QString extractedSound;
    // La scena ha UN audio. Lo stesso brano puo' arrivare da piu' slot (i Save
    // vecchi lo scrivevano sia nel codice della texture sia in quello dello
    // sfondo, es. Kerr Black Hole): va tenuto una volta sola, altrimenti il
    // Save lo riscrive doppio e il confronto con la voce della Library Sounds
    // non combacia piu'.
    QStringList seen;
    auto addOnce = [&](const QString &piece) {
        if (seen.contains(piece)) return;
        seen << piece;
        extractedSound += piece;
    };

    // 1. Estrae file MP3/WAV (con Smart Path Resolver integrato)
    QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator musicIt = musicRe.globalMatch(fullText);

    while (musicIt.hasNext()) {
        QRegularExpressionMatch match = musicIt.next();
        QString rawPath = match.captured(1).trimmed();

        // A. Il file e' LEGGIBILE al percorso originale? (non basta che esista:
        // vedi TextureCode::resolveImagePath per il perche')
        if (isReadableFile(rawPath)) {
            addOnce("//MUSIC: " + rawPath + "\n");
        } else {
            // B. Se il percorso è rotto, cerchiamo il file nella cartella Sounds locale
            QString fileName = QFileInfo(rawPath).fileName();
            QSettings settings;
            QString rootPath;

            rootPath = LibraryFolders::root();

            QString sndDir = settings.value("pathSounds", rootPath + "/sounds").toString();

            // Cerca il file in tutte le sottocartelle dei suoni
            QDirIterator it(sndDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

            if (it.hasNext()) {
                QString resolvedPath = it.next();
                addOnce("//MUSIC: " + resolvedPath + "\n"); // Sostituisce il path rotto con quello giusto!
            } else {
                // Fallback di sicurezza: rimette quello vecchio
                addOnce("//MUSIC: " + rawPath + "\n");
            }
        }
    }

    // 2. Estrae il formato procedurale (Sintesi GLSL).
    QRegularExpression blockRe(R"(//\s*SOUND_BEGIN(.*?)//\s*SOUND_END)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    // Marcatori interni residui: con blocchi annidati/duplicati (es. preset salvati
    // due volte con "//SOUND_BEGIN\n//SOUND_BEGIN\n...\n//SOUND_END\n//SOUND_END")
    // la cattura non-greedy include un //SOUND_BEGIN interno spurio. Li rimuoviamo
    // dal contenuto, cosi' l'output e' SEMPRE un blocco singolo pulito: altrimenti
    // m_scene.soundScriptText differisce dal codice salvato in libreria e il confronto
    // isAlreadyPresent (onSoundItemClicked) da' un falso negativo -> il click di
    // stop ricarica e riavvia il suono invece di fermarlo.
    QRegularExpression innerMarkerRe(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator blockIt = blockRe.globalMatch(fullText);
    while (blockIt.hasNext()) {
        QString inner = blockIt.next().captured(1);
        inner.remove(innerMarkerRe);   // toglie eventuali marcatori annidati residui
        addOnce("//SOUND_BEGIN\n" + inner.trimmed() + "\n//SOUND_END\n");
    }

    return extractedSound.trimmed();
}

QString MainWindow::stripAudioDirectives(QString code)
{
    static const QRegularExpression musicRe(R"(^\s*//MUSIC:.*$\n?)", QRegularExpression::MultilineOption);
    static const QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression markerRe(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    code.remove(musicRe);
    while (code.contains(blockRe)) code.remove(blockRe);   // blocchi duplicati o annidati
    code.remove(markerRe);
    return code;
}

void MainWindow::updateScriptButtonText() {
    bool isRayMarching = (implicitMode());
    bool isBackground = editingBackground();

    // LA VISTA PRIMA DEI TASTI: l'editor mostra lo slot del modulo corrente
    // (bersaglio e fascia compresi). Chiunque riallinei i tasti riallinea cosi'
    // anche l'editor; se il testo e' gia' quello non succede nulla.
    refreshScriptEditor();
    const ScriptSlot shownSlot = shownScriptSlot();
    QString rawText = scriptText(shownSlot);

    // 1. ANALISI DEL TESTO: Cerchiamo il VERO codice GLSL
    QString codeOnly = rawText;
    // Rimuoviamo i tag audio e immagine per valutare se c'è logica procedurale
    codeOnly.remove(QRegularExpression(R"(^\s*//(SYNTH|IMG):.*$\n?)", QRegularExpression::MultilineOption));
    codeOnly = stripAudioDirectives(codeOnly);

    bool hasGLSLCode = !codeOnly.trimmed().isEmpty();
    bool hasAnyText = !rawText.trimmed().isEmpty();

    // 2. CONTROLLO MODIFICHE: cio' che e' scritto e' diverso da cio' che e' a
    // schermo (ultimo Run o preset)? Il confronto e' con l'APPLICATO, non con
    // una copia del testo: prima il riferimento era lo slot in cui l'editor
    // veniva travasato a ogni cambio di modulo o di bersaglio, e bastava un
    // giro per far risultare eseguito uno script mai eseguito.
    auto canonical = [](QString t) {
        t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        return t.trimmed();
    };
    const bool isModified = canonical(rawText) != canonical(appliedScriptText(shownSlot));

    // Regola 1: Tasto Modo sempre attivo per poter scorrere le Tab
    ui->btnScriptMode->setEnabled(true);

    // Variabili di stato per i pulsanti (Regole 2 e 4)
    bool enableRun = false;
    bool enableSave = false;

    // ==========================================
    // LOGICA PER TAB SUPERFICIE
    // ==========================================
    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setEnabled(true);

        // Lo stato del tasto rispecchia DIRETTAMENTE l'orologio della geometria,
        // non il testo dell'editor (vuoto al load per i record impliciti non-script).
        // Per gli script metrici la geometria è la mesh geodetica (ricalcolo CPU
        // via m_geoAnimTimer), non lo shader della superficie: vanno considerati
        // entrambi i clock, così il tasto resta sincronizzato col dock Equations.
        bool geoFlowRunning = (m_geoAnimTimer && m_geoAnimTimer->isActive());
        bool isSurfaceMoving = (ui->glWidget && ui->glWidget->isSurfaceAnimating())
                || geoFlowRunning;

        if (isRayMarching) {
            ui->btnScriptMode->setText("Implicit Surface");
            ui->btnRunCurrentScript->setText(isSurfaceMoving ? "Stop" : "Run");
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Implicit Surface (Ray Marching).\nExample: return length(p) - 1.0;");
        } else if (tubesShown()) {
            // Sotto-tab Solid Curve (nel codice: Tubes): lo script descrive la
            // CURVA, disegnata come tubo.
            ui->btnScriptMode->setText("Solid Curve");
            ui->btnRunCurrentScript->setText(isSurfaceMoving ? "Stop Solid Curve" : "Run Solid Curve");
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for the curve: its point at u, as vec4(x, y, z, p).\n"
                                                    "Example: return vec4(cos(u), sin(u), 0.3 * sin(3.0 * u), 0.0);\n"
                                                    "More curves: one //MESH_BEGIN section each, told apart by 'mesh'.");
        } else {
            ui->btnScriptMode->setText("Parametric Surface");
            ui->btnRunCurrentScript->setText(isSurfaceMoving ? "Stop Parametric" : "Run Parametric");
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Parametric Surface.\nExample: return vec4(0.2 * u - 0.5, 0.2 * v - 0.5, 0.2 * sin(u * v), 1.0);");
        }

        // Abilitazione del tasto Run/Stop del dock SCRIPT (regola richiesta):
        //   - ACCESO se c'e' un'animazione: o lo script sta gia' girando
        //     (isSurfaceMoving -> serve lo Stop), o il codice contiene 't'/'iTime'
        //     (un'animazione avviabile);
        //   - se NON c'e' animazione (script statico), resta SPENTO finche' non si
        //     modifica il testo (isModified) -> allora si puo' ri-eseguire.
        // Serve comunque del codice nell'editor: un RECORD non-script ha editor
        // vuoto e il tasto va disabilitato (l'animazione del record si governa da
        // master/Equations, non da qui).
        bool codeAnimated = hasTimeVariable(codeOnly);
        enableRun = hasGLSLCode && (isSurfaceMoving || codeAnimated || isModified);
        enableSave = hasGLSLCode;
    }

    // ==========================================
    // LOGICA PER TAB TEXTURE
    // ==========================================
    else if (m_currentScriptMode == ScriptModeTexture) {
        // Stato reale dell'orologio della texture interessata (sfondo o superficie).
        bool texMoving = false;
        if (ui->glWidget) {
            if (isBackground) {
                texMoving = ui->glWidget->isBackgroundTextureAnimating()
                        && ui->glWidget->isBackgroundTextureEnabled()
                        && hasTimeVariable(m_scene.bgTextureCode);
            } else if (editingBorder()) {
                // BORDO: l'orologio della SUA texture.
                texMoving = ui->glWidget->isBorderTextureAnimating()
                        && ui->glWidget->borderTextureShown()
                        && hasTimeVariable(ui->glWidget->borderTextureCode());
            } else if (ui->glWidget->activeMeshPart() >= 0) {
                // AMBITO "MESH": il tasto mostra lo stato della PARTE, non quello
                // globale -- stesso principio di editor, slider e radio, che
                // mostrano cio' che stanno per comandare. Leggendo il clock
                // globale il tasto direbbe "Stop" su una mesh gia' ferma (e
                // viceversa), e il click successivo agirebbe al contrario.
                texMoving = ui->glWidget->isActiveMeshTextureAnimating()
                        && ui->glWidget->activeMeshTextureActive()
                        && hasTimeVariable(ui->glWidget->activeMeshTextureCode());
            } else {
                // AMBITO "ALL": il tasto comanda la texture di SUPERFICIE, quindi
                // deve descrivere QUELLA. Si guarda m_scene.surfaceTextureCode e non
                // allSurfaceTextureCode(), che aggrega anche gli script delle
                // fasce: con una texture animata su una mesh il tasto di "All"
                // cambiava aspetto per un moto che non gli apparteneva (e
                // viceversa restava fermo quando la sua era in movimento).
                texMoving = ui->glWidget->isSurfaceTextureAnimating()
                        && surfaceTextureShown()
                        && hasTimeVariable(m_scene.surfaceTextureCode);
            }
        }

        // Stessa regola della superficie: ACCESO se c'e' animazione (texture in
        // movimento o codice con 't'/'iTime'), altrimenti SPENTO finche' non si
        // modifica il testo.
        bool texCodeAnimated = hasTimeVariable(codeOnly);

        if (editingBorder()) {
            ui->btnScriptMode->setText("Texture");
            ui->btnRunCurrentScript->setText(texMoving ? "Stop Border Texture" : "Run Border Texture");
            ui->txtScriptEditor->setEnabled(true);
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for the Border Texture.\nuv.x runs along the edge, uv.y around the tube.\nExample: return vec3(uv.x, uv.y, 0.5);");

            enableRun = hasGLSLCode && (texMoving || texCodeAnimated || isModified);
            enableSave = hasGLSLCode;
        } else if (isBackground) {
            ui->btnScriptMode->setText("Texture");
            ui->btnRunCurrentScript->setText(texMoving ? "Stop Background Texture" : "Run Background Texture");
            ui->txtScriptEditor->setEnabled(true);
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Background Texture.\nExample: return vec3(uv.x, uv.y, 0.5);");

            enableRun = hasGLSLCode && (texMoving || texCodeAnimated || isModified);
            enableSave = hasGLSLCode;
        } else {
            if (isRayMarching) {
                ui->btnScriptMode->setText("Texture (Disabled)");
                ui->btnRunCurrentScript->setText("Not Available in Ray Marching");
                ui->txtScriptEditor->setEnabled(false);
                ui->txtScriptEditor->setPlaceholderText("Surface textures in Ray Marching are handled directly via the Equations Panel.");

                enableRun = false;
                enableSave = false;
            } else {
                ui->btnScriptMode->setText("Texture");
                ui->btnRunCurrentScript->setText(texMoving ? "Stop Parametric Texture" : "Run Parametric Texture");
                ui->txtScriptEditor->setEnabled(true);
                ui->txtScriptEditor->setPlaceholderText("Write GLSL for Parametric Texture.\nExample: return vec3(u / tau, v / tau, 1.0);");

                enableRun = hasGLSLCode && (texMoving || texCodeAnimated || isModified);
                enableSave = hasGLSLCode;
            }
        }
    }

    // ==========================================
    // LOGICA PER TAB SUONI
    // ==========================================
    else if (m_currentScriptMode == ScriptModeSound) {
        ui->btnScriptMode->setText("Sound");
        ui->txtScriptEditor->setEnabled(true);

        // ---> NUOVO PLACEHOLDER AUDIO <---
        ui->txtScriptEditor->setPlaceholderText("Add audio to your scene.\nExamples:\n//MUSIC: /path/to/song.mp3\n\n//SOUND_BEGIN\n// Write GLSL Synth Code here\n//SOUND_END");

        bool isPlaying = m_audioController && m_audioController->isPlaying();
        ui->btnRunCurrentScript->setText(isPlaying ? "Stop Sound" : "Run Sound");

        // I suoni agiscono come un interruttore Play/Stop, ignoriamo "isModified" qui
        enableRun = hasAnyText || isPlaying;
        enableSave = hasAnyText;
    }

    // Applicazione finale degli stati
    ui->btnRunCurrentScript->setEnabled(enableRun);
    ui->btnSaveScript->setEnabled(enableSave);

    // Aggiorniamo a cascata il bottone 2D
    updateFlatPreviewButton();

    ui->txtScriptEditor->update();
    if (ui->txtScriptEditor->viewport()) {
        ui->txtScriptEditor->viewport()->update();
    }
}

QString MainWindow::wrapSoundCode(const QString &sound)
{
    const QString s = sound.trimmed();
    if (s.isEmpty() || s.startsWith("//MUSIC:") || s.contains("//SOUND_BEGIN")) return s;
    return "//SOUND_BEGIN\n" + s + "\n//SOUND_END";
}

QString MainWindow::sceneAudioSource() const
{
    const QString code = soundCode() + "\n" + m_scene.surfaceScriptApplied + "\n"
                         + m_scene.surfaceTextureCode + "\n" + m_scene.bgTextureCode;
    return code.trimmed().isEmpty() ? scriptText(shownScriptSlot()) : code;
}
