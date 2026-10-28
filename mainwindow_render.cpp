// mainwindow_render.cpp - MainWindow: resa -- Base/Phong/Wireframe, trasparenza, Shell/Solid
// e marcher, scena vuota, aspetto per-mesh e ambito All/Mesh.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

// Dock Renderer: resa, colori, luci, trasparenza, texture. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupRendererDock()
{
    // =========================================================================
    // 7. RENDERER, COLORS & LIGHTING
    // =========================================================================
    ui->glWidget->setProjectionMode(1);
    updateProjectionButtonText();

    // Il gruppo esclusivo gestisce SOLO la modalità di rendering della superficie
    // (Base / Phong / Wireframe): sono mutuamente esclusivi perché la superficie
    // viene disegnata in un solo modo. Il Background NON ne fa parte: è una funzione
    // indipendente (texture di sfondo) e va attivata/disattivata senza spegnere la
    // modalità superficie e viceversa.
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->addButton(ui->radioBasic, 0);
    m_modeGroup->addButton(ui->radioPhong, 1);
    m_modeGroup->addButton(ui->radioWF,    2);
    m_modeGroup->setExclusive(true);

    // TARGET DI EDITING: terna esclusiva Surface / Border / Background, in testa
    // ai riquadri della superficie e dello sfondo (stanno in contenitori diversi:
    // l'esclusivita' la da' il gruppo, non i genitori). Sceglie COSA pilotano gli
    // slider/texture/colore. NON tocca la modalità di rendering della superficie
    // (Base/Phong/Wireframe), che resta un asse a sé nel m_modeGroup. Il
    // bersaglio e' STATO (m_editTarget, letto con editingBackground() /
    // editingBorder()); i tre radio ne sono la vista. radioSurface ha assorbito
    // il vecchio radioEditSurf.
    // I due color slot della texture (radioTexColor1/2) stanno in m_colorGroup a parte;
    // l'esclusività FRA i due gruppi è mantenuta a mano (vedi setColorTargetExclusive).
    m_bgTargetGroup = new QButtonGroup(this);
    m_bgTargetGroup->addButton(ui->radioSurface);
    m_bgTargetGroup->addButton(ui->radioBorder);
    m_bgTargetGroup->addButton(ui->radioBackground);
    m_bgTargetGroup->setExclusive(true);
    // Default = editing superficie. Impostato PRIMA della connect dell'handler di
    // radioBackground (più sotto) così non scatena logica al boot.
    setEditTarget(EditTarget::Surface);
    // I due gruppi (coppia m_bgTargetGroup e color slot m_colorGroup) sono INDIPENDENTI:
    // la coppia dice DOVE operi (Surface/Background), Color1/2 dicono QUALE tinta
    // della texture editi quando una texture colorata è attiva. Possono essere accesi
    // entrambi (es. Surface + Color 1). Nessuna deselezione incrociata: cliccare un radio
    // della tripla lascia intatta la coppia Color1/2 e viceversa. Qui basta riallineare
    // gli slider. Background porta la sua logica nell'handler toggled dedicato (che chiama
    // già onColorTargetChanged), quindi per Background non rifacciamo nulla.
    connect(m_bgTargetGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *btn){
        if (btn == ui->radioBackground) return;
        // Fra Surface e Border il gestore di radioBackground non scatta: il
        // bersaglio lo scrive il clic.
        setEditTarget(btn == ui->radioBorder ? EditTarget::Border : EditTarget::Surface);
        // Border col bordo spento lo accende allo spessore di default (0.50):
        // il clic deve avere un effetto visibile. Valore esatto nel motore e
        // nell'etichetta, slider sulla sua posizione; per spegnerlo si riporta
        // lo slider a 0.
        if (btn == ui->radioBorder && ui->glWidget && ui->glWidget->borderRadius() <= 0.0f
            && ui->borderThicknessSlider->value() == 0) {
            const float t = kBorderThicknessDisplayDefault * kBorderThicknessUnit;
            {
                const QSignalBlocker blocker(ui->borderThicknessSlider);
                ui->borderThicknessSlider->setValue(sliderFromBorderThickness(t));
            }
            ui->glWidget->setBorderRadius(t);
            ui->lblValBorderThickness->setText(borderThicknessText(t));
        }
        onColorTargetChanged();
        // Base/Phong/WireFrame e densita' mostrano ora il bersaglio nuovo, e il
        // dock Script la sua texture (col suo tasto Run/Stop).
        updateRenderState();
        updateScriptButtonText();
        syncTextureTreeSelection();
    });

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id){
        // Col bersaglio Border il clic e' gia' stato scritto sul bordo
        // (onUserRenderModeChosen, dal toggled che precede): la superficie --
        // modalita' globale, texture, editor -- non c'entra.
        if (editingBorder()) return;
        // Click su un radio della superficie (Base/Phong/Wireframe).
        // NON tocchiamo il Background: è indipendente. Se il dock texture sta
        // attualmente editando lo sfondo (radioBackground acceso) lasciamo invariati
        // editor, checkbox e picker: continuano a riferirsi allo sfondo.
        //
        // m_scene.renderMode e' lo stato GLOBALE della superficie, quello che le
        // mesh senza modalita' propria ereditano e che il preset salva. Quando
        // si sta editando una SINGOLA mesh (spinbox diverso da "All") il click
        // riguarda solo quella parte, non la superficie intera: registrarlo
        // come globale lo faceva riapplicare a TUTTE le mesh al ricarico del
        // preset (sequenza: seleziono 5, Phong, wireframe, ricarico -> tutto
        // wireframe, perche' updateRenderState rileggeva m_scene.renderMode=2 e
        // col bypass del load lo scriveva sul globale).
        const bool editingSingleMesh =
            ui->glWidget && ui->glWidget->activeMeshPart() >= 0;
        if (!editingSingleMesh)
            m_scene.renderMode = id;

        if (!editingBackground()) {
            refreshTextureCheckbox();

            updateTextureUIState(m_scene.surfaceTextureState);
            // LO SPEGNIMENTO PER WIREFRAME RIGUARDA SOLO L'AMBITO "ALL".
            // setGlobalTextureEnabled scrive m_textureEnabled, che e' lo stato GLOBALE
            // della texture di superficie: mettendo in wireframe UNA fascia si
            // spegneva la texture di tutta la superficie, e tornando su "All" il
            // checkbox rileggeva quello stato e la texture risultava persa.
            // Con una fascia selezionata la modalita' e' una proprieta' di
            // QUELLA parte (la scrive onUserRenderModeChosen su MeshPart), e il
            // render decide gia' da se' di non texturizzare una parte in
            // wireframe: non c'e' nulla da spegnere sul globale.
            applySurfaceTextureToEngine();

            // In Wireframe gli slider editano il colore uniforme delle linee: il pallino
            // di lavoro va su "Surface". updateTextureUIState sopra ha già spento Color1/2
            // (surfaceWireframe), qui assicuriamo che la tripla sia su Surface.
            // Col bersaglio Border gli slider restano sul bordo: il wireframe
            // non cambia nulla per lui.
            if (id == 2 && ui->radioSurface->isEnabled() && !editingBorder()) {
                selectSurfaceColorTarget();
            }
            onColorTargetChanged();
        }

        updateFlatPreviewButton();
        updateRenderState();
        syncTextureTreeSelection();
        updateScriptButtonText();
    });

    // BACKGROUND: toggle indipendente. Acceso = il dock texture edita lo sfondo e
    // (se c'è codice) lo sfondo è attivo; spento = il dock torna a editare la
    // superficie. NON spegne né accende i radio Base/Phong/Wireframe: la modalità
    // di rendering della superficie resta quella che era.
    connect(ui->radioBackground, &QRadioButton::toggled, this, [this](bool checked){
        // Il clic: prima lo STATO (e la vista 2D del motore), che tutto il
        // resto del dock legge. Arriva solo dai clic: il programma scrive i
        // radio a segnali bloccati (setEditTarget). In uscita il radio cliccato
        // e' gia' acceso (il gruppo spegne questo dopo aver acceso quello): puo'
        // essere Surface o Border.
        setEditTarget(checked ? EditTarget::Background
                              : ui->radioBorder->isChecked() ? EditTarget::Border
                                                             : EditTarget::Surface);

        if (checked) {
            // ENTRO in editing sfondo. Lo stato della texture di superficie
            // (m_scene.surfaceTextureState) non si tocca: il checkbox ne era solo la
            // vista, e ora mostra lo sfondo. (Prima lo si ricopiava DAL checkbox
            // per "salvarlo": in ambito Mesh il checkbox mostra la fascia, e la
            // copia accendeva una texture globale che non esisteva.)

            if (m_currentScriptMode == ScriptModeTexture) {
                // Niente da travasare: gli slot sono lo stato, l'editor la loro
                // vista -- updateScriptButtonText lo porta sullo sfondo. (Prima
                // qui l'editor si salvava nello slot della texture di superficie,
                // e in ambito "Mesh" ci finiva lo script della fascia.)
                ui->btnRunCurrentScript->setText("Run Background Texture");
                updateScriptButtonText();
            }

            bool bgTexActive = ui->glWidget->isBackgroundTextureEnabled();
            refreshTextureCheckbox();   // etichetta, abilitazione e spunta dello sfondo

            // Surface resta cliccabile anche in editing sfondo: la coppia Surface/Background
            // è l'asse di scelta della scena, quindi disabilitare Surface impedirebbe di
            // tornare alla superficie. Cliccare Surface esce da Background (esclusività di
            // m_bgTargetGroup) e il ramo "else" di questo handler ripristina il contesto
            // superficie. Niente disabilitazione di radioSurface.

            // Picker Colore attivi solo se lo sfondo è acceso E usa quel colore:
            // ciascuno indipendente (una texture che usa solo u_col1 non abilita col2).
            bool bgCol1 = bgTexActive && m_scene.bgTextureCode.contains("u_col1");
            bool bgCol2 = bgTexActive && m_scene.bgTextureCode.contains("u_col2");
            ui->radioTexColor1->setEnabled(bgCol1);
            ui->radioTexColor2->setEnabled(bgCol2);

            if (bgCol1 || bgCol2) {
                // Accendiamo il picker ABILITATO (col1 se usato, altrimenti col2).
                QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
                bool oldBlock2 = target->blockSignals(true);
                target->setChecked(true);
                target->blockSignals(oldBlock2);
            } else {
                // Sfondo senza colori: oltre a disabilitarli, DESELEZIONIAMO i color slot,
                // altrimenti un Color rimasto checked dalla superficie precedente mostra un
                // pallino fantasma (grigio ma acceso). uncheckInExclusiveGroup perché un
                // setChecked(false) diretto sull'unico acceso di un gruppo esclusivo è no-op.
                uncheckInExclusiveGroup(ui->radioTexColor1);
                uncheckInExclusiveGroup(ui->radioTexColor2);
            }

        }
        else {
            // ESCO dall'editing sfondo: il dock torna alla superficie. Lo sfondo
            // resta com'è a video (acceso o spento), spegnere il radio significa
            // solo "non sto più editando lo sfondo".
            if (m_currentScriptMode == ScriptModeTexture) {
                // USCENDO da Background il dock torna a mostrare cio' che il
                // bersaglio comanda: in ambito "Mesh" lo script della FASCIA,
                // altrimenti la texture di superficie (shownScriptSlot).
                ui->btnRunCurrentScript->setText("Run Shape Texture");
                updateScriptButtonText();
            }

            // radioSurface non viene più disabilitato entrando in Background,
            // quindi qui non c'è nulla da riabilitare.

            // Ripristino la checkbox e la sua etichetta.
            // AMBITO "MESH": il checkbox e' il DISPLAY della fascia, non lo
            // stato della texture GLOBALE (entrando in Background la selezione
            // della parte viene lasciata com'era apposta, vedi
            // updateMeshScopeEnabled, quindi uscendo siamo ancora su quella
            // fascia). Ripristinando m_scene.surfaceTextureState si riaccendeva il
            // checkbox su una fascia a cui la texture era appena stata TOLTA.
            // Stesso criterio del ramo di ingresso, che per questo non salva
            // m_scene.surfaceTextureState quando showingMeshTex.
            const bool onMeshScope = ui->glWidget
                                     && ui->glWidget->activeMeshPart() >= 0;
            const bool texOn = onMeshScope
                                   ? ui->glWidget->activeMeshTextureActive()
                                   : m_scene.surfaceTextureState;

            refreshTextureCheckbox();

            updateTextureUIState(texOn);
            // Questo resta sul GLOBALE anche in ambito Mesh: e' lo stato della
            // texture di SUPERFICIE nel motore, che l'ambito non cambia -- la
            // fascia ha il proprio interruttore in MeshPart. Solo il DISPLAY
            // qui sopra segue la parte.
            applySurfaceTextureToEngine();

            // Uscendo da Background torniamo a editare la superficie: updateTextureUIState
            // sopra ha già messo il target colore su Surface (selectSurfaceColorTarget).
            // onColorTargetChanged() finale allinea gli slider.
        }

        onColorTargetChanged();
        updateFlatPreviewButton();
        updateRenderState();
        // Entrando in Background il selettore All/Mesh non ha senso (lo sfondo
        // non ha fasce): si disabilita e l'ambito torna ad "All", cosi' i
        // comandi non restano dirottati su una mesh. Uscendo si riabilita da
        // solo, se la superficie e' multi-mesh.
        updateMeshScopeEnabled();
        syncTextureTreeSelection();
        updateScriptButtonText();
    });

    refreshRenderRadios();

    // I radio sono DISPLAY (mostrano la modalita' della mesh selezionata) e
    // COMANDO (l'utente li clicca). Solo il secondo caso deve scrivere una
    // modalita' PROPRIA sulla parte attiva: quando e' syncAppearanceControls-
    // ToActiveMesh a muoverli, m_syncingMeshControls e' vero e qui non si scrive
    // nulla. Senza questa distinzione ogni updateRenderState (cambio tab,
    // proiezione, load) riapplicava il valore a un destinatario variabile ed era
    // la causa del wireframe che si propagava a tutte le mesh.
    auto onRenderRadioToggled = [this](bool checked){
        if (!checked) return;                 // interessa solo chi si accende
        if (!m_syncingMeshControls) onUserRenderModeChosen();
        updateRenderState();
    };
    connect(ui->radioBasic, &QRadioButton::toggled, this, onRenderRadioToggled);
    connect(ui->radioPhong, &QRadioButton::toggled, this, onRenderRadioToggled);
    connect(ui->radioWF,    &QRadioButton::toggled, this, onRenderRadioToggled);

    // Stato iniziale dei controlli densità Wireframe: al boot la modalità è Base, quindi
    // vanno disabilitati. updateRenderState (che li gestisce) non viene chiamato a tempo
    // di costruzione — solo dagli handler dei radio dopo un click — e setChecked(true) su
    // radioBasic sopra non emette toggled finché le connect non sono in piedi: senza questo
    // restavano attivi (stato di default della .ui) finché non si cliccava un radio.
    ui->uDensity->setEnabled(false);
    ui->vDensity->setEnabled(false);

    // (Qui c'era un secondo gestore di radioWF, che spegneva a mano il
    // checkbox Texture. Il checkbox e' una vista: lo allinea
    // refreshTextureCheckbox, chiamata da updateRenderState qui sopra.)

    ui->alphaSlider->setRange(0, 100);
    ui->alphaSlider->setValue(100);
    ui->glWidget->setAlpha(1.0f);
    ui->lblAlphaVal->setText("1.00");

    connect(ui->alphaSlider, &QSlider::valueChanged, this, [this](int value){
        // Su campo implicito a PRODOTTO ("Chain") la trasparenza fa sparire la superficie.
        // Se e' l'UTENTE ad abbassare lo slider (non un set programmatico di caricamento
        // preset), ripristiniamo l'opacita', mostriamo il popup UNA volta e blocchiamo.
        if (!m_settingAlphaProgrammatic && value < 100) {
            bool isImplicitMode = (implicitMode());
            bool illImplicit = isImplicitMode && ui->glWidget && ui->glWidget->isImplicitIllConditioned();
            if (illImplicit) {
                onAlphaSliderMovedIllCheck(value);
                return;   // onAlphaSliderMovedIllCheck ha gia' rimesso alpha a 100
            }
            // SOLO ANDROID: superficie la cui trasparenza puo' degradare (Gyroid, script
            // RM). NON blocchiamo: mostriamo l'avviso una volta e proseguiamo applicando
            // l'alpha normalmente (lo slider agisce, l'utente vede l'effetto reale).
            bool warnImplicit = isImplicitMode && ui->glWidget && ui->glWidget->implicitTransparencyMayDegrade();
            if (warnImplicit) {
                onAlphaSliderMovedWarnCheck();   // no return: l'alpha si applica sotto
            }
            // CONFERMA MISURATA (tutte le piattaforme): se la GPU e' GIA' sotto
            // carico pesante con la scena opaca (EMA del watchdog, significativa
            // solo ad animazione in corso), il ramo trasparente (~4-12x il costo
            // per pixel) porta quasi certamente al collasso, che il watchdog
            // fermerebbe solo DOPO il magenta. Chiediamo QUI, prima che il primo
            // frame trasparente venga renderizzato. Sulle scene fluide o ferme
            // non scatta mai (renderingUnderHeavyLoad e' false a riposo).
            if (isImplicitMode && !m_alphaHeavyWarnShown &&
                ui->glWidget && ui->glWidget->renderingUnderHeavyLoad()) {
                // Eventi della STESSA presa arrivati col box gia' aperto, o dopo
                // un "Keep it opaque": riassorbiti a 100 senza riaprire nulla,
                // altrimenti il drag ancora in corso impila/riapre il box in
                // loop (finestra che "permane o ricompare"). Una nuova presa
                // riarma via sliderPressed; il cambio superficie via sync.
                if (m_alphaHeavyPopupActive || m_alphaHeavyDeclined) {
                    m_settingAlphaProgrammatic = true;
                    ui->alphaSlider->setValue(100);
                    m_settingAlphaProgrammatic = false;
                    return;
                }
                m_alphaHeavyPopupActive = true;
                // Slider subito a 100 PRIMA del box modale: mentre e' aperto
                // nessun frame trasparente parte (pattern di onAlphaSliderMovedIllCheck).
                m_settingAlphaProgrammatic = true;
                ui->alphaSlider->setValue(100);
                m_settingAlphaProgrammatic = false;

                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(tr("Transparency on a heavy scene"));
                box.setText(tr("The GPU is already under heavy load with this "
                               "animation."));
                box.setInformativeText(tr("Transparency multiplies the per-pixel "
                                          "cost of ray marching and would very "
                                          "likely make rendering collapse (on "
                                          "some devices it can freeze the "
                                          "application).\n\n"
                                          "You can apply it anyway at your own "
                                          "risk, or keep the surface opaque."));
                QPushButton *applyBtn  = box.addButton(tr("Apply anyway"), QMessageBox::AcceptRole);
                QPushButton *opaqueBtn = box.addButton(tr("Keep it opaque"), QMessageBox::RejectRole);
                box.setDefaultButton(opaqueBtn);
                box.exec();
                if (box.clickedButton() == applyBtn) {
                    // Conferma data: applichiamo il valore richiesto in modo
                    // programmatico (i check di questa invocazione sono gia'
                    // stati fatti) e non richiediamo piu' per questa superficie.
                    m_alphaHeavyWarnShown = true;
                    m_settingAlphaProgrammatic = true;
                    ui->alphaSlider->setValue(value);
                    m_settingAlphaProgrammatic = false;
                } else {
                    // "Keep it opaque": vale per TUTTO il gesto in corso — gli
                    // eventi residui della presa vengono riassorbiti sopra.
                    m_alphaHeavyDeclined = true;
                }
                m_alphaHeavyPopupActive = false;
                return;  // in entrambi i casi il set giusto e' gia' avvenuto sopra
            }
        }
        const float alpha = static_cast<float>(value) / 100.0f;
        ui->lblAlphaVal->setText(QString::number(alpha, 'f', 2));
        ui->glWidget->setAlpha(alpha);
    });

    // Nuova presa dello slider trasparenza: il "Keep it opaque" dato durante la
    // presa precedente valeva per QUEL gesto, non per sempre — riarma la conferma.
    // Riarma anche il watchdog: se era stato zittito perche' avevamo disattivato
    // la trasparenza (guardTransparencyOnDisplacementApply -> ack), toccare di
    // nuovo lo slider e' l'atto esplicito dopo cui il watchdog deve tornare a
    // vigilare (setAlpha fa solo update(), NON passa da rebuildShader che
    // altrimenti lo riarmerebbe). E' il "a meno che non riporti alpha<1".
    connect(ui->alphaSlider, &QSlider::sliderPressed, this, [this](){
        m_alphaHeavyDeclined = false;
        if (ui->glWidget) ui->glWidget->rearmPerformanceWarning();
    });

    connect(ui->chkBoxTexture, &QCheckBox::toggled, this, [this](bool checked){
        // Il checkbox MOSTRA o NASCONDE una texture: non ne scrive nessuna, e
        // non e' lavoro da proteggere (segnalato dall'utente: l'avviso usciva
        // su una texture mai toccata). Cio' che cambia qui dentro entra quindi
        // anche nei riferimenti puliti. Non durante load e reset, che segnano
        // il loro momento pulito all'uscita.
        std::optional<AbsorbChangesGuard> absorbToggle;
        if (m_uiReady) absorbToggle.emplace(this);
        // BORDO: accende o spegne la SUA texture. Senza un codice proprio parte
        // dalla scacchiera di default, coi suoi due colori, come una fascia.
        if (editingBorder()) {
            GLWidget *gl = ui->glWidget;
            if (!gl) return;
            QString code = gl->borderTextureCode();
            if (checked && code.trimmed().isEmpty()) {
                code = TextureCode::defaultMeshCode();
                gl->setBorderTexColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
            }
            gl->setBorderTexture(code, checked);
            gl->setBorderTextureAnimating(checked && hasTimeVariable(code) && !m_masterStopped
                                          && !m_userStoppedBorderTexClock);
            updateTextureUIState(checked, true);
            updateMasterButtonState();
            syncTextureTreeSelection();
            refreshConstants();
            return;
        }
        // ==========================================================
        // TEXTURE DELLA SOLA MESH SELEZIONATA
        // ==========================================================
        // Con l'ambito su "Mesh" e una parte attiva, il checkbox accende o
        // spegne la texture di QUELLA parte, come fanno colore/alpha/luce.
        // Va PRIMA di tutto il resto: il ramo sotto scrive lo stato GLOBALE
        // (setGlobalTextureEnabled + generateTexture), che finisce nell'UBO di ogni
        // parte e texturizzava l'intera superficie -- oltre a far diventare
        // bianche le mesh in wireframe, che venivano disegnate col colore
        // della texture invece che col proprio.
        // In "All" (nessuna parte attiva) si prosegue col percorso di sempre.
        // Escluso il ramo Background (la texture di sfondo non e' per-mesh) e
        // il Ray Marching, che non ha parti di mesh.
        if (!editingBackground()
            && !implicitMode()
            && ui->glWidget && ui->glWidget->activeMeshPart() >= 0) {

            // Codice da usare per QUESTA parte: quello che ha gia' (riaccensione)
            // oppure la scacchiera di default, cosi' accendere il checkbox
            // produce sempre qualcosa di visibile come sulla superficie intera.
            QString code = ui->glWidget->activeMeshTextureCode();
            if (checked && code.trimmed().isEmpty())
                code = TextureCode::defaultMeshCode();

            // COLORI DELLA TEXTURE ALLA GPU. La scacchiera di default e' tutta
            // costruita su u_col1/u_col2 (mix dei due), che arrivano dall'UBO
            // via setGlobalTextureColors: senza questa riga i due slot restano ai
            // valori stantii del contesto precedente e, se coincidono, la
            // scacchiera esce in TINTA UNITA (il caso "tutto bianco").
            // Il ramo globale del checkbox fa lo stesso poco piu' sotto: qui
            // serve perche' quel ramo non viene percorso.
            // I colori di default valgono solo per una texture NUOVA. Se la
            // parte ne aveva già di propri (riaccensione, o rientro su una mesh
            // già texturizzata) vanno RIPRISTINATI i suoi: forzarli a
            // verde/nero qui riscriveva i colori di una mesh già configurata
            // solo perché la si era riselezionata.
            if (checked) {
                const auto &cparts = ui->glWidget->getEngine()->getMeshParts();
                const int ci = ui->glWidget->activeMeshPart();
                const MeshPart *cp = (ci >= 0 && ci < (int)cparts.size()) ? &cparts[ci] : nullptr;
                // I due slot GLOBALI restano quelli della texture di superficie:
                // qui si sta configurando una fascia, e i colori vanno nella
                // parte. setActiveMeshTexture poco sotto li copierebbe dai
                // globali solo a una parte che non ne ha di propri.
                if (!(cp && cp->hasCustomTexColors()))
                    ui->glWidget->setActiveMeshTexColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
            }

            // ACCENSIONE: applica lo script alla parte (via di COMANDO).
            // SPEGNIMENTO: si spegne soltanto, CONSERVANDO lo script --
            // setActiveMeshTexture e' la via di comando e riscriverebbe
            // textureCode forzando hasCustomTexture=true, cioe' rimetterebbe la
            // texture "in vita" proprio mentre la si sta spegnendo: l'editor
            // continuava a mostrarne lo script (il display guarda la texture
            // EFFICACE, e quella restava dichiarata).
            if (checked) ui->glWidget->setActiveMeshTexture(code, true);
            else         ui->glWidget->setActiveMeshTextureEnabled(false);

            // OROLOGIO DELLA TEXTURE. Accendere la texture di una mesh e' un
            // avvio esplicito del modulo, come il Run: riarma un eventuale stop
            // manuale e rivaluta se il clock serve. Il conto va fatto DOPO
            // setActiveMeshTexture, perche' allSurfaceTextureCode() legge lo
            // stato appena scritto sulla parte.
            m_userStoppedTexClock = false;
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(allSurfaceTextureCode()));

            // OROLOGIO DELLA PARTE. Ogni fascia ha il PROPRIO clock: accendere
            // solo quello globale (riga sopra) non la muove piu'. Senza questa
            // riga, riaccendendo il checkbox la texture tornava FERMA.
            // Solo se il suo script usa il tempo: una texture statica non deve
            // lasciare acceso un orologio a vuoto.
            if (checked) {
                m_userStoppedMeshTexClock = false;   // comando esplicito
                ui->glWidget->setActiveMeshTextureAnimating(hasTimeVariable(code));
            }

            // I picker Color 1/2 servono solo se lo script della parte li usa.
            updateTextureUIState(checked, true);
            updateFlatPreviewButton();

            // EDITOR E TASTI RIALLINEATI SUBITO. Questo ramo esce con return e
            // non passava dal display per-mesh: spegnendo il checkbox l'editor
            // restava con lo script della texture appena spenta, e si svuotava
            // solo al PRIMO EVENTO SUCCESSIVO che risincronizza (il "giro di
            // ritardo": serviva spegnere due volte perche' sparisse).
            // syncAppearanceControlsToActiveMesh e' il punto unico del display
            // per-mesh e decide da solo cosa mostrare, in base alla texture
            // EFFICACE della parte.
            syncAppearanceControlsToActiveMesh();
            updateMasterButtonState();

            ui->glWidget->update();
            return;
        }

        if (editingBackground()) {
            ui->glWidget->setBackgroundTextureEnabled(checked);
            // Picker Colore solo se lo sfondo è acceso E usa quel colore (indipendenti).
            bool bgCol1 = checked && m_scene.bgTextureCode.contains("u_col1");
            bool bgCol2 = checked && m_scene.bgTextureCode.contains("u_col2");
            ui->radioTexColor1->setEnabled(bgCol1);
            ui->radioTexColor2->setEnabled(bgCol2);

            if ((bgCol1 || bgCol2) && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
                QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
                bool oldBlock = target->blockSignals(true);
                target->setChecked(true);
                target->blockSignals(oldBlock);
            }

            if (!checked) {
                // Codice, percorso dell'immagine, ancora e GPU insieme: la
                // riaccensione riparte sempre dalla default, e il Save non trova
                // piu' il percorso dell'immagine appena tolta (lo scriveva come
                // //IMG: in un record con lo sfondo spento).
                forgetBackgroundTexture();

                // Lo slot dello sfondo e' vuoto (forgetBackgroundTexture): se
                // il dock lo sta mostrando, editor e tasti lo seguono.
                updateScriptButtonText();
            }

            onColorTargetChanged();
        }
        else {
            // CANCELLAZIONE SCRIPTS CON WARNING (SURFACE TEXTURE)
            if (!checked && !m_blockTextureGen) {
                // 1. Verifichiamo se c'è effettivamente del codice che andrebbe perso
                bool hasCode = false;
                bool isModified = m_textureModified;

                if (implicitMode()) { // Ray Marching
                    QString tex = m_scene.rm.texture.trimmed();
                    QString disp = m_scene.rm.displacement.trimmed();

                    // Ignoriamo le texture di DEFAULT generate automaticamente (non sono
                    // codice scritto dall'utente da proteggere): la scacchiera procedurale
                    // attuale e il vecchio Triplanar Mapping (per record/preset salvati prima).
                    bool isAutoDefault = tex.contains("sin(pModel.x * 20.0) * sin(pModel.y * 20.0)")
                                         || tex.contains("vec3 blend = abs(n_model);");
                    if (!tex.isEmpty() && !isAutoDefault) hasCode = true;
                    if (!disp.isEmpty()) hasCode = true;

                } else { // Parametrica
                    QString tex = m_scene.surfaceTextureCode.trimmed();

                    // Se c'è SOLO un tag immagine (es. //IMG:/percorso.png) senza logica a capo, ignoralo
                    bool isOnlyImage = tex.startsWith("//IMG:") && !tex.contains("\n");
                    if (!tex.isEmpty() && !isOnlyImage) hasCode = true;
                }

                // Cancellazione fisica del codice texture in memoria. DEVE avvenire
                // a OGNI spegnimento (non solo quando si modifica a mano), altrimenti
                // riaccendendo il checkbox il rebuildShader/generateTexture riusa il
                // codice ancora in memoria e ricompare l'ULTIMA texture caricata invece
                // della default. Il warning sotto decide solo se CHIEDERE conferma
                // (quando c'è codice scritto a mano che andrebbe perso).
                auto clearTextureMemory = [this]() {
                    if (implicitMode()) {
                        // Svuota i campi della tab Ray Marching
                        setRmText(&ImplicitTexts::texture, QString());
                        setRmText(&ImplicitTexts::displacement, QString());
                        if (ui->glWidget) {
                            ui->glWidget->setTextureCode("");
                            ui->glWidget->setDisplacementCode("");
                            // Anche l'immagine, come nel ramo parametrico: il
                            // campo non la nomina piu', e restava in GPU.
                            ui->glWidget->clearTexture();
                        }
                    } else {
                        // Svuota memoria e variabili parametriche (il motore
                        // torna allo shader standard)
                        commitSurfaceTextureCode(QString());
                        // Lo script con lei: l'editor, se lo mostra, lo segue.
                        setScriptText(SlotSurfaceTexture, QString());

                        if (ui->glWidget) ui->glWidget->clearTexture();
                    }
                    // TRASFORMAZIONE 2D: va azzerata insieme al codice. Zoom/pan/
                    // rotazione sono di MODULO, non della singola texture: restando
                    // in piedi, la scacchiera di default che ricompare alla
                    // riaccensione veniva disegnata attraverso l'inquadratura della
                    // texture precedente. Col record "Dynamic Mobius Band" (che salva
                    // zoom 9.36 per il proprio Shadertoy) la default si vedeva come un
                    // quadrato 2x2 invece che come la griglia 8x8; con uno zoom < 1
                    // salvato altrove appariva invece rimpicciolita. Stesso motivo per
                    // cui qui si azzerano gia' i colori texture nei due rami piu' sotto.
                    // setGlobalTexTransform allinea da se' anche il buffer di lavoro
                    // della vista 2D (in ambito "All"), che e' cio' che lo shader legge.
                    if (ui->glWidget)
                        ui->glWidget->setGlobalTexTransform(1.0f, QVector2D(0.0f, 0.0f), 0.0f);

                    // Codice perso/azzerato: lo stato "modificato" non ha più senso.
                    m_textureModified = false;
                };

                // MOSTRA IL WARNING SOLO SE C'È VERO CODICE *E* L'UTENTE LO HA MODIFICATO MANUALMENTE
                if (hasCode && isModified) {
                    // Stesso popup di ogni altra uscita senza salvare (Save /
                    // Don't save / Cancel): prima era un Si'/No che poteva solo
                    // buttare via il lavoro. Uniformita' dell'interfaccia, e in
                    // piu' qui si puo' finalmente salvare invece di perdere tutto.
                    if (!confirmDiscardUnsaved(ScopeTexture)) {
                        // Cancel: la texture resta accesa, e il checkbox -- la
                        // sua vista -- torna a dirlo.
                        refreshTextureCheckbox();
                        return; // Interrompe l'operazione
                    }

                    // Save o Don't save: in entrambi i casi si prosegue e si
                    // azzera. Dopo Save il codice e' su disco, quindi non si
                    // perde nulla; dopo Don't save la perdita e' voluta.
                    clearTextureMemory();
                } else {
                    // Nessun codice scritto a mano da proteggere (es. texture solo
                    // CARICATA da preset): niente warning, ma azzeriamo lo stesso così
                    // la riaccensione riparte sempre dalla texture di default.
                    clearTextureMemory();
                }
            }

            m_scene.surfaceTextureState = checked;
            updateTextureUIState(checked);
            applySurfaceTextureToEngine();

            if (!m_blockTextureGen && checked) {
                // --- LOGICA RAY MARCHING (Tab 1) ---
                if (implicitMode()) {
                    QString currentTex = m_scene.rm.texture.trimmed();

                    if (currentTex.isEmpty()) {
                        // Reset dei colori texture alla default: senza questo, dopo
                        // una texture RM con colori custom (u_col1/u_col2), la default
                        // ricompariva con i colori della precedente (l'UBO restava
                        // stantio). Simmetrico al ramo parametrico.
                        if (ui->glWidget)
                            ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);

                        // Default RM = scacchiera PROCEDURALE pilotata da u_col1/u_col2
                        // (come il preset "Checkboard"): niente immagine, così i picker
                        // Color 1/2 sono attivi sulla texture di default. Contratto texture
                        // RM: assegnare textureCol usando pModel e ubuf.u_col1/u_col2.
                        QString defaultRM =
                            "float pattern = sin(pModel.x * 20.0) * sin(pModel.y * 20.0);\n"
                            "if (pattern > 0.0) {\n"
                            "    textureCol = ubuf.u_col1;\n"
                            "} else {\n"
                            "    textureCol = ubuf.u_col2;\n"
                            "}";

                        // A segnali bloccati: questa e' la texture DI DEFAULT che
                        // l'accensione della checkbox fa comparire, non codice
                        // scritto dall'utente. Senza il blocco passerebbe da
                        // markRmTextureEdited come una digitazione. (Che non sia
                        // lavoro da salvare lo garantisce AbsorbChangesGuard, in
                        // cima a questo gestore.)
                        {
                            setRmText(&ImplicitTexts::texture, defaultRM);
                        }
                        if (ui->glWidget) ui->glWidget->setTextureCode(defaultRM);

                        // Texture procedurale, non immagine: l'immagine se n'e'
                        // gia' andata dalla GPU allo spegnimento (clearTextureMemory).

                        // Texture colorata appena attivata: il pallino dei Color va su
                        // Color 1 (resetColorTargetToFirst), la tripla resta dov'è
                        // (gruppi indipendenti). updateTextureUIState chiama già
                        // onColorTargetChanged. DEVE stare DOPO aver impostato lineTexture:
                        // activeTextureUsesColors() in RM legge u_col1/u_col2 da lineTexture.
                        updateTextureUIState(true, true);
                    }
                }
                // --- LOGICA PARAMETRICA (Tab 0) ---
                else {
                    // Niente immagine: generateTexture() qui sotto mette la
                    // scacchiera nel sampler al suo posto.

                    // Stacco dello SHADER PROCEDURALE residuo. A differenza dello sfondo
                    // (vedi setBackgroundTexture("background.png") al suo spegnimento), la
                    // texture di superficie parametrica e' uno shader custom applicato via
                    // loadCustomShader: se la superficie PRECEDENTE aveva una texture
                    // procedurale, quello shader resta agganciato -> riappare la vecchia
                    // texture senza animazione / coi colori falsati. Azzeriamo il codice in
                    // memoria e ripristiniamo lo shader standard prima di applicare la
                    // default (applyDefaultCheckerShader piu' sotto).
                    // NB: il bug residuo era SOLO sulle parametriche (le implicite
                    // ricompilano la texture nello shader SDF a ogni Run).
                    commitSurfaceTextureCode(QString());   // torna allo shader standard
                    setScriptText(SlotSurfaceTexture, QString());

                    // Reset dei colori texture alla default. Senza questo restano
                    // quelli del preset precedente e la scacchiera default
                    // ricompare coi colori vecchi. Stesso reset del ramo Ray Marching.
                    if (ui->glWidget)
                        ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);

                    // Texture colorata appena attivata: il pallino dei Color va su Color 1
                    // (resetColorTargetToFirst); la tripla resta dov'è (gruppi indipendenti).
                    updateTextureUIState(true, true);

                    generateTexture();
                    // Il visual della default e' lo shader procedurale, non l'immagine
                    // appena caricata (vedi applyDefaultCheckerShader). Se la bake
                    // fallisse resta lo shader standard -> fallback sull'immagine.
                    applyDefaultCheckerShader();
                }

                if (ui->glWidget) ui->glWidget->rebuildShader();
            }
        }

        updateFlatPreviewButton();

        // COSTANTI: spegnere una texture (di superficie o di sfondo) puo' far
        // cadere in disuso una lettera, accenderla sulla default pure. Senza
        // ricalcolo lo slider restava acceso col valore di prima, a muovere nulla.
        refreshConstants();

        // --- GESTIONE AUTOMATICA ANIMAZIONE (START/STOP) SICURA ---
        bool needsAnim = false;

        // 1. Controllo Equazioni Base
        if (implicitMode()) { // Ray Marching
            // Solo l'SDF (lineEquation/script) è geometria; il displacement
            // (lineVariations) è del modulo texture ed è controllato al punto 2.
            QString eq = activeImplicitEquationText() + " " + m_scene.surfaceScriptApplied;
            if (hasTimeVariable(eq)) needsAnim = true;
        } else { // Parametrica
            // Includere i campi Composition (lineU/lineV/lineW) e i vincoli espliciti:
            // 't' può vivere SOLO lì (es. U(u,v)=u+t*D) mentre X/Y/Z/P ne sono privi.
            // Senza questi, accendere/spegnere la texture ricalcolava needsAnim=false
            // e fermava per errore l'animazione della geometria. Stesso insieme di
            // rawEqsForT (onStartClicked) e mainEq (updateMasterButtonState).
            QString eq = m_scene.eq.x + " " + m_scene.eq.y + " " +
                         m_scene.eq.z + " " + m_scene.eq.p + " " +
                         m_scene.eq.u + " " + m_scene.eq.v + " " + m_scene.eq.w + " " +
                         m_scene.eq.explicitU + " " + m_scene.eq.explicitV + " " + m_scene.eq.explicitW + " " +
                         m_scene.surfaceScriptApplied;
            if (hasTimeVariable(eq)) needsAnim = true;
        }

        // 2. Controllo Texture Superficie (SOLO SE ABILITATA)
        // MULTI-MESH: il modulo e' attivo, e il suo 't' va cercato, anche quando
        // la texture ce l'ha solo una FASCIA. Qui si guardava m_scene.surfaceTextureCode
        // (la sola texture globale) con un gate sul checkbox / su
        // m_scene.surfaceTextureState: con l'animazione sulla fascia 1 e nessuna
        // texture globale, needsAnim usciva falso e il ramo sotto chiamava
        // applyAnimationState(false), fermando l'animazione.
        // Si vedeva SOLO applicando in Background la texture di DEFAULT: le
        // altre (script o immagine) portano un proprio codice e passano da
        // percorsi che non ricalcolano needsAnim, mentre la default arriva qui.
        bool isSurfTexActive = editingBackground() ? m_scene.surfaceTextureState : checked;
        if (!isSurfTexActive) isSurfTexActive = anyMeshTextureActive();
        if (isSurfTexActive) {
            QString tex = (implicitMode())
                    ? (m_scene.rm.texture + m_scene.rm.displacement)
                    : allSurfaceTextureCode();
            if (hasTimeVariable(tex)) needsAnim = true;
        }

        // 3. Controllo Texture Sfondo (SOLO SE ABILITATA)
        if (ui->glWidget && ui->glWidget->isBackgroundTextureEnabled()) {
            if (hasTimeVariable(m_scene.bgTextureCode)) needsAnim = true;
        }

        // 4. APPLICAZIONE STATO E AGGIORNAMENTO UI
        if (needsAnim) {
            if (isAnythingMoving()) {
                applyAnimationState(true);
            }
        } else {
            applyAnimationState(false);
        }

        if (ui->glWidget) ui->glWidget->update();
    });

    // Avviso di rallentamento: il GLWidget segnala quando il rendering resta
    // sotto soglia troppo a lungo. PRIMA fermiamo l'animazione, POI avvisiamo:
    // col collasso in corso (frame da secondi) il popup modale restava sepolto
    // dietro il rendering e l'utente non riusciva nemmeno a premere "Stop"
    // (visto su iPhone: magenta + app di fatto inutilizzabile). Fermare subito
    // libera GPU e GUI thread, la finestra appare ed e' reattiva; chi vuole fa
    // ripartire tutto dal popup (equivale a un master Start).
    // QueuedConnection: il segnale parte dal thread di rendering del QRhiWidget,
    // il QMessageBox deve invece girare nel thread GUI.
    // Superficie chiusa o aperta: la sonda dei lati coperti arriva dopo il
    // caricamento, e il segnale puo' partire da render(): in coda.
    connect(ui->glWidget, &GLWidget::borderAvailabilityChanged, this,
            [this] { updateBorderControlsGate(); }, Qt::QueuedConnection);

    // SHADER CHE NON COMPILA: la superficie SPARISCE (buildPipeline azzera le
    // pipeline) e finora l'unica traccia era un qWarning sulla console, che
    // l'utente non vede -- restava solo lo schermo vuoto, senza spiegazione.
    // Caso tipico: due texture per-mesh i cui script dichiarano lo stesso
    // simbolo. Il generatore rinomina per-mesh le forme note (#define, funzioni,
    // globali, struct), ma uno script NUOVO puo' sempre introdurne una non
    // prevista: qui l'utente almeno legge QUALE simbolo e' in conflitto.
    connect(ui->glWidget, &GLWidget::shaderCompilationFailed, this,
            [this](const QString &err) {
        // Un popup alla volta. La sorgente ne emette gia' UNO solo per errore
        // (m_shaderErrorReported in GLWidget), ma la guardia resta come rete:
        // il segnale e' Queued e il box e' asincrono, quindi due errori diversi
        // in rapida successione potrebbero comunque accavallarsi.
        if (m_shaderErrorPopupActive) return;
        m_shaderErrorPopupActive = true;

        // BOX ASINCRONO, non exec(): un QMessageBox modale gira un event loop
        // ANNIDATO, e i segnali consegnati li' dentro riaprivano il popup sopra
        // se stesso -- la raffica che ha reso necessaria l'uscita forzata.
        // open() ritorna subito e l'applicazione resta utilizzabile.
        auto *box = new QMessageBox(this);
        box->setIcon(QMessageBox::Warning);
        box->setWindowTitle(tr("Shader Compilation Failed"));

        // TESTO CORTO in setText, RESTO in setInformativeText: QMessageBox tratta
        // il testo principale come un titolo e NON lo manda a capo, allargando la
        // finestra quanto serve a contenerlo su una riga. Con la spiegazione
        // intera li' dentro il box arrivava a occupare tutto lo schermo (visto su
        // iPhone). L'informativeText invece va a capo da solo: e' lo stesso
        // schema del box "Transparency on a heavy scene", che infatti si e'
        // sempre visto di dimensioni normali.
        const QString detail = err.trimmed();

        // Una RISORSA di shader mancante non e' un errore dell'utente: il testo
        // sulle texture in conflitto sarebbe una spiegazione falsa, e manderebbe
        // a cercare un problema inesistente nei propri script. bakeShader in quel
        // caso confeziona gia' il messaggio giusto (riconoscibile dal prefisso),
        // che qui si mostra al posto di quello standard.
        if (detail.startsWith(QLatin1String("Internal error:"))) {
            box->setText(tr("The application is missing part of its built-in data."));
            box->setInformativeText(detail);
        } else {
            box->setText(tr("The surface was not updated: what you see is the "
                            "last valid image."));
            box->setInformativeText(
                tr("The shader could not be compiled.\n\n"
                   "If you have just applied a texture to a mesh, its script may "
                   "declare a symbol (a function, a #define, a global variable) "
                   "with the same name as another mesh's texture.\n\n"
                   "To recover, turn that texture off on the mesh, or load a "
                   "different one."));
            // Il log del compilatore e' lungo e a righe fisse: sta nei dettagli,
            // dove ha una sua area con scorrimento invece di allargare il box.
            if (!detail.isEmpty()) box->setDetailedText(detail);
        }

        box->setStandardButtons(QMessageBox::Ok);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QDialog::finished, this,
                [this](int){ m_shaderErrorPopupActive = false; });
        box->open();
    }, Qt::QueuedConnection);

    // IMMAGINE DI TEXTURE NON DECODIFICABILE.
    // Il file c'e' ed e' leggibile (validato prima del caricamento) ma Qt non ne
    // ricava pixel: formato non supportato o file corrotto. Prima
    // loadTextureFromFile usciva in silenzio e a schermo restava la texture
    // PRECEDENTE, che sembrava "la texture di default del preset": nessun
    // indizio della causa. Box asincrono per gli stessi motivi del segnale
    // gemello qui sopra (niente event loop annidato).
    connect(ui->glWidget, &GLWidget::textureImageLoadFailed, this,
            [this](const QString &path) {
        if (m_textureImageErrorPopupActive) return;
        m_textureImageErrorPopupActive = true;

        auto *box = new QMessageBox(QMessageBox::Warning, "Image Not Loaded",
            "This image could not be loaded, so the previous texture is still "
            "showing:\n\n" + path + "\n\nThe file exists but is not a readable "
            "image: it may be corrupted, or in a format that is not supported.",
            QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QDialog::finished, this,
                [this](int){ m_textureImageErrorPopupActive = false; });
        box->open();
    }, Qt::QueuedConnection);

    connect(ui->glWidget, &GLWidget::performanceWarning, this, [this]() {
        // Un popup alla volta: eventuali segnali gia' in coda quando il box e'
        // aperto (peggioramenti misurati prima del nostro stop) non devono
        // aprirne altri. E se il master e' gia' fermo (stop manuale arrivato
        // prima della consegna del segnale in coda) l'avviso e' stantio.
        //
        // m_transparencyGuardActive: una guardia trasparenza (interattiva o
        // post-load) sta gestendo il caso o ha il suo popup aperto. Il segnale
        // del watchdog qui e' STANTIO — era stato emesso (QueuedConnection) sui
        // primi frame trasparenti PRIMA che la guardia mettesse alpha a 1 e
        // chiamasse acknowledgePerformanceWarning(); l'ack blocca le emissioni
        // FUTURE ma non questa gia' in coda. Scartarlo evita il popup del
        // watchdog SOPRA la finestra della guardia (la confusione segnalata).
        if (m_perfPopupActive || m_masterStopped || m_transparencyGuardActive) return;
        m_perfPopupActive = true;

        // Lo stop e la trasparenza tolta qui sotto li decide l'app, non
        // l'utente: non sono lavoro da salvare.
        AbsorbChangesGuard absorbWatchdog(this);

        performMasterStop();

        // Allo stop da collasso riportiamo anche la trasparenza a 1 (solo Ray
        // Marching): il ramo trasparente e' il moltiplicatore di costo del
        // marcher (MAX_FACES x 3 marchNextLayer per pixel), e con alpha<1 OGNI
        // ridisegno (slider, drag, resize) restava da secondi anche a scena
        // ferma — lo stop toglie il moto, non il costo per pixel. Opaco costa
        // una frazione: l'interfaccia torna reattiva subito, popup compreso.
        // setValue(100) passa dall'handler valueChanged (value==100: nessun
        // check ill/warn) e riallinea slider, label e GLWidget in un colpo.
        // Sul parametrico la trasparenza non e' il collo di bottiglia: non si
        // tocca. Se l'utente sceglie "Restart animation" l'alpha ORIGINALE
        // viene ripristinato prima del riavvio (prosegue a suo rischio con la
        // scena identica a prima); con "Keep it stopped" resta opaco.
        bool alphaReset = false;
        int  alphaPrev  = 100;
        const bool perfIsImplicit = (implicitMode());
        if (perfIsImplicit && ui->alphaSlider->value() < 100) {
            alphaPrev = ui->alphaSlider->value();
            ui->alphaSlider->setValue(100);
            alphaReset = true;
        }

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Animation stopped"));
        box.setText(tr("The animation was stopped because rendering was "
                       "slowing down dangerously."));
        QString info = tr("Pushing the complexity further (steps, effects, "
                          "resolution) may degrade the image or, on some "
                          "devices, freeze the application.\n\n"
                          "You can restart the animation at your own risk, "
                          "or leave it stopped and reduce some parameters "
                          "first.");
        if (alphaReset)
            info += tr("\n\nTransparency has been set to fully opaque to keep "
                       "the app responsive while stopped. Restarting the "
                       "animation restores your transparency setting.");
        box.setInformativeText(info);
        QPushButton *resumeBtn = box.addButton(tr("Restart animation"), QMessageBox::AcceptRole);
        QPushButton *stayBtn   = box.addButton(tr("Keep it stopped"), QMessageBox::RejectRole);
        box.setDefaultButton(stayBtn);
        box.exec();
        if (box.clickedButton() == resumeBtn) {
            // Ripristina la trasparenza originale PRIMA del riavvio, cosi' la
            // scena riparte identica a com'era. Set programmatico: il flag salta
            // i check ill/warn dell'handler (gia' assolti quando l'utente aveva
            // abbassato lo slider la prima volta).
            if (alphaReset) {
                m_settingAlphaProgrammatic = true;
                ui->alphaSlider->setValue(alphaPrev);
                m_settingAlphaProgrammatic = false;
            }
            // Riavvio = vero master Start (il gestore riconosce sender()==m_btnStart:
            // riarma i flag user-stop e riparte il moto camera corrente).
            if (m_btnStart) m_btnStart->click();
            // L'utente e' avvisato e ha scelto di proseguire: zittiamo il watchdog
            // per QUESTA animazione (niente popup a raffica sullo stesso
            // rallentamento). DOPO il riavvio, non prima: da fermo il ramo
            // !animating del watchdog azzererebbe subito il flag.
            if (ui->glWidget) ui->glWidget->acknowledgePerformanceWarning();
        }
        m_perfPopupActive = false;
    }, Qt::QueuedConnection);

    // Col bersaglio Border le densita' sono del bordo: Density u = anelli lungo
    // il lato, Density v = linee attorno al tubo.
    connect(ui->btnWireUPlus,  &QPushButton::clicked, this, [this](){
        if (editingBorder()) ui->glWidget->adjustBorderWireStep(true, -1);
        else                 ui->glWidget->increaseWireframeUDensity();
    });
    connect(ui->btnWireVPlus,  &QPushButton::clicked, this, [this](){
        if (editingBorder()) ui->glWidget->adjustBorderWireStep(false, -1);
        else                 ui->glWidget->increaseWireframeVDensity();
    });
    connect(ui->btnWireUMinus, &QPushButton::clicked, this, [this](){
        if (editingBorder()) ui->glWidget->adjustBorderWireStep(true, +1);
        else                 ui->glWidget->decreaseWireframeUDensity();
    });
    connect(ui->btnWireVMinus, &QPushButton::clicked, this, [this](){
        if (editingBorder()) ui->glWidget->adjustBorderWireStep(false, +1);
        else                 ui->glWidget->decreaseWireframeVDensity();
    });

    // Colori Default
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;

    if (ui->glWidget) {
        // Colore superficie (Verde)
        ui->glWidget->setColor(defR, defG, defB);
        // Colori della texture di default: verde e nero.
        ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
    }

    ui->sliderR->setRange(0, 255);
    ui->sliderG->setRange(0, 255);
    ui->sliderB->setRange(0, 255);
    ui->lightSlider->setRange(0, 200); ui->lightSlider->setValue(100);
    ui->lblValLight->setText(QString::number(ui->lightSlider->value()) + " %");
    ui->speed3DSlider->setRange(1, 100); setPathSpeed3D(10);
    ui->speed4DSlider->setRange(1, 100); setPathSpeed4D(10);
    // Il trascinamento scrive lo stato (vedi pathSpeed3D).
    connect(ui->speed3DSlider, &QSlider::valueChanged, this, [this](int v) { m_scene.pathSpeed3D = v; });
    connect(ui->speed4DSlider, &QSlider::valueChanged, this, [this](int v) { m_scene.pathSpeed4D = v; });
    // FOV UNICO (dock renderer, sotto Light). Prima erano due slider separati nei
    // dock 3D e 4D, attivi solo con la RISPETTIVA path in corsa: andavano in
    // conflitto (due controlli sullo stesso m_cameraFov) e da fermo erano
    // entrambi bloccati, quindi dopo un path a FOV largo non si poteva
    // correggere l'inquadratura se non con Reset View. Questo e' l'unico
    // controllo, non si blocca mai e agisce su TUTTO: path 3D/4D, rotazioni,
    // t-motion e superfici statiche.
    ui->fovSliderMain->setRange(20, 110);
    ui->fovSliderMain->setValue(45);
    ui->lblValFov->setText(QString::number(45) + QString::fromUtf8("°"));

    UiStyleManager::setupBigSliders(ui->sliderR, ui->sliderG, ui->sliderB, ui->alphaSlider, ui->lightSlider, ui->speed3DSlider, ui->speed4DSlider, ui->fovSliderMain);

    // Color slot della texture (col1/col2). Surface NON è qui: è nella coppia
    // m_bgTargetGroup (Surface/Background). L'esclusività FRA i due gruppi è a mano.
    m_colorGroup = new QButtonGroup(this);
    m_colorGroup->addButton(ui->radioTexColor1);
    m_colorGroup->addButton(ui->radioTexColor2);
    m_colorGroup->setExclusive(true);

    m_scene.bgColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    ui->glWidget->setBackgroundColor(m_scene.bgColor);

    // (onColorTargetChanged è già invocato dall'handler toggled di radioBackground
    //  definito sopra: nessuna connessione separata per evitare doppia chiamata.)

    auto handleColorChange = [this]() {
        int r = ui->sliderR->value(); int g = ui->sliderG->value(); int b = ui->sliderB->value();
        ui->valR->setNum(r); ui->valG->setNum(g); ui->valB->setNum(b);
        QColor newColor(r, g, b);

        // Due gruppi INDIPENDENTI: la terna (Surface/Border/Background) dice
        // DOVE operiamo, la coppia Color1/Color2 QUALE tinta della texture
        // editiamo. La priorità è data dalla terna; Color1/2 scelgono solo lo
        // slot quando il target ha una texture colorata attiva.
        if (editingBorder()) {
            // Texture del bordo colorata: Color 1/2 scelgono la tinta; senza,
            // e' il colore del bordo.
            GLWidget *gl = ui->glWidget;
            if (gl && gl->borderTextureShown() && activeTextureUsesColors()) {
                if (ui->radioTexColor2->isChecked())
                    gl->setBorderTexColors(gl->borderTexColor1(), newColor);
                else
                    gl->setBorderTexColors(newColor, gl->borderTexColor2());
            } else if (gl) {
                gl->setBorderColor(newColor);
            }
        }
        else if (editingBackground()) {
            if (targetTextureOn() && activeTextureUsesColors()) {
                // Texture di sfondo colorata: Color1/Color2 scelgono quale tinta.
                if (ui->radioTexColor2->isChecked()) m_scene.bgTexColor2 = newColor;
                else m_scene.bgTexColor1 = newColor;

                ui->glWidget->setBackgroundTexColors(m_scene.bgTexColor1, m_scene.bgTexColor2);
                ui->glWidget->update();
            } else {
                m_scene.bgColor = newColor;
                ui->glWidget->setBackgroundColor(m_scene.bgColor);
                ui->glWidget->update();
            }
        }
        else { // target = Surface
            // In Wireframe la texture è nascosta e le linee usano il COLORE SUPERFICIE.
            bool wireframeMode = (shownRenderMode() == 2);
            // TEXTURE ATTIVA SUL DESTINATARIO CORRENTE. Con una fascia
            // selezionata conta la SUA texture: m_scene.surfaceTextureState e' lo
            // stato di quella GLOBALE e resta false se si e' texturizzata solo
            // la fascia, quindi questo ramo non veniva mai preso e gli slider
            // finivano a editare il colore della superficie invece dei due
            // u_col1/u_col2 della texture -- "i picker non cambiano colore".
            const bool texActiveHere =
                (ui->glWidget && ui->glWidget->activeMeshPart() >= 0)
                    ? ui->glWidget->activeMeshTextureActive()
                    : m_scene.surfaceTextureState;
            if (!wireframeMode && texActiveHere && activeTextureUsesColors()) {
                // Texture di superficie colorata: Color1/Color2 scelgono lo slot.
                // AMBITO "MESH": i colori vanno nella PARTE, non nei due slot
                // globali. Quelli appartengono alla texture di superficie, e
                // scriverli qui cambiava i colori di tutte le altre fasce che li
                // ereditano (stesso difetto del caso Mandelbrot, ma sul percorso
                // interattivo degli slider).
                // Lo slot non toccato resta quello che il bersaglio sta gia'
                // disegnando (surfaceTexColor: i colori propri della fascia, o
                // i globali che eredita).
                const bool slot2 = ui->radioTexColor2->isChecked();
                const QColor c1 = slot2 ? surfaceTexColor(1) : newColor;
                const QColor c2 = slot2 ? newColor : surfaceTexColor(2);
                if (!ui->glWidget->setActiveMeshTexColors(c1, c2))
                    ui->glWidget->setGlobalTextureColors(c1, c2);
                if (!surfaceTextureIsCustom() && !surfaceHasImage()) scheduleTextureGeneration();
            } else {
                ui->glWidget->setColor(r/255.0f, g/255.0f, b/255.0f);
            }
        }
    };

    ui->sliderR->disconnect(); ui->sliderG->disconnect(); ui->sliderB->disconnect();
    connect(ui->sliderR, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderG, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderB, &QSlider::valueChanged, this, handleColorChange);

    connect(ui->lightSlider, &QSlider::valueChanged, this, [this](int val){
        float intensity = val / 100.0f;
        ui->glWidget->setLightIntensity(intensity);
        ui->lblValLight->setText(QString::number(val) + " %");
    });

    // LUCE DI RIEMPIMENTO (Fill Light): luce dall'osservatore, SOLO Ray Marching.
    // Serve dove le due luci principali non arrivano -- le pareti viste da dentro
    // un tubo o una cavita' lungo un path, che restano nere per quanto si alzi
    // Light (che moltiplica, quindi su un valore quasi nullo non ha presa).
    // Scala 0..100 = 0.00..1.20. Il tetto era 0.50 e si e' rivelato basso: su
    // certe geometrie bisognava portare il cursore a fondo senza riuscire a
    // schiarire l'ombra, perche' le facce quasi PERPENDICOLARI alla vista sono
    // quelle che la headlight prende meno (vedi la formula "wrap" nel template,
    // che le recupera). Con 1.20 il cursore ha margine anche in quei casi e
    // resta comunque una regolazione fine nella prima meta' della corsa.
    // DEFAULT 0: a zero il termine nello shader e' esattamente zero e l'immagine
    // e' identica a prima. E' la ragione per cui questa luce e' un controllo e
    // non una costante -- vedi u_fillLight in glwidget.h.
    ui->fillLightSlider->setRange(0, 100);
    ui->fillLightSlider->setValue(0);
    ui->lblValFill->setText("0.00");
    // Stesso aspetto degli altri slider di valore (UiStyleManager::
    // valueSliderStyle, grigio da spento) e la stessa altezza di Light.
    if (ui->lightSlider) {
        ui->fillLightSlider->setStyleSheet(UiStyleManager::valueSliderStyle());
        ui->fillLightSlider->setMinimumHeight(ui->lightSlider->minimumHeight());
    }
    // SPESSORE DEL BORDO (raggio dei tubi sui lati, unita' di scena). Curva
    // QUADRATICA, come lo spessore del guscio: i bordi utili sono sottili e la
    // prima meta' della corsa deve regolarli finemente. 0 = nessun bordo, il
    // default: lo shader non contiene nemmeno il codice del bordo.
    ui->borderThicknessSlider->setRange(0, 100);
    ui->borderThicknessSlider->setValue(0);
    ui->lblValBorderThickness->setText(borderThicknessText(0.0f));
    if (ui->lightSlider) {
        ui->borderThicknessSlider->setStyleSheet(UiStyleManager::valueSliderStyle());
        ui->borderThicknessSlider->setMinimumHeight(ui->lightSlider->minimumHeight());
    }
    connect(ui->borderThicknessSlider, &QSlider::valueChanged, this, [this](int val){
        // La posizione e' una VISTA dello spessore: se corrisponde gia' a quello
        // applicato (un passo fisso, qui sotto) non lo si riporta sulla curva.
        const float cur = ui->glWidget ? ui->glWidget->borderRadius() : 0.0f;
        const float t = (sliderFromBorderThickness(cur) == val) ? cur : borderThicknessFromSlider(val);
        if (ui->glWidget) ui->glWidget->setBorderRadius(t);
        ui->lblValBorderThickness->setText(borderThicknessText(t));
    });
    // Clic sulla corsa e frecce: passo fisso sul valore (borderThicknessStepped),
    // valore esatto nel motore e nell'etichetta, slider sulla sua posizione --
    // come l'accensione dal radio Border. Il trascinamento resta sulla curva.
    connect(ui->borderThicknessSlider, &QAbstractSlider::actionTriggered, this, [this](int action){
        float step = 0.0f;
        switch (action) {
        case QAbstractSlider::SliderPageStepAdd:   step =  0.1f;  break;
        case QAbstractSlider::SliderPageStepSub:   step = -0.1f;  break;
        case QAbstractSlider::SliderSingleStepAdd: step =  0.01f; break;
        case QAbstractSlider::SliderSingleStepSub: step = -0.01f; break;
        default: return;
        }
        if (!ui->glWidget) return;
        const float t = borderThicknessStepped(ui->glWidget->borderRadius(), step);
        ui->glWidget->setBorderRadius(t);
        ui->lblValBorderThickness->setText(borderThicknessText(t));
        // Durante actionTriggered Qt applica poi la posizione come valore.
        ui->borderThicknessSlider->setSliderPosition(sliderFromBorderThickness(t));
    });

    connect(ui->fillLightSlider, &QSlider::valueChanged, this, [this](int val){
        const float v = val * 0.012f;          // 0..100 -> 0.00..1.20
        ui->glWidget->setFillLight(v);
        ui->lblValFill->setText(QString::number(v, 'f', 2));
    });

    // FOV UNICO: agisce SEMPRE e subito, qualunque cosa stia guidando la camera
    // (path 3D/4D, rotazioni, t-motion, o superficie ferma). Nessun gate: era il
    // blocco "solo con la propria path in corsa" a rendere il valore non
    // correggibile da fermo.
    connect(ui->fovSliderMain, &QSlider::valueChanged, this, [this](int val){
        applyCameraFov((float)val);
    });

    // ASPETTO PER-MESH: lo spinbox sceglie su quale parte agiscono i controlli
    // gia' esistenti (colore, trasparenza, Light, Solid/Wireframe). Il valore 0
    // mostra "All" e li riporta sullo stato globale, cioe' il comportamento di
    // sempre; 1..N selezionano la parte k-1. Cambiando selezione riallineiamo i
    // controlli ai valori di quella parte, cosi' gli slider mostrano cio' che
    // stanno per modificare invece di un valore ereditato da un'altra mesh.
    // Le parti di mesh nascono in GLWidget::updateSurfaceData, che ha molti
    // chiamanti (script, equazioni, cambio tab, load di preset): agganciarsi al
    // segnale invece che ai singoli chiamanti copre tutti i percorsi. In
    // particolare il load di un preset SCRIPT non passa da
    // checkAndTriggerMeshUpdate, dove l'aggancio precedente non scattava.
    connect(ui->glWidget, &GLWidget::meshPartsChanged, this, [this](){
        applyPendingMeshAppearance();
        updateMeshSelectorRange();
    });

    // ALL / MESH: due radio espliciti al posto della vecchia voce "All" nascosta
    // dentro lo spinbox (che era il valore 0 con specialValueText). Li' non si
    // capiva che 0 fosse uno stato diverso, e digitarlo veniva rifiutato perche'
    // updateMeshSelectorRange riportava subito la selezione a 1.
    // CAMPO MESHES: il testo e' lo stato (anche a meta' digitazione, come i
    // limiti); si applica alla conferma, lo slider subito.
    if (ui->lineMeshCount) {
        connect(ui->lineMeshCount, &QLineEdit::textEdited, this,
                [this](const QString &text) { m_scene.meshCount = text; });
        connect(ui->lineMeshCount, &QLineEdit::editingFinished, this, &MainWindow::commitMeshCountOnEnter);
    }
    if (ui->meshCountSlider) {
        connect(ui->meshCountSlider, &QSlider::valueChanged, this, [this](int value) {
            setMeshCountText(QString::number(value));
            applyMeshCount();
        });
    }

    // Passando ad All l'aspetto per-mesh NON si perde: resta nelle parti, e
    // tornando su Mesh si ritrova (i valori vivono in MeshPart, non nei radio).
    auto applyMeshScope = [this](){
        if (!ui->glWidget) return;
        const bool single = !meshScopeAll();
        ui->spinMeshSel->setEnabled(single);
        // In "All" la superficie si comporta come UNA SOLA: l'aspetto proprio
        // delle parti viene SOSPESO (ignorato dal render, non cancellato), cosi'
        // colore, trasparenza, luce e wireframe globali valgono per tutte.
        // Premendo "Mesh" le differenze tornano da sole: i valori sono rimasti
        // nelle MeshPart.
        ui->glWidget->setMeshAppearanceUniform(!single);
        ui->glWidget->setActiveMeshPart(single ? ui->spinMeshSel->value() - 1 : -1);
        syncAppearanceControlsToActiveMesh();

        // OROLOGIO TEXTURE. Cambiare ambito cambia QUALI texture sono in gioco:
        // in "All" quelle per-mesh sono sospese, quindi il clock va rivalutato o
        // resterebbe acceso per una texture animata che nessuno sta piu'
        // disegnando (e viceversa, spento tornando su "Mesh").
        // Si guarda SOLO la texture di SUPERFICIE: e' quella che questo clock
        // governa. Con allSurfaceTextureCode() (che aggrega anche le fasce),
        // passare ad "All" con una texture per-mesh animata RIACCENDEVA il clock
        // di superficie che l'utente aveva appena fermato -- e il tasto tornava
        // su "Stop" da solo.
        // ...e MAI dopo un master Stop. Il gate storico e' m_userStoppedTexClock,
        // che pero' registra il solo Stop del DOCK: performMasterStop alza
        // m_masterStopped (e m_userStoppedMeshTexClock per le fasce), non
        // quello. Cosi' bastava cambiare ambito per far ripartire la texture di
        // superficie a scena ferma -- e il master, che la contava come attivita'
        // in moto, tornava a dire STOP senza che si muovesse nient'altro.
        // Stessa regola di applyAnimationState: il master e' il gate di ogni
        // riaccensione, qualunque sia il modulo.
        if (!m_userStoppedTexClock && !m_masterStopped)
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(m_scene.surfaceTextureCode));

        // In "All" il checkbox torna a mostrare lo stato GLOBALE della texture:
        // in "Mesh" ci pensa syncAppearanceControlsToActiveMesh (display della
        // parte), ma quella esce presto quando non c'e' parte attiva.
        if (!single && !editingBackground()
            && !implicitMode()) {
            refreshTextureCheckbox();
        }
        // Come per lo spinbox: il sync muove i radio a segnali bloccati, quindi
        // il gating (tasti densita' U/V) va aggiornato a mano.
        updateRenderState();

        // TASTI RICALCOLATI PER ULTIMI. syncAppearanceControlsToActiveMesh (piu'
        // sopra) li aggiorna gia', ma gira PRIMA che il clock texture venga
        // rivalutato qui: leggeva quindi lo stato vecchio e il tasto restava
        // quello dell'ambito precedente. Ordine: stato -> display, mai il
        // contrario.
        updateScriptButtonText();
        updateMasterButtonState();

        // GATING DEI LIMITI PER-MESH. I quattro campi u/v sono attivi solo in
        // ambito "Mesh" (in "All" non c'e' una parte a cui riferirli), e quel
        // gating vive in updateMeshScopeEnabled. Senza questa chiamata il
        // cambio di ambito non lo aggiornava: gli altri controlli per-mesh sono
        // sempre abilitati e cambiano solo DESTINATARIO, quindi finora nessuno
        // aveva bisogno di rivalutare il gating al click sui radio -- il giro
        // passava solo da meshPartsChanged, che al solo cambio di ambito non
        // scatta. I campi restavano percio' spenti e vuoti fino alla prima
        // rigenerazione della griglia.
        // Va per ULTIMA: legge i radio, che sono gia' nello stato finale.
        updateMeshScopeEnabled();
        // ...e il gating appena calcolato ha potuto RIABILITARE i campi, che
        // pero' sono ancora vuoti: updateMeshScopeEnabled li riempie solo
        // quando li spegne. Il display tocca a questa.
        syncMeshLimitFields();
    };
    // ESCLUSIVITA': i due radio NON sono fratelli (radioMeshOne sta dentro
    // groupMeshOne, il riquadro che lo tiene insieme allo spinbox; radioMeshAll
    // sta in widgetMeshSel). Qt rende esclusivi solo i radio con lo stesso
    // genitore, quindi senza questo gruppo esplicito ognuno faceva storia a se':
    // si potevano avere entrambi accesi, oppure entrambi spenti (un radio solo
    // nel suo gruppo e' anche deselezionabile).
    // Il gruppo li riunisce a prescindere dal layout: il riquadro attorno a
    // "Mesh" resta libero di essere spostato o ridisegnato.
    m_meshScopeGroup = new QButtonGroup(this);
    m_meshScopeGroup->setExclusive(true);
    m_meshScopeGroup->addButton(ui->radioMeshAll);
    m_meshScopeGroup->addButton(ui->radioMeshOne);
    // Il clic: prima lo STATO, che applyMeshScope legge. Arriva solo dai clic:
    // il programma scrive i radio a segnali bloccati (setMeshScopeAll).
    connect(ui->radioMeshAll, &QRadioButton::toggled, this, [this, applyMeshScope](bool on){
        if (!on) return;
        m_scene.meshScopeAll = true;
        applyMeshScope();
    });
    connect(ui->radioMeshOne, &QRadioButton::toggled, this, [this, applyMeshScope](bool on){
        if (!on) return;
        m_scene.meshScopeAll = false;
        applyMeshScope();
    });
    // Stato iniziale: radioMeshAll e' gia' checked nella .ui, quindi il suo
    // toggled NON scatta qui (le connect sono appena state fatte). Senza questa
    // chiamata l'ambito "All" sarebbe mostrato dai radio ma non applicato al
    // motore, e le mesh partirebbero gia' differenziate.
    //
    // PRIMA pero' va allineato il renderMode del motore alla modalita'
    // PARAMETRICA di partenza (Base, come radioBasic gia' selezionato sopra).
    // Nel setup del Ray Marching il motore riceve setGlobalRenderMode(1) = Shell, che
    // resta li' finche' nessuno lo cambia: applyMeshScope -> ...ToActiveMesh ->
    // ramo "All" -> i radio sulla globalRenderMode() del motore lo leggevano come
    // modalita' parametrica e accendeva PHONG, mentre il toro di default era
    // ovviamente disegnato in Base. Radio e superficie non concordavano.
    if (ui->glWidget) ui->glWidget->setGlobalRenderMode(m_scene.renderMode);
    applyMeshScope();

    // MOBILE: il campo Mesh e' un intero 1..N e non ha bisogno della tastiera.
    // Le frecce native vengono sostituite da due tasti a forma di freccia e il
    // campo diventa non editabile (vedi installMobileSpinButtons: su iOS
    // toccarlo apriva tastierino e menu di modifica senza poterli chiudere).
    // No-op su desktop.
    UiStyleManager::installMobileSpinButtons(ui->spinMeshSel);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // I radio non devono cedere spazio: senza questo il testo "Mesh" veniva
    // troncato in "Mes". Un minimo "a occhio" non basta — dipende dal font
    // effettivo, che su mobile e' piu' grande — quindi lo calcoliamo dal testo
    // reale: larghezza del testo + indicatore + margini dello stile, e lo
    // imponiamo come minimo E come dimensione preferita, cosi' il layout non
    // puo' comprimerli sotto quella soglia per far posto ai tasti.
    // NB: si usa sizeHint(), non un calcolo a mano su fontMetrics +
    // pixelMetric. Il QSS mobile ridefinisce sia la dimensione
    // dell'indicatore (QRadioButton::indicator width/height) sia lo spacing,
    // quindi i pixelMetric dello stile restituirebbero i valori di DEFAULT e
    // non quelli realmente usati: il minimo risulterebbe troppo stretto e il
    // testo continuerebbe a essere troncato. sizeHint tiene conto del foglio
    // di stile applicato.
    auto fitRadio = [](QRadioButton* rb) {
        if (!rb) return;
        rb->ensurePolished();                       // QSS applicato prima di misurare
        const int w = rb->sizeHint().width() + 8;    // 8 = respiro
        rb->setMinimumWidth(w);
        rb->setSizePolicy(QSizePolicy::Fixed, rb->sizePolicy().verticalPolicy());
    };
    fitRadio(ui->radioMeshAll);
    fitRadio(ui->radioMeshOne);

    // MARGINE SINISTRO (solo mobile). Va toccato SOLO quello: la distribuzione
    // fra i due gruppi la fa lo stretch 1,2 della .ui (non si tocca, o "Mesh"
    // torna sotto il campo come su desktop), e il rightMargin deve restare 0.
    // Quello zero non e' un caso: e' cio' che tiene il gruppo "Mesh + frecce +
    // campo" compattato a DESTRA, e quindi nettamente separato dal radio "All".
    // Un margine simmetrico lo staccherebbe dal bordo destro e i due gruppi
    // tornerebbero a somigliarsi.
    // Il leftMargin invece e' 24px tarati sui widget piccoli del desktop: su
    // mobile indicatori, font e tasti sono piu' grandi, la riga si riempie quasi
    // tutta e quei 24px venivano mangiati dal layout, lasciando "All"
    // appiccicato al bordo sinistro. Bastano pochi px in piu' per staccarlo,
    // senza spostare nulla a destra.
    if (auto* meshRow = qobject_cast<QHBoxLayout*>(ui->widgetMeshSel->layout())) {
        const QMargins m = meshRow->contentsMargins();
        meshRow->setContentsMargins(12, m.top(), 0, m.bottom());
        // COMPATTAZIONE A DESTRA. Con lo stretch 1,2 le due celle si allargano,
        // ma i widget dentro restano allineati a SINISTRA della propria cella:
        // il gruppo "Mesh" galleggiava a meta' di una cella larga il doppio,
        // invece di stare tutto a destra come prima. Il rightMargin a 0 da solo
        // non basta a rimediare, perche' e' la cella a essere piu' larga del
        // gruppo, non il margine a spingerlo dentro.
        // Allineandolo a destra torna compatto contro il bordo e nettamente
        // staccato da "All", che resta a sinistra nella sua cella.
        meshRow->setAlignment(ui->groupMeshOne, Qt::AlignRight | Qt::AlignVCenter);
    }
#endif

    connect(ui->spinMeshSel, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val){
        if (!ui->glWidget) return;
        if (meshScopeAll()) return;   // in All lo spinbox e' inerte
        ui->glWidget->setActiveMeshPart(val - 1);     // lo spinbox parte da 1
        syncAppearanceControlsToActiveMesh();
        // syncAppearanceControlsToActiveMesh muove i radio a SEGNALI BLOCCATI
        // (deve: il loro handler scriverebbe la modalita' sulla parte), quindi
        // updateRenderState non gira da solo e il gating dei controlli resta
        // fermo allo stato della mesh PRECEDENTE. Senza questa chiamata,
        // selezionando una mesh in wireframe i tasti densita' U/V restavano
        // grigi. Va DOPO il sync, cosi' rilegge i radio gia' aggiornati.
        updateRenderState();
    });

    // Click su Color1/Color2. Gruppi indipendenti: scegliere quale tinta editare NON
    // tocca la coppia Surface/Background (che resta dov'è: continui a operare
    // sulla superficie o sullo sfondo). Basta riallineare gli slider alla tinta scelta.
    connect(m_colorGroup, &QButtonGroup::buttonClicked, this, [this](){
        onColorTargetChanged();
    });
}


// UNICO punto da cui si mostra un errore di compilazione all'utente (~18
// chiamanti). L'esito del Run NON si marca qui: lo deduce RunOutcomeGuard dal
// contatore di InputValidator, che intercetta anche gli errori dei validatori
// -- i quali fermano l'equazione prima di compilare e non passerebbero mai da
// questa funzione.
void MainWindow::showShaderError(const QString &title, const QString &errorLog)
{
    InputValidator::showShaderCompilationError(this, title, errorLog);
}

// Allinea etichette e range degli slider che cambiano significato col modo:
// - S: "s=" (parametrico, range [-1000,1000]) vs "Step Relax" (Ray Marching, min 0)
// - Steps: "Steps=" (parametrico) vs "Ray Steps=" (Ray Marching)
// Il gestore di tabModeSelector::currentChanged fa già questo, ma durante il load di
// un preset il tab viene cambiato a SEGNALI BLOCCATI (per non innescare il reset
// distruttivo), quindi va richiamato esplicitamente: senza, caricando una superficie
// Ray Marching all'avvio (tab Parametric mai cambiato a mano) restano "s="/"Steps=".
// Tocca SOLO etichette e min dello slider, mai i VALORI (li imposta il preset).
void MainWindow::applyModeDependentStepUI(bool isImplicit)
{
    bool sB = ui->sSlider->blockSignals(true);
    if (isImplicit) {
        ui->lblS->setText("Step Relax");
        ui->lblSteps->setText("Ray Steps=");
        if (ui->sSlider->minimum() < 0) ui->sSlider->setMinimum(0);
    } else {
        ui->lblS->setText("s=");
        ui->lblSteps->setText("Steps=");
        if (ui->sSlider->minimum() == 0) ui->sSlider->setMinimum(-1000);
    }
    ui->sSlider->blockSignals(sB);
}

// Sincronizza lo stato dello slider di trasparenza con il condizionamento del campo
// implicito corrente. I campi a PRODOTTO (es. preset "Chain") fanno sparire la
// superficie con alpha<1 (crossing fantasma nel ramo trasparente): il motore forza
// gia' opaco (m_uboData.alpha=1).
//
// LOGICA (scelta utente): al caricamento lo slider resta ABILITATO e NON compare il
// popup. Solo quando l'utente TOCCA lo slider su un campo a prodotto scatta il popup e
// lo slider si blocca (handler valueChanged -> onAlphaSliderMovedIllCheck).
//
// newSurface=true  -> chiamata dopo un COMMIT di equazione implicita (nuova superficie):
//   riparte "fresco", slider abilitato e popup riarmato, cosi' l'utente puo' toccare di
//   nuovo lo slider su questa superficie.
// newSurface=false -> chiamata da updateRenderState (eventi UI vari, NON cambi superficie):
//   aggiorna SOLO il tooltip, senza toccare enabled/guardia (altrimenti un toggle
//   qualsiasi riabiliterebbe uno slider appena bloccato dall'utente sulla stessa superficie).
void MainWindow::syncImplicitAlphaSlider(bool isImplicitMode, bool newSurface)
{
    bool illImplicit = isImplicitMode && ui->glWidget && ui->glWidget->isImplicitIllConditioned();
    // SOLO ANDROID (implicitTransparencyMayDegrade e' sempre false altrove): superficie
    // la cui trasparenza potrebbe degradare (Gyroid, script RM). NON blocca: lo slider
    // resta usabile, mostriamo solo un avviso.
    bool warnImplicit = isImplicitMode && ui->glWidget && ui->glWidget->implicitTransparencyMayDegrade();

    if (newSurface) {
        if (!ui->alphaSlider->isEnabled()) ui->alphaSlider->setEnabled(true);
        m_implicitAlphaDisabled = false;
        m_implicitWarnShown = false;    // riarma il popup di avviso per la nuova superficie
        m_alphaHeavyWarnShown = false;  // riarma anche la conferma "scena pesante"
        m_alphaHeavyDeclined = false;   // il "no" valeva per la superficie precedente
        // Anche la guardia displacement-su-trasparenza riparte pulita: il suo
        // "gia' chiesto" era legato alla superficie precedente, e lo stesso
        // displacement su una superficie nuova e' un caso nuovo da valutare.
        // NON mentre la guardia e' in corso: nel ramo libreria questa funzione e'
        // chiamata (newSurface=true) POCHE RIGHE DOPO la guardia, sullo stesso
        // commit. Azzerare li' avrebbe buttato via il "gia' chiesto" appena
        // scritto, e il commit seguente avrebbe riaperto il box: e' la raffica
        // senza fine da cui veniamo.
        if (!m_heavyTexGuardActive) m_heavyTexGuardAskedFor.clear();
    }

    if (illImplicit) {
        ui->alphaSlider->setToolTip(
            tr("Transparency is unavailable for this surface: it is defined by a "
               "product of several factors, so true transparency cannot be computed "
               "reliably. Moving the slider will keep the surface opaque."));
    } else if (warnImplicit) {
        ui->alphaSlider->setToolTip(
            tr("Transparency may not render correctly on this surface on this device: "
               "it has many layers along each ray. The slider still works, but the "
               "surface may look clipped."));
    } else {
        ui->alphaSlider->setToolTip(QString());
    }
}

// L'utente ha abbassato lo slider trasparenza su un campo implicito a PRODOTTO:
// ripristina l'opacita' piena, mostra il popup UNA VOLTA e blocca lo slider. Chiamata
// dall'handler valueChanged solo per interazioni utente (non set programmatici) e con
// il campo gia' verificato come a prodotto.
void MainWindow::onAlphaSliderMovedIllCheck(int /*value*/)
{
    // Ripristina opacita' piena. La rientranza in valueChanged porta value==100, che
    // non rientra nel ramo dell'intercetto (gated da value < 100).
    ui->alphaSlider->setValue(100);

    if (!m_implicitAlphaDisabled) {
        m_implicitAlphaDisabled = true;   // guardia PRIMA del popup modale (anti-doppio)
        ui->alphaSlider->setEnabled(false);
        // Testo breve in setText, dettaglio in informativeText (che va a capo):
        // il testo principale non viene mandato a capo e allargherebbe il box
        // oltre la larghezza dello schermo.
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("Transparency unavailable"));
        box.setText(tr("This surface will stay opaque."));
        box.setInformativeText(
            tr("It is defined by a product of several factors "
               "(e.g. linked tori). True transparency cannot be computed "
               "reliably on such fields — with transparency the surface would "
               "disappear instead of turning translucent.\n\n"
               "The transparency slider is disabled."));
        box.exec();
    }
}

// SOLO ANDROID. L'utente ha abbassato lo slider su una superficie implicita la cui
// trasparenza puo' degradare (Gyroid, script RM): NON blocchiamo e NON ripristiniamo
// l'opacita' (lo slider agisce davvero, l'utente vede l'effetto reale). Mostriamo solo
// un popup di avviso UNA VOLTA per superficie. Chiamata dall'handler valueChanged solo
// per interazioni utente (non set programmatici), con warn gia' verificato.
void MainWindow::onAlphaSliderMovedWarnCheck()
{
    if (m_implicitWarnShown) return;
    m_implicitWarnShown = true;   // guardia PRIMA del popup modale (anti-doppio)
    // Vedi sopra: il testo principale non va a capo, il corpo sta
    // nell'informativeText o il box invade tutto lo schermo.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Transparency may not render correctly"));
    box.setText(tr("Transparency may not render correctly here."));
    box.setInformativeText(
        tr("On this device, this surface is made of many layers stacked along each "
           "viewing ray, more than the renderer can blend here, so with "
           "transparency it may look clipped.\n\n"
           "The slider still works — this is just a heads-up."));
    box.exec();
}

// SOLO MOBILE (no-op su desktop, dove trasparenza+displacement regge). Chiamata
// DOPO un validateAndApplyImplicitShader riuscito, col displacement che era
// applicato PRIMA: se l'apply lo ha INTRODOTTO o CAMBIATO mentre la trasparenza
// e' attiva, l'alpha va a 1 in modo deterministico. Il displacement gira dentro
// map() e il ramo trasparente lo moltiplica per MAX_FACES x 3 x passi: il
// collasso arriva CON la texture, quindi la conferma misurata (EMA della scena
// PRECEDENTE, ancora leggera) non puo' prevederlo. L'animazione/record CONTINUA,
// opaca; riabbassando lo slider decide la conferma misurata coi dati veri.
// Corpo condiviso: porta la scena a opaca e zittisce il watchdog, poi avvisa.
// Il watchdog va zittito perche' opaco+texture-con-displacement su iPhone e'
// comunque al limite -> senza, certi frame sforavano e faceva scattare un SECONDO
// popup (sopra questo) o l'auto-stop, in modo intermittente. Il flag si riarma
// quando l'utente riabbassa lo slider (sliderPressed -> rearmPerformanceWarning):
// esattamente il "a meno che non riporti alpha<1".
void MainWindow::forceOpaqueForHeavyRM(const QString &message)
{
    // La trasparenza la toglie l'app: non e' lavoro dell'utente da salvare.
    AbsorbChangesGuard absorbOpaque(this);
    m_settingAlphaProgrammatic = true;
    ui->alphaSlider->setValue(100);
    m_settingAlphaProgrammatic = false;

    if (ui->glWidget) ui->glWidget->acknowledgePerformanceWarning();

    // Il QMessageBox modale fa girare l'event loop: un performanceWarning gia' in
    // coda (emesso sui primi frame trasparenti prima dell'ack) verrebbe
    // consegnato proprio ORA, aprendo il popup del watchdog SOPRA questo. Il flag
    // fa scartare quel segnale stantio nel gestore di performanceWarning per
    // tutta la durata del nostro box. Ripristinato dopo (nested guard-safe: il
    // valore precedente non e' mai true perche' questa funzione non e' rientrante).
    m_transparencyGuardActive = true;
    QMessageBox::information(this, tr("Transparency turned off"), message);
    m_transparencyGuardActive = false;
}

// SOLO MOBILE (no-op su desktop, dove trasparenza+displacement regge). Chiamata
// DOPO un validateAndApplyImplicitShader riuscito, col displacement che era
// applicato PRIMA: se l'apply lo ha INTRODOTTO o CAMBIATO mentre la trasparenza
// e' attiva, l'alpha va a 1 in modo deterministico. Il displacement gira dentro
// map() e il ramo trasparente lo moltiplica per MAX_FACES x 3 x passi: il
// collasso arriva CON la texture, quindi la conferma misurata (EMA della scena
// PRECEDENTE, ancora leggera) non puo' prevederlo. L'animazione/record CONTINUA,
// opaca; riabbassando lo slider decide la conferma misurata coi dati veri.
void MainWindow::guardTransparencyOnDisplacementApply(const QString &prevDisp)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!ui->glWidget) return;
    const QString newDisp = ui->glWidget->currentDisplacementCode().trimmed();
    if (newDisp.isEmpty() || newDisp == prevDisp.trimmed()) return; // nessun displacement nuovo
    if (ui->alphaSlider->value() >= 100) return;                    // trasparenza non attiva

    forceOpaqueForHeavyRM(
        tr("This texture carves relief into the surface (displacement). Combined "
           "with transparency it would overload the GPU on this device, so the "
           "surface has been set to fully opaque — the animation keeps "
           "running.\n\n"
           "Lower the transparency slider again to retry it: the app will check "
           "the measured load first."));
#else
    Q_UNUSED(prevDisp);
#endif
}

// SOLO MOBILE (no-op su desktop). Guardia POST-LOAD: chiamata a fine
// applyCommonData. Chiude la falla del "record/preset con alpha<1 nel JSON +
// displacement": al load alpha e displacement sono impostati programmaticamente,
// quindi ne' la conferma misurata ne' la guardia interattiva scattano, e la
// scena partirebbe trasparente+pesante lasciando come unica rete il watchdog
// tardivo. Se lo STATO FINALE e' RM + alpha<1 + displacement presente -> opaco +
// avviso, coerente con la guardia interattiva. NB: legge lo stato GIA' finale
// (slider + widget), quindi va invocata DOPO che il load ha applicato tutto.
void MainWindow::guardTransparencyOnImplicitLoad()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!ui->glWidget) return;
    const bool isImplicit = (implicitMode());
    if (!isImplicit) return;
    if (ui->alphaSlider->value() >= 100) return;                          // trasparenza non attiva
    if (ui->glWidget->currentDisplacementCode().trimmed().isEmpty()) return; // niente displacement

    forceOpaqueForHeavyRM(
        tr("This surface was saved with transparency and a relief texture "
           "(displacement). Together they would overload the GPU on this device, "
           "so it has been loaded fully opaque — the animation keeps "
           "running.\n\n"
           "Lower the transparency slider to try transparency: the app will "
           "check the measured load first."));
#endif
}

// TUTTE LE PIATTAFORME, desktop COMPRESO (a differenza delle due guardie qui
// sopra). Chiamata DOPO un validateAndApplyImplicitShader riuscito, quando la
// texture RM appena committata e' finita su una scena gia' trasparente. Dopo e
// non prima: cosi' agisce sullo stato davvero applicato, e un apply fallito (che
// lascia la scena precedente) non fa comparire nessun popup.
//
// PERCHE' SERVE ANCHE SU DESKTOP. Il caso che la motiva e' il Cross Section di
// default: loadCrossSectionDefaultSurface si porta da solo ad alpha 0.75 e Ray
// Steps 600 su un'equazione T^3 (quartica di quartiche in x,y,z,p). Quell'alpha
// e' scritto con m_settingAlphaProgrammatic, quindi NON passa dalla conferma
// misurata dell'handler valueChanged: quando arriva la texture la scena e' gia'
// nel ramo trasparente senza che nessuna guardia l'abbia mai vista. Il costo per
// pixel del ramo trasparente e' MAX_FACES(8 desktop) x 3 marchNextLayer x fino a
// MAX_LAYER_STEPS passi x 4 map(): una texture RM non banale dentro map() lo
// moltiplica ancora, e il salto da "fluido" a "GPU fault" non passa per una zona
// grigia misurabile. Il watchdog lavora su una EMA e richiede animazione in
// corso: arriva sempre DOPO il magenta, quando c'e' ancora un'app a cui parlare.
// Per questo la guardia e' PREVENTIVA (prima del commit) e non misurata.
//
// Fa le tre cose insieme, come da richiesta: ferma il moto, ripristina l'opaco e
// chiede se ripristinarli. Ritorna true se l'utente sceglie di proseguire
// trasparente (chi chiama ha gia' applicato la texture: qui si decide solo il
// destino di alpha e moto).
bool MainWindow::guardTransparencyOnHeavyTextureApply(const QString &newDispCode)
{
    if (!ui->glWidget || !ui->alphaSlider) return true;
    if (m_heavyTexGuardActive) return true;                       // box gia' aperto: non impilare
    if (!implicitMode()) return true;    // solo Ray Marching
    if (ui->alphaSlider->value() >= 100) return true;             // scena gia' opaca: nulla da fare

    // IL CRITERIO E' IL DISPLACEMENT, non la texture. %DISPLACEMENT_CODE% e'
    // iniettato in rawField() e quindi valutato dentro ogni map() del marcher
    // (MAX_FACES x 3 x MAX_LAYER_STEPS x 4 per pixel sul ramo trasparente);
    // %TEXTURE_CODE% vive nello shading finale e costa una valutazione per pixel.
    // Senza displacement non c'e' nessun moltiplicatore da temere, per quanto
    // elaborata sia la texture di colore: era il bug del "popup anche con una
    // texture leggera". Commenti tolti col filtro delle validazioni del commit.
    const QString disp = stripCodeComments(newDispCode).trimmed();
    if (disp.isEmpty()) return true;

    // GIA' CHIESTO per questo displacement: non si richiede, qualunque sia stata
    // la risposta. E' la fine del popup "a raffica": con "Restore" l'alpha torna
    // <1 e il commit successivo -- che il riavvio stesso puo' innescare --
    // ritrovava condizioni identiche e riapriva il box all'infinito. Il confronto
    // sul CODICE riarma da solo quando cambia davvero il displacement.
    if (disp == m_heavyTexGuardAskedFor) return true;
    m_heavyTexGuardAskedFor = disp;

    // Stato da ripristinare se l'utente sceglie di proseguire.
    const int alphaPrev = ui->alphaSlider->value();
    const bool wasMoving = !m_masterStopped;

    // STOP PRIMA del box: il moto va tolto mentre la finestra e' aperta,
    // altrimenti la scena continua a marciare trasparente+texturizzata proprio
    // nei secondi in cui l'utente legge (ed e' li' che si blocca).
    if (wasMoving) performMasterStop();

    // Opaco PRIMA del box, per lo stesso motivo: lo stop toglie il moto, non il
    // costo per pixel: con alpha<1 ogni singolo ridisegno (il box stesso, un
    // resize) resta da secondi. Set programmatico: i check dell'handler sono
    // fuori luogo qui, la decisione la prende questo box.
    {
        // La trasparenza la toglie l'app: non e' lavoro dell'utente da salvare.
        AbsorbChangesGuard absorbOpaque(this);
        m_settingAlphaProgrammatic = true;
        ui->alphaSlider->setValue(100);
        m_settingAlphaProgrammatic = false;
    }

    // Il watchdog va zittito e il suo eventuale segnale gia' in coda scartato:
    // stesso motivo documentato in forceOpaqueForHeavyRM (il box modale fa girare
    // l'event loop e aprirebbe il popup del watchdog SOPRA questo).
    ui->glWidget->acknowledgePerformanceWarning();
    m_transparencyGuardActive = true;
    m_heavyTexGuardActive = true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Relief texture on a transparent surface"));
    box.setText(tr("This texture carves relief into the surface "
                   "(displacement), and the surface is currently transparent."));
    QString info = tr("The relief is computed at every step of every ray, and "
                      "transparency multiplies the number of those steps: "
                      "together they can make rendering collapse and freeze the "
                      "application, on desktop too.\n\n"
                      "The surface has been set to fully opaque");
    if (wasMoving)
        info += tr(" and the animation has been stopped");
    info += tr(" so you can see the result safely.\n\n");
    info += wasMoving
        ? tr("You can restore transparency and restart the animation at your "
             "own risk, or keep the surface opaque and stopped.")
        : tr("You can restore transparency at your own risk, or keep the "
             "surface opaque.");
    box.setInformativeText(info);
    QPushButton *restoreBtn = box.addButton(wasMoving ? tr("Restore and restart")
                                                      : tr("Restore transparency"),
                                            QMessageBox::AcceptRole);
    QPushButton *keepBtn    = box.addButton(wasMoving ? tr("Keep it opaque and stopped")
                                                      : tr("Keep it opaque"),
                                            QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    box.exec();
    const bool restore = (box.clickedButton() == restoreBtn);

    m_transparencyGuardActive = false;

    if (restore) {
        // Trasparenza PRIMA del riavvio, cosi' la scena riparte come l'utente
        // l'aveva lasciata. Programmatico: i check sono gia' stati assolti qui.
        m_settingAlphaProgrammatic = true;
        ui->alphaSlider->setValue(alphaPrev);
        m_settingAlphaProgrammatic = false;
        // Riavvio = vero master Start, come fa il gestore di performanceWarning:
        // onStartClicked riconosce sender()==m_btnStart e riarma i flag user-stop.
        if (wasMoving && m_btnStart) m_btnStart->click();
        // L'utente e' avvisato e prosegue: il watchdog resta zittito per QUESTA
        // scena (niente popup a raffica sullo stesso rallentamento). Si riarma
        // alla prossima presa dello slider o al prossimo rebuildShader.
        ui->glWidget->acknowledgePerformanceWarning();
    }

    // ULTIMA RIGA, non subito dopo exec(): il flag deve coprire anche il ramo
    // "restore" qui sopra. m_btnStart->click() rientra in onStartClicked, che
    // puo' ricommittare lo shader e ripassare di qui, e nel percorso libreria
    // syncImplicitAlphaSlider(newSurface=true) gira poche righe dopo il ritorno e
    // azzererebbe il "gia' chiesto" appena scritto (e' gated proprio da questo
    // flag). Abbassarlo dopo exec() lo rendeva cieco esattamente dove serviva.
    m_heavyTexGuardActive = false;
    return restore;
}

void MainWindow::updateRenderState()
{
    // Per primo, prima di qualunque ramo: vedi updateBackgroundControlsGate.
    updateBackgroundControlsGate();

    // 1. Identifichiamo se siamo in modalità Ray Marching
    bool isImplicitMode = (implicitMode());

        // --- DISATTIVAZIONE CONTROLLI NON SUPPORTATI ---
        ui->radioWF->setEnabled(!isImplicitMode);

        // Controlli densità Wireframe (uDensity/vDensity, contenitori dei tasti +/-):
        // attivi SOLO quando il Wireframe è la modalità di rendering attiva (e non in Ray
        // Marching, dove il wireframe non esiste). Disabilitare il widget contenitore
        // disabilita anche i suoi figli. Senza Wireframe la densità non ha effetto, quindi
        // i controlli vanno in grigio.
        // I radio MOSTRANO la mesh selezionata, quindi radioWF acceso significa
        // "la mesh che sto guardando e' in wireframe": e' esattamente la
        // condizione in cui i tasti densita' servono (agiscono su quella parte).
        bool wireframeDensityUsable = !isImplicitMode && shownRenderMode() == 2;
        ui->uDensity->setEnabled(wireframeDensityUsable);
        ui->vDensity->setEnabled(wireframeDensityUsable);

        // --- 1. DISATTIVAZIONE ROTAZIONI 4D ---
        // Omega/Phi/Psi vivono nel dock 3D insieme a Spin/Precessione/Nutazione.
        // Erano spenti in TUTTA la modalita' implicita perche' li' non avevano
        // effetto: lo shader ray marching non leggeva omega/phi/psi.
        //
        // Ora il sotto-tab CROSS SECTION li usa: l'equazione ha 4 variabili
        // (x,y,z,p) e la rotazione 4D decide quale sezione dell'ipersuperficie
        // si vede (vedi %CROSS_SECTION_P% in createImplicitFragmentShader).
        // Quindi restano spenti solo dove continuano a non avere effetto: il
        // sotto-tab "3D", la cui equazione e' a 3 variabili e non ha un p da
        // ruotare, e la modalita' parametrica non e' implicita affatto.
        const bool crossSectionActive = isImplicitMode && crossSectionTab();
        const bool rot4DUsable = !isImplicitMode || crossSectionActive;

        // Omega (W-X)
        ui->btnOmegaMinus->setEnabled(rot4DUsable);
        ui->btnOmegaPlus->setEnabled(rot4DUsable);

        // Phi (W-Y)
        ui->btnPhiMinus->setEnabled(rot4DUsable);
        ui->btnPhiPlus->setEnabled(rot4DUsable);

        // Psi (W-Z)
        ui->btnPsiMinus->setEnabled(rot4DUsable);
        ui->btnPsiPlus->setEnabled(rot4DUsable);

        // --- 3. DISATTIVAZIONE PANNELLO 4D ---
        // Il pannello contiene DUE famiglie di controlli con destini diversi in
        // Ray Marching:
        //  - path 4D e SPOSTAMENTI dell'osservatore (btnXPlus, btnPPlus, ...):
        //    scrivono m_cameraPos4D / m_observerPos, che il template ray
        //    marching NON legge (u_observerPos e u_cameraPos4D sono dichiarati
        //    nell'UBO ma non usati da nessuna riga dello shader). Restano spenti:
        //    accenderli darebbe tasti che non fanno nulla;
        //  - ROTAZIONI 4D della camera (btnOmegaAhead/Rear e i gemelli Phi/Psi):
        //    scrivono omega/phi/psi, cioe' ESATTAMENTE lo stato che il sotto-tab
        //    Cross Section usa per decidere quale sezione dell'ipersuperficie si
        //    vede. Sono i gemelli "a scatto" dei tasti Omega/Phi/Psi del dock 3D,
        //    gia' abilitati sopra con rot4DUsable.
        // NB: in Qt un figlio NON si riattiva dentro un padre disabilitato --
        // isEnabled() resta false comunque (verificato). Quindi NON si puo'
        // spegnere dockWidgetContents_3 a blocco e riaccendere le rotazioni
        // dopo: il contenitore va lasciato abilitato e si spengono i singoli
        // controlli che non hanno effetto.
        if (ui->dockWidgetContents_3) {
            ui->dockWidgetContents_3->setEnabled(true);
        }
        // Path 4D: ATTIVO nel sotto-tab Cross Section, spento nel "3D".
        //
        // Nel sotto-tab 3D le equazioni del path non hanno effetto, ed e' la
        // ragione per cui erano spente ovunque in ray marching: quel path
        // muoverebbe l'osservatore 4D (u_observerPos/u_cameraPos4D), che il
        // template non legge.
        //
        // Nel Cross Section invece funzionano gia', senza una riga di logica
        // nuova, perche' la camera del path 4D (CameraPaths) NON passa da quegli uniform. Scrive
        // due cose che il ray marcher usa entrambe:
        //  - setRotation4D(omega, phi, psi): e' lo stato che %CROSS_SECTION_P%
        //    legge per decidere quale sezione dell'ipersuperficie si vede, quindi
        //    lungo il path la sezione cambia;
        //  - setCameraFrom4DVectors(...): proietta la posa 4D in una camera 3D
        //    (projectPoint4Dto3D), cioe' proprio la matrice di vista da cui il
        //    marcher ricava il raggio (inverse(u_mvMatrix)).
        // In pratica il path muove la camera nello spazio 4D e la sezione segue:
        // la stessa semantica del ramo parametrico, dalla stessa funzione
        // condivisa fra tick live e loop di registrazione (CLAUDE.md).
        //
        // Lo slider di VELOCITA' del pannello resta abilitato in entrambi i
        // sotto-tab anche a path spento: e' la manopola con cui si dosano i tasti
        // P+/P- della sezione (obsSpeed = speed4D * kObs4DSpeedMul).
        if (ui->panelPath) {
            ui->panelPath->setEnabled(true);
        }
        const bool path4DUsable = !isImplicitMode || crossSectionActive;
        for (QWidget *w : { (QWidget*)ui->lineX_P,  (QWidget*)ui->lineY_P,
                            (QWidget*)ui->lineZ_P,  (QWidget*)ui->lineP_P,
                            (QWidget*)ui->lineAlpha_P, (QWidget*)ui->lineBeta_P,
                            (QWidget*)ui->lineGamma_P }) {
            if (w) w->setEnabled(path4DUsable);
        }

        // DEPARTURE e VIEW hanno DUE condizioni, non una: il modo deve
        // permettere il path 4D (path4DUsable) E i campi devono definirlo.
        // Accendendoli qui insieme ai campi si scriveva la sola prima: passando
        // da parametrico a Cross Section il tasto si accendeva anche a campi
        // vuoti, sovrascrivendo l'ultimo checkPathFields.
        // Non si duplica la seconda condizione: la decidono checkPathFields e
        // updateViewButtonsEnabled, che sanno anche del path IN CORSA (a timer
        // attivo il tasto resta acceso per poterlo fermare).
        if (!path4DUsable) {
            if (ui->btnDeparture) ui->btnDeparture->setEnabled(false);
            if (ui->pushView)     ui->pushView->setEnabled(false);
        } else {
            checkPathFields();   // accende Departure e, di seguito, i View
        }
        // Navigazione 4D: contenitore ACCESO, si spengono i singoli tasti di
        // SPOSTAMENTO dell'osservatore, che scrivono m_cameraPos4D/m_observerPos
        // -- stato che lo shader ray marching non usa.
        // SPOSTAMENTI X/Y/Z dell'osservatore: restano spenti in Ray Marching.
        // Non perche' siano concettualmente fuori luogo, ma perche' sarebbero un
        // DOPPIO spostamento: la posizione 3D della telecamera e' gia' quella
        // della matrice di vista (rayPos = inverse(u_mvMatrix) * origine, nel
        // main() del template), e sommare anche u_observerPos.xyz muoverebbe la
        // scena due volte. Per muoversi in 3D ci sono i controlli del dock 3D.
        // btnLightMode idem: il suo modo si applica solo quando is4DActive() e'
        // vero (glwidget ~600), cosa che in Ray Marching non accade.
        for (QPushButton *b : { ui->btnXPlus, ui->btnXMinus,
                                ui->btnYPlus, ui->btnYMinus,
                                ui->btnZPlus, ui->btnZMinus,
                                ui->btnLightMode }) {
            if (b) b->setEnabled(!isImplicitMode);
        }
        // TRASLAZIONE LUNGO P: il caso opposto, ed e' il controllo piu'
        // importante del Cross Section dopo le rotazioni. Muove la QUOTA del
        // piano di sezione (u_observerPos.w, letta da %CROSS_SECTION_P%), cioe'
        // fa scorrere la sezione lungo la quarta dimensione: le rotazioni
        // cambiano l'INCLINAZIONE del taglio, questa cambia DOVE taglia. La
        // quarta coordinata e' anche l'unica che la matrice di vista non
        // rappresenta, quindi qui non c'e' il doppio spostamento degli altri assi.
        for (QPushButton *b : { ui->btnPPlus, ui->btnPMinus }) {
            if (b) b->setEnabled(rot4DUsable);
        }
        // ROTAZIONI 4D della camera: scrivono omega/phi/psi, cioe' lo stato che
        // il sotto-tab Cross Section usa per scegliere la sezione. Sono i
        // gemelli "a scatto" dei tasti Omega/Phi/Psi del dock 3D, e seguono lo
        // stesso gate (rot4DUsable).
        for (QPushButton *b : { ui->btnOmegaAhead, ui->btnOmegaRear,
                                ui->btnPhiAhead,   ui->btnPhiRear,
                                ui->btnPsiAhead,   ui->btnPsiRear }) {
            if (b) b->setEnabled(rot4DUsable);
        }

        if (isImplicitMode) {
            // Ripristino forzato se l'utente era in modalità non compatibili
            if (m_scene.renderMode == 2) {
                m_scene.renderMode = 1;
                refreshRenderRadios();
            }
        }

    // 2. RECUPERA LO STATO AGGIORNATO
    // 'mode' governa il GATING dell'interfaccia (texture, trasparenza, densita'
    // wireframe): deve seguire cio' che l'utente sta guardando, cioe' la
    // modalita' EFFICACE della mesh selezionata. Su "All" coincide con lo stato
    // globale, quindi il comportamento storico non cambia.
    // Restano invece su m_scene.renderMode le decisioni sullo stato GLOBALE
    // (vedi 'isPhong' e il blocco che scrive nel motore piu' sotto).
    int mode = m_scene.renderMode;
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0
        && ui->glWidget->meshPartCount() > 1 && !isImplicitMode) {
        mode = ui->glWidget->activeMeshEffectiveRenderMode();
    }
    // L'accensione della texture di superficie nel motore NON si decide piu' qui
    // dal checkbox: il checkbox e' una vista (dello sfondo, della fascia
    // selezionata, spento in wireframe) e leggerlo come comando accendeva la
    // texture globale su una multi-mesh (superficie bianca o nera, vedi la
    // storia in applySurfaceTextureToEngine). Il motore segue l'intenzione
    // m_scene.surfaceTextureState, piu' sotto.

    // 3. LOGICA TEXTURE (Mantenendo il fix per il Background)
    // In Wireframe superficie (mode==2, non in editing sfondo) la texture della
    // superficie non e' visibile: oltre al checkbox Texture, disabilitiamo anche
    // il ramo Texture del dock Library (l'albero treeTextures), cosi' non si puo'
    // applicare una texture che non avrebbe effetto. In editing sfondo la texture
    // di background e' indipendente dal wireframe, quindi resta tutto attivo.
    bool wireframeSurface = (mode == 2 && !editingBackground());
    // Il checkbox Texture e' la vista della texture del bersaglio: etichetta,
    // abilitazione (spento in wireframe sulla superficie e a scena vuota) e
    // spunta si decidono in un posto solo.
    refreshTextureCheckbox();
    if (ui->treeTextures) ui->treeTextures->setEnabled(!wireframeSurface);

    // Collasso + grigio del ramo Texture solo alla TRANSIZIONE di stato, non a
    // ogni updateRenderState (chiamata di frequente). Entrando in wireframe le
    // cartelle si chiudono e restano chiuse; uscendo si ripristina il colore.
    if (wireframeSurface != m_textureLibraryGrayed) {
        setTextureLibraryGrayed(wireframeSurface);
        m_textureLibraryGrayed = wireframeSurface;
    }

    // Transparence e Light non hanno effetto in Wireframe: la superficie è disegnata
    // a colore piatto senza illuminazione (surface.frag, ramo u_renderMode == 2),
    // quindi i due slider vanno in grigio. Qui NON c'è l'esclusione radioBackground
    // usata sopra per la texture: questi slider agiscono SEMPRE sulla superficie
    // (l'alpha dello sfondo è forzato a 1), anche mentre si edita lo sfondo.
    // Disabilitiamo il PANNELLO contenitore (etichette comprese) e non i singoli
    // slider: agire sul parent preserva il flag enabled proprio di alphaSlider,
    // così i suoi blocchi indipendenti (campo a prodotto, vista 2D) sopravvivono
    // al passaggio per il wireframe.
    //
    // In wireframe i due slider tornano anche al DEFAULT (opaco, luce 100%): lo
    // shader usa comunque ubuf.alpha sulle linee, e un alpha stantio (< 1) le
    // renderebbe sbiadite con lo slider ormai bloccato. Reset incondizionato
    // finché mode == 2, non solo alla transizione: copre anche il load di un
    // preset wireframe con alpha salvato < 1 (setValue del load, poi questa
    // chiamata lo riporta a 1). A slider disabilitati nessun input utente da
    // preservare; uscendo dal wireframe restano ai default.
    // Il reset riguarda lo stato GLOBALE, quindi si fa solo quando e' il
    // globale a essere in wireframe (m_scene.renderMode), NON quando e'
    // semplicemente la mesh selezionata a esserlo: li' le altre mesh sono
    // ancora solide e azzerarne trasparenza e luce cancellerebbe l'aspetto
    // per-mesh appena impostato.
    if (mode == 2 && m_scene.renderMode == 2) {
        // Questi due sono reset AUTOMATICI del motore, non scelte dell'utente su
        // una singola mesh: vanno sullo stato globale. Senza il bypass, con una
        // mesh selezionata nello spinbox finivano scritti su QUELLA parte (che
        // smetteva di ereditare) e il giro di segnali che ne seguiva poteva
        // inchiodare il selettore. Si manifestava solo TORNANDO in wireframe,
        // perche' solo qui esiste questo ramo.
        // Si RIPRISTINA il valore precedente, non si spegne a forza: durante il
        // load di un preset il bypass e' gia' acceso (MeshBypassGuard di
        // applySurfaceExample), e updateRenderState passa di qui dentro quel
        // periodo. Spegnendolo, il resto del load proseguiva SENZA protezione e
        // il setColor del colore globale finiva scritto sulla parte attiva.
        const bool oldBypass = ui->glWidget && ui->glWidget->meshAppearanceBypass();
        // Il bypass salta anche il congelamento di setAlpha: va fatto qui, o le
        // mesh trasparenti che ereditano si ritrovano opache tornando su "Mesh"
        // (wireframe scelto da "All": la loro modalita' e' gia' stata congelata
        // da pinInheritedRenderModes, l'alpha no). Non durante un load.
        if (!oldBypass && ui->glWidget && ui->glWidget->globalAlpha() != 1.0f)
            ui->glWidget->pinInheritedAlphas();
        if (ui->glWidget) ui->glWidget->setMeshAppearanceBypass(true);
        resetTransparency();
        ui->lightSlider->setValue(100);   // valueChanged aggiorna intensità e label
        if (ui->glWidget) ui->glWidget->setMeshAppearanceBypass(oldBypass);
    }
    // Il FOV vive ORA dentro panelTransp (scelta di layout), ma NON deve
    // seguirne l'abilitazione: disabilitare un contenitore disabilita tutti i
    // figli, e il FOV deve restare sempre attivo (agisce sulla camera, non
    // sull'aspetto della superficie; vedi la nota storica sullo slider unico).
    // Percio' qui si disabilitano i figli che riguardano davvero la
    // trasparenza/luce, non il groupbox.
    const bool transpUsable = (mode != 2);
    // Col bersaglio BORDER la trasparenza agiva sulla superficie (segnalato
    // dall'utente, 10-17): il bordo e' sempre opaco -- si disegna per primo,
    // con la pipeline opaca. Spenta, col perche' nel tooltip (regola dei
    // controlli inerti); la luce no, vale per tutta la scena, bordo compreso.
    const bool onBorder = editingBorder();
    const bool transpOn = transpUsable && !onBorder;
    if (ui->lblTrans)         ui->lblTrans->setEnabled(transpOn);
    if (ui->panelSliderTrans) {
        ui->panelSliderTrans->setEnabled(transpOn);
        ui->panelSliderTrans->setToolTip(onBorder
            ? tr("The border is always opaque: select Shape to change the transparency of the surface.")
            : QString());
    }
    if (ui->lblLight)         ui->lblLight->setEnabled(transpUsable);
    if (ui->widget_2)         ui->widget_2->setEnabled(transpUsable);
    // Il groupbox resta abilitato: lo spegnerebbe insieme al FOV.
    ui->panelTransp->setEnabled(true);

    // Slider trasparenza su campo implicito mal condizionato (vedi syncImplicitAlphaSlider).
    syncImplicitAlphaSlider(isImplicitMode);

    bool isPhong = (m_scene.renderMode == 1);

    // 4. APPLICAZIONE AL MOTORE GRAFICO
    if (ui->glWidget) {
        ui->glWidget->setSpecularEnabled(isPhong);

        // Texture di superficie: accesa = intenzione e non wireframe (il
        // wireframe di UNA fascia non la spegne). Regola in un punto solo.
        applySurfaceTextureToEngine();

        if (isImplicitMode) {
            // Modalità Ray Marching: il render mode viene dai radio Shell/Solid
            // (panelRenderControls, comune ai due sotto-tab). Si passa
            // dall'helper e non da ui->radioShell diretto perche' quello e' il
            // punto unico di lettura -- quando i radio erano duplicati per
            // sotto-tab, leggerne uno fisso qui cancellava a ogni cambio
            // tab/proiezione/load la scelta fatta nell'altro.
            ui->glWidget->setGlobalRenderMode(implicitShellSelected() ? 1 : 0);
        } else {
            // Modalità Parametrica: Ascolta i radio button classici.
            // (il gating dello slider Thickness e' in fondo a questa funzione)
            // NB: setGlobalRenderMode scrive SOLO lo stato GLOBALE, mai su una parte,
            // anche se lo spinbox ha una mesh selezionata. Questa funzione gira
            // a ogni cambio tab / proiezione / load: se scrivesse sulla parte
            // attiva, il valore mostrato dai radio (che e' quello della mesh
            // selezionata) verrebbe riapplicato a un destinatario diverso ogni
            // volta, propagando il wireframe alle mesh che ereditano. La sola
            // via che scrive una modalita' per-parte e' onUserRenderModeChosen,
            // cioe' un click esplicito dell'utente.
            //
            // Quando una mesh e' selezionata i radio mostrano LEI, quindi non
            // sono una fonte valida per il globale: lo lasciamo com'e'.
            const bool showingPart =
                (ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1);
            if (!showingPart) ui->glWidget->setGlobalRenderMode(m_scene.renderMode);
        }

        ui->glWidget->update();
    }

    // ==========================================================
    // SCENA VUOTA (tasto NEW): controlli di RESA in grigio
    // ==========================================================
    // I comandi legati al CONTENUTO si spengono gia' da soli a campi vuoti --
    // i Run del dock Equations via i flag "applied", Run/Save script via
    // hasGLSLCode, Run/Save texture via i controlli campi-vuoti, gli slider
    // A..F/S via updateConstantsUIState. Restano accesi solo quelli di RESA,
    // che non hanno mai avuto un gate sull'esistenza di una superficie perche'
    // fino a ora una superficie c'era sempre: agirebbero sul nulla.
    // Le eccezioni sono volute: COLORE e TEXTURE dello SFONDO restano usabili,
    // perche' lo sfondo esiste anche senza superficie ed e' l'unica cosa che
    // si puo' ancora comporre a scena vuota.
    // SPESSORE DEL GUSCIO: ha senso solo in Ray Marching e solo con Shell
    // selezionato -- in Solid non c'e' nessun guscio di cui regolare la parete, e
    // nel parametrico la modalita' non esiste affatto. Il contenitore intero
    // (etichetta, valore, slider) cosi' il numero non resta leggibile accanto a
    // uno slider spento.
    // Etichetta E slider: il pannello contenitore non esiste piu' (i tre
    // controlli vivono nella griglia gridRenderControls, che li allinea in
    // colonne condivise), quindi si disabilitano i due widget direttamente --
    // cosi' il testo non resta leggibile accanto a uno slider spento.
    {
        const bool shellUsable = isImplicitMode && implicitShellSelected();
        if (ui->lblShellThickness)    ui->lblShellThickness->setEnabled(shellUsable);
        if (ui->shellThicknessSlider) ui->shellThicknessSlider->setEnabled(shellUsable);
    }

    // FILL LIGHT: solo in Ray Marching. Lo shader parametrico DICHIARA
    // u_fillLight (il blocco UBO deve combaciare campo per campo col fragment,
    // regola Adreno) ma non lo usa, e non per una dimenticanza: il ramo
    // parametrico illumina con abs(dot(N,L)) -- two-sided, quindi la faccia
    // interna di un tubo e' gia' illuminata quanto l'esterna -- e alza il
    // risultato con un fondo fisso (0.3 + 0.7*diff), per cui nessuna faccia
    // scende sotto il 30%. Non ha le zone nere che questa luce va a recuperare:
    // nel ray marching una faccia radente misurava 0.06 su 1.
    // Sommarci un riempimento schiarirebbe tutto invece di recuperare le ombre,
    // appiattendo il modellato. Quindi il controllo si SPEGNE qui, invece di
    // restare acceso senza fare nulla.
    if (ui->lblFill)    ui->lblFill->setEnabled(isImplicitMode);
    if (ui->widgetFill) ui->widgetFill->setEnabled(isImplicitMode);

    // RAY STEPS e STEP RELAX restano ACCESI anche col marcher "Precise". Erano
    // stati spenti come "senza effetto", ed era sbagliato: in Precise governano
    // l'AVVICINAMENTO (marchField), e il suo esito non e' solo un'ottimizzazione.
    // Decide quali raggi sono sfondo -- un raggio che esce dalla scena senza aver
    // visto un cambio di segno viene scartato senza seconda marcia, anche se un
    // passo lungo ha scavalcato per intero una parete sottile -- e da dove riparte
    // la ricerca a cambio di segno, che guarda solo IN AVANTI. Verificato a
    // schermo: difetti presenti in Precise sparivano dopo aver mosso i due slider.
    // La misura sul T^3 che aveva motivato il gate riguardava il tetto di
    // marchNextLayer, non questi due valori.

    // Ultimi blocchi della funzione: sovrascrivono di proposito le decisioni
    // prese qui sopra, che presuppongono tutte una superficie a schermo e
    // l'editing della superficie.
    applyEmptySceneGating();
    updateSurfaceControlsGate();
    updateBorderControlsGate();
    updateMeshCountControls();   // campo Meshes: spento in Ray Marching e in Background
}

void MainWindow::updateBorderControlsGate()
{
    const bool rayMarching = implicitMode();
    // SUPERFICIE CHIUSA (toro, sfera, tori di Hopf...): ogni lato e' una
    // cucitura, un polo o giace su un'altra parte, e il bordo non comparirebbe.
    // Acceso, radio e spessore non farebbero nulla a schermo ma sporcherebbero
    // la scena (avviso di salvataggio al cambio di record). Lo dice la sonda
    // dei lati coperti (GLWidget::borderAvailability), che arriva poco dopo il
    // caricamento e dopo ogni cambio di forma. Anche col bordo ACCESO (scelta
    // dell'utente, 10-17): aperto il toro riducendo un intervallo, acceso il
    // bordo e richiuso il toro, il bordo spariva ma Border restava attivo. Ora
    // il radio si spegne e lo spessore resta quello, fermo: riaprendo la
    // superficie il bordo ricompare uguale.
    GLWidget *gl = ui->glWidget;
    const bool closed = !rayMarching && gl
                        && gl->borderAvailability() == GLWidget::BorderAvailability::None;
    // Senza superficie (dopo NEW) non c'e' un bordo a cui applicare niente:
    // acceso, il radio sporcherebbe la scena.
    const bool empty = isSceneEmpty();
    const bool usable = !rayMarching && !closed && !empty;
    if (ui->radioBorder) {
        ui->radioBorder->setEnabled(usable);
        // Da spento deve dire PERCHE' (regola dei controlli inerti).
        ui->radioBorder->setToolTip(usable ? tr("Edit the border: its color with the sliders below, its thickness with Border Thickness.")
                                    : empty ? tr("There is no surface: the border follows the edges of a parametric surface.")
                                    : rayMarching ? tr("The border follows the edges of a parametric surface: not available in ray marching.")
                                                  : tr("This surface is closed: every edge is a seam, a pole or lies on another "
                                                       "part of the surface, so there is no border to draw."));
    }
    if (!usable && editingBorder()) {
        setEditTarget(EditTarget::Surface);
        onColorTargetChanged();
        // Le regole dei comandi di resa dipendono dal bersaglio (la
        // trasparenza e' spenta col Border): si rifanno, fuori da questo giro --
        // qui si e' gia' dentro updateRenderState, che chiama questo gate.
        QTimer::singleShot(0, this, [this] { updateRenderState(); });
    }
}

// COMANDI DELLA SUPERFICIE SPENTI MENTRE SI EDITA LO SFONDO (radio Background in
// testa al riquadro dello sfondo, nel dock RENDERER), fino al ritorno su Surface. Speculare a
// updateBackgroundControlsGate. Si spengono modo di resa e densita' wireframe,
// trasparenza, luce, Headlight e FOV (scelta dell'utente, 10-09: e' un comando
// della scena, non dello sfondo): restando accesi sembravano comandi dello
// sfondo. Restano accesi texture e Color 1/2 (in Background sono quelli dello
// sfondo) e gli slider RGB (colore dello sfondo). L'ambito All/Mesh lo spegne
// gia' updateMeshScopeEnabled.
// I radio Surface/Border stanno nello stesso riquadro e devono restare
// cliccabili (sono la via del ritorno): si spegne il contenitore interno
// panelSurfaceModes, non panelSurface.
// panelSurfaceModes non ha altre regole e si commuta direttamente: i figli spenti di
// proposito (Wireframe in Ray Marching, densita' fuori dal wireframe, radio a
// scena vuota) restano spenti, perche' Qt ricorda chi e' stato disabilitato
// esplicitamente. Trasparenza, luce e Headlight hanno invece regole loro su
// QUESTI contenitori (updateRenderState qui sopra, applyEmptySceneGating, che
// riaccende anche il FOV): qui si spengono soltanto, per ultimi; tornando su
// Surface li rimette a posto updateRenderState, che l'handler di
// radioBackground chiama.
void MainWindow::updateSurfaceControlsGate()
{
    const bool onBackground = editingBackground();
    if (ui->panelSurfaceModes) ui->panelSurfaceModes->setEnabled(!onBackground);
    // FOV in ENTRAMBI i versi: nessun'altra regola lo riaccende tornando su
    // Surface (applyEmptySceneGating lo fa solo all'uscita dalla scena vuota).
    const bool fovOn = !onBackground && !isSceneEmpty();
    for (QWidget *w : { static_cast<QWidget*>(ui->lblFov), static_cast<QWidget*>(ui->lblValFov),
                        static_cast<QWidget*>(ui->fovSliderMain) })
        if (w) w->setEnabled(fovOn);
    if (!onBackground) return;
    for (QWidget *w : { static_cast<QWidget*>(ui->lblTrans), static_cast<QWidget*>(ui->panelSliderTrans),
                        static_cast<QWidget*>(ui->lblLight), static_cast<QWidget*>(ui->widget_2),
                        static_cast<QWidget*>(ui->lblFill),  static_cast<QWidget*>(ui->widgetFill) })
        if (w) w->setEnabled(false);
}

// Gate dei controlli di RESA sulla scena vuota. Sede unica, chiamata sia da
// updateRenderState (che decide tutto il resto dell'aspetto) sia da
// updateMasterButtonState: il Run NON passa da updateRenderState -- onStartClicked
// ha 31 uscite e nessuna la richiama -- quindi senza il secondo chiamante i
// controlli restavano grigi anche dopo aver ricostruito una superficie, e
// l'unico modo di riaverli era cambiare tab.
void MainWindow::applyEmptySceneGating()
{
    if (isSceneEmpty()) {
        // Trasparenza e luce: gli STESSI figli che il ramo wireframe disabilita
        // qui sopra. Si agisce sui figli e non sul groupbox panelTransp perche'
        // quello si porterebbe dietro anche il FOV, che ha una regola sua (piu'
        // sotto: a scena vuota si spegne anche lui, ma va detto esplicitamente).
        if (ui->lblTrans)         ui->lblTrans->setEnabled(false);
        if (ui->panelSliderTrans) ui->panelSliderTrans->setEnabled(false);
        if (ui->lblLight)         ui->lblLight->setEnabled(false);
        if (ui->widget_2)         ui->widget_2->setEnabled(false);

        ui->uDensity->setEnabled(false);
        ui->vDensity->setEnabled(false);

        // Texture: chkBoxTexture e' UN SOLO widget per due destinatari -- vale
        // per la superficie o per lo SFONDO a seconda di radioSurface/
        // radioBackground (cambia solo l'etichetta, ~2085). Spegnerlo sempre
        // bloccava anche la texture di sfondo, che a scena vuota deve restare
        // usabile: lo sfondo esiste anche senza superficie ed e' l'unica cosa
        // che si puo' ancora comporre. Stessa ragione per il ramo Library.
        // La regola sta nella vista (refreshTextureCheckbox: a scena vuota
        // resta acceso solo col bersaglio Background).
        refreshTextureCheckbox();
        // Il ramo Texture della Library resta USABILE anche senza superficie e
        // fuori da Background: cliccare una texture a scena vuota ricostruisce
        // la superficie di DEFAULT del tab corrente e ce la applica sopra (vedi
        // ensureSurfaceForTexture, chiamata dal click). Il checkbox qui sopra
        // invece resta legato allo sfondo: e' il display di uno stato, non un
        // comando che possa creare una superficie.
        if (ui->treeTextures) ui->treeTextures->setEnabled(true);

        // Risoluzione/passi: nulla da campionare.
        ui->stepSlider->setEnabled(false);
        ui->lineSteps->setEnabled(false);

        // Modo di resa della SUPERFICIE: non c'e' superficie da rendere.
        // (radioShell/radioSolid sono gli equivalenti del ramo Ray Marching, e
        // vivono in panelRenderControls insieme a Thickness e ai radio marcher.)
        if (ui->radioBasic)  ui->radioBasic->setEnabled(false);
        if (ui->radioPhong)  ui->radioPhong->setEnabled(false);
        if (ui->radioWF)     ui->radioWF->setEnabled(false);
        if (ui->radioShell)  ui->radioShell->setEnabled(false);
        if (ui->radioSolid)  ui->radioSolid->setEnabled(false);
        // Scelta del marcher: come Shell/Solid, riguarda solo il ray marching.
        if (ui->radioMarcherFast)    ui->radioMarcherFast->setEnabled(false);
        if (ui->radioMarcherPrecise) ui->radioMarcherPrecise->setEnabled(false);

        // Ambito multi-mesh: le parti non esistono piu'. Senza questo, restava
        // selezionabile la mesh di una superficie che non c'e' -- e i comandi
        // di aspetto sarebbero finiti dentro un MeshPart fantasma.
        if (ui->radioMeshAll) ui->radioMeshAll->setEnabled(false);
        if (ui->radioMeshOne) ui->radioMeshOne->setEnabled(false);
        if (ui->spinMeshSel)  ui->spinMeshSel->setEnabled(false);
        if (ui->panelMultiMesh) ui->panelMultiMesh->setEnabled(false);

        // Picker Color1/Color2: dipendono dalla texture ATTIVA (quali token
        // u_col1/u_col2 referenzia). A scena vuota la texture di superficie e'
        // spenta, quindi updateTextureUIState li spegne da se': va solo
        // CHIAMATA, perche' il reset non la invocava mai e i due radio
        // restavano accesi com'erano sotto il preset precedente.
        // In editing sfondo decide lo stato della texture di background, che
        // resta legittimamente componibile.
        updateTextureUIState(editingBackground() && targetTextureOn());

        // Slider RGB: NON si toccano da qui. La loro sede unica e'
        // onColorTargetChanged, che gira anche dopo questo gate e li
        // riaccenderebbe: il caso "scena vuota" e' dentro di lei, insieme alle
        // altre ragioni per cui possono essere inerti (texture senza colori).
        onColorTargetChanged();

        // FOV: agisce sulla camera, non sulla superficie, ed e' per questo che
        // il ramo wireframe lo lascia sempre attivo. Ma a scena VUOTA non c'e'
        // nulla da inquadrare, e il criterio qui e' "si spegne tutto tranne lo
        // sfondo": inquadrare il nulla non e' un'eccezione che vale la pena.
        if (ui->fovSliderMain) ui->fovSliderMain->setEnabled(false);
        if (ui->lblFov)        ui->lblFov->setEnabled(false);
        if (ui->lblValFov)     ui->lblValFov->setEnabled(false);

        m_emptySceneGated = true;
    }
    else if (m_emptySceneGated) {
        // La scena e' tornata piena (primo Run dopo un NEW): si riaccende SOLO
        // cio' che questo gate aveva spento, e solo la prima volta -- da qui in
        // poi comandano di nuovo le regole normali (wireframe, ray marching,
        // ambito mesh), che questo blocco non deve piu' scavalcare.
        // Trasparenza, luce e densita' non si toccano: dipendono dal render mode
        // e li rimette a posto updateRenderState, che gira a ogni cambio di
        // modalita'. Qui basta togliere il blocco su cio' che nessun altro
        // riaccenderebbe.
        m_emptySceneGated = false;

        ui->stepSlider->setEnabled(true);
        ui->lineSteps->setEnabled(true);

        // Modo di resa: si riaccende solo cio' che la modalita' corrente
        // ammette. radioWF non esiste in Ray Marching e radioShell/radioSolid
        // non esistono in parametrico: la scelta la rifa' updateRenderState
        // (che gestisce anche il ripristino forzato se si veniva da wireframe),
        // qui basta togliere il blocco.
        const bool isRM = (implicitMode());
        if (ui->radioBasic)  ui->radioBasic->setEnabled(true);
        if (ui->radioPhong)  ui->radioPhong->setEnabled(true);
        if (ui->radioWF)     ui->radioWF->setEnabled(!isRM);
        if (ui->radioShell)  ui->radioShell->setEnabled(true);
        if (ui->radioSolid)  ui->radioSolid->setEnabled(true);
        // Scelta del marcher: SOLO in Ray Marching (nel ramo parametrico non c'e'
        // marcher). radioWF qui sopra usa la stessa condizione, al contrario.
        if (ui->radioMarcherFast)    ui->radioMarcherFast->setEnabled(isRM);
        if (ui->radioMarcherPrecise) ui->radioMarcherPrecise->setEnabled(isRM);
        // NB: il gate di Ray Steps col marcher Precise NON va qui. Questo ramo
        // gira una volta sola, all'uscita dalla scena vuota (m_emptySceneGated
        // viene azzerato sopra): con una superficie gia' carica non passa mai.
        // Vive in updateRenderState, che gira a ogni cambio di stato.

        // Ambito multi-mesh: lo stato giusto lo conosce updateMeshScopeEnabled
        // (dipende da quante parti ha la superficie appena costruita), che ha
        // gia' la sua logica di "usable". Qui si toglie solo il blocco del
        // pannello e si lascia decidere a lei.
        if (ui->panelMultiMesh) ui->panelMultiMesh->setEnabled(true);
        updateMeshScopeEnabled();

        // Slider RGB: li rimette onColorTargetChanged, che sa se il target e'
        // superficie, sfondo o una texture senza colori (in quel caso restano
        // spenti di proposito).
        onColorTargetChanged();

        // FOV di nuovo utile: c'e' qualcosa da inquadrare.
        if (ui->fovSliderMain) ui->fovSliderMain->setEnabled(true);
        if (ui->lblFov)        ui->lblFov->setEnabled(true);
        if (ui->lblValFov)     ui->lblValFov->setEnabled(true);

        // Trasparenza, luce, densita' e texture le rimette a posto
        // updateRenderState secondo le regole normali. NON la si chiama da qui:
        // e' lei a chiamare questa funzione, e si avviterebbe. Quando il gate si
        // apre dal Run (via updateMasterButtonState) la si invoca dal chiamante.
    }
}

// Scena vuota = nessuna geometria a schermo. Non guarda i campi della UI (che
// l'utente puo' aver gia' ricominciato a riempire) ma cio' che il motore ha
// davvero da disegnare: in parametrico la mesh, in ray marching l'equazione
// implicita. E' la stessa condizione che il tasto NEW produce via
// resetScene(..., false).
// Scena vuota + click su una texture: si ricostruisce la superficie di DEFAULT
// del tab corrente (sfera in Ray Marching, superficie parametrica di avvio in
// Parametric) cosi' la texture ha su cosa apparire. Senza, il click non
// produceva nulla di visibile e il ramo Texture della Library restava
// disabilitato apposta per non offrire un gesto inerte -- ma cosi' da una scena
// vuota non c'era modo di ripartire da una texture.
//
// Si passa da resetScene(..., loadDefaultSurface=true), cioe' lo STESSO percorso
// del cambio tab e del reset: nessuna seconda copia della costruzione di default
// da tenere allineata. Non e' un ramo nuovo, e' quello gia' in produzione.
//
// Ritorna true se ha ricostruito qualcosa: il chiamante lo usa per sapere che la
// scena e' cambiata sotto i piedi e deve rileggerla.
bool MainWindow::ensureSurfaceForTexture()
{
    if (!isSceneEmpty()) return false;
    // In editing SFONDO non serve: lo sfondo esiste anche senza superficie ed e'
    // proprio il caso che a scena vuota resta componibile.
    if (editingBackground()) return false;

    resetScene(implicitMode() ? 1 : 0, /*loadDefaultSurface=*/true);
    return true;
}

bool MainWindow::isWholeSceneEmpty() const
{
    return ui->glWidget && isSceneEmpty();
}

bool MainWindow::isSceneEmpty() const
{
    if (!ui->glWidget) return false;

    if (implicitMode()) {
        // Il ramo ATTIVO nel marcher, non sempre quello del sotto-tab 3D: nel
        // Cross Section il campo compilato e' m_eqCrossSectionF, e leggere
        // implicitEquation() giudicava la scena su un'equazione che a schermo
        // non c'e'. Dopo un New nel Cross Section la scena restava percio'
        // "vuota" anche a superficie ripristinata, e il gate non si sganciava
        // piu': i controlli del tab Render (Basic/Phong, wireframe, texture)
        // rimanevano disabilitati finche' non si cambiava modalita'.
        const QString eq = ui->glWidget->activeImplicitEquation().trimmed();
        return eq.isEmpty() || eq == QLatin1String(kEmptyImplicitField);
    }

    const SurfaceEngine *e = ui->glWidget->getEngine();
    return !e || e->getIndices().empty();
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void MainWindow::resetTransparency()
{
    // setValue(100) scatena il valueChanged dello slider, che aggiorna
    // l'etichetta e la GPU (setAlpha) in un colpo. Se eravamo gia' a 100 il
    // segnale non scatta, quindi forziamo a mano label e GPU per coprire anche
    // quel caso. (L'alpha globale vive nel motore: nessuna copia qui.)
    if (ui->alphaSlider->value() != 100) {
        ui->alphaSlider->setValue(100);
    }
    ui->lblAlphaVal->setText("1.00");
    if (ui->glWidget) ui->glWidget->setAlpha(1.0f);
}

// Shell/Solid del Ray Marching. UNICA implementazione: la chiamano entrambi i
// rami implicit di applyCommonData (equazione E script) e il reset alla sfera di
// default in resetScene. Prima esisteva solo dentro il ramo EQUAZIONE: siccome i
// record RM sono quasi tutti da SCRIPT, caricare un record (o la sfera di
// default) lasciava i radio e il motore sullo stato del preset precedente.
// I radio sono esclusivi e il loro toggled riscrive comunque il globalRenderMode:
// li muoviamo a segnali bloccati e scriviamo noi il motore, per non far girare
// l'handler durante un load.
void MainWindow::applyImplicitShellMode(bool shell)
{
    setImplicitShell(shell);
    if (ui->glWidget) ui->glWidget->setGlobalRenderMode(shell ? 1 : 0);
}

void MainWindow::setImplicitShell(bool shell)
{
    m_scene.implicitShell = shell;
    // UNA SOLA coppia di radio, in panelRenderControls (widget comune ai due
    // sotto-tab). Lo stato Shell/Solid e' sempre stato UNO SOLO — il render mode
    // del motore e' globale e il preset lo salva in un solo campo (renderMode
    // composito, >= 10 = Shell) — e da quando i radio non sono piu' duplicati per
    // sotto-tab non c'e' nulla da riallineare: la selezione non puo' contraddire
    // cio' che si vede.
    if (ui->radioShell && ui->radioSolid) {
        const bool oldShell = ui->radioShell->blockSignals(true);
        const bool oldSolid = ui->radioSolid->blockSignals(true);
        if (shell) ui->radioShell->setChecked(true);
        else       ui->radioSolid->setChecked(true);
        ui->radioShell->blockSignals(oldShell);
        ui->radioSolid->blockSignals(oldSolid);
    }
}

// Spessore del guscio: motore + posizione dello slider in un colpo. La curva dello
// slider e' quadratica (0..100 -> 0.005..0.30), quindi la posizione si ricava
// invertendola: f = sqrt((t - min) / (max - min)). Tenerla qui evita che ogni
// chiamante se la riscriva -- ed e' la stessa curva definita nel connect dello
// slider, che resta l'unico punto in cui i due estremi sono scritti.
void MainWindow::setShellThicknessUI(float thickness)
{
    // Chi scrive qui (load, reset, clic su Solid) decide lo spessore da capo:
    // lo spessore ricordato per il ritorno da Solid non vale piu'. Il clic su
    // Solid lo reimposta DOPO questa chiamata (updateImplicitRenderMode).
    m_shellThicknessBeforeSolid = -1.0f;

    const double kMin = 0.005, kMax = 0.30;
    const double t = qBound(kMin, (double)thickness, kMax);

    if (ui->glWidget) ui->glWidget->setShellThickness((float)t);

    if (ui->shellThicknessSlider) {
        const double f = std::sqrt((t - kMin) / (kMax - kMin));
        const bool old = ui->shellThicknessSlider->blockSignals(true);
        ui->shellThicknessSlider->setValue(qRound(f * 100.0));
        ui->shellThicknessSlider->blockSignals(old);
    }
}

// LUCE DI RIEMPIMENTO (dock Renderer): motore + slider + etichetta in un colpo.
// La conversione dello slider non e' l'identita' (0..100 -> 0.00..1.20, fattore
// 0.012), quindi ogni chiamante che scrivesse i widget a mano dovrebbe
// ripeterla -- ed e' la ragione per cui questa funzione esiste, come la gemella
// setShellThicknessUI.
//
// A SEGNALI BLOCCATI: serve dal caricamento di un preset e dai reset, dove la
// scrittura NON deve passare per l'handler dello slider (che la scambierebbe per
// una modifica dell'utente). Il motore lo scriviamo qui, esplicitamente, perche'
// setValue non emette valueChanged quando il valore coincide con quello corrente
// -- due preset di fila con la stessa luce lascerebbero la GPU non aggiornata.
void MainWindow::setBorderUI(const GLWidget::BorderStyle &style)
{
    GLWidget::BorderStyle s = style;
    s.radius = qBound(0.0f, s.radius, kBorderThicknessMax);
    const float t = s.radius;
    // Scena nuova: lo stop manuale della texture del bordo non vale piu'.
    m_userStoppedBorderTexClock = false;
    if (ui->glWidget) {
        ui->glWidget->setBorderStyle(s);
        // Una texture animata del bordo parte con la scena, salvo master fermo.
        ui->glWidget->setBorderTextureAnimating(!m_masterStopped && ui->glWidget->borderTextureShown()
                                                && hasTimeVariable(ui->glWidget->borderTextureCode()));
    }
    if (ui->borderThicknessSlider) {
        const bool old = ui->borderThicknessSlider->blockSignals(true);
        ui->borderThicknessSlider->setValue(sliderFromBorderThickness(t));
        ui->borderThicknessSlider->blockSignals(old);
    }
    if (ui->lblValBorderThickness) ui->lblValBorderThickness->setText(borderThicknessText(t));
}

void MainWindow::setFillLightUI(float v)
{
    const float val = qBound(0.0f, v, 1.20f);
    if (ui->glWidget) ui->glWidget->setFillLight(val);
    if (ui->fillLightSlider) {
        const bool old = ui->fillLightSlider->blockSignals(true);
        ui->fillLightSlider->setValue(qRound(val / 0.012f));
        ui->fillLightSlider->blockSignals(old);
    }
    if (ui->lblValFill) ui->lblValFill->setText(QString::number(val, 'f', 2));
}

// MARCHER (radio Fast/Precise): scrive il motore E i radio, a segnali bloccati.
// Gemella di setShellThicknessUI: serve dal caricamento di un preset e dai reset,
// dove la scrittura NON deve passare per l'handler dei radio (che la scambierebbe
// per una modifica dell'utente e accenderebbe l'avviso "lavoro non salvato").
void MainWindow::setMarcherUI(bool precise)
{
    if (ui->glWidget) ui->glWidget->setHybridMarcher(precise);

    // ENTRAMBI bloccati, come fa applyImplicitShellMode: l'esclusivita' spegne
    // l'altro radio, e se i suoi segnali non sono bloccati il suo toggled(false)
    // parte comunque (l'handler lo scarta con "solo chi si accende", ma il
    // pattern del progetto e' bloccare la coppia e scrivere il motore qui).
    QRadioButton *fast = ui->radioMarcherFast;
    QRadioButton *prec = ui->radioMarcherPrecise;
    if (fast && prec) {
        const bool oldFast = fast->blockSignals(true);
        const bool oldPrec = prec->blockSignals(true);
        if (precise) prec->setChecked(true);
        else         fast->setChecked(true);
        fast->blockSignals(oldFast);
        prec->blockSignals(oldPrec);
    }
}

// FOV UNICO. Unico punto che imposta il campo visivo: aggiorna slider + etichetta
// del dock renderer e applica SEMPRE il valore alla proiezione, senza condizioni.
//
// Prima c'erano applyPathFov3D/applyPathFov4D, una per dock, che applicavano il
// valore SOLO se la rispettiva path era in corsa. Con entrambi i path fermi il
// FOV non era piu' modificabile (gli slider erano anche disabilitati), quindi
// dopo un path a FOV largo l'inquadratura restava tale fino a Reset View; e coi
// due path attivi in sequenza i due slider si contendevano lo stesso
// m_cameraFov. Un solo controllo, sempre attivo, elimina entrambi i problemi.
//
// Il valore unico e' m_scene.fov; PresetSerializer lo scrive ancora anche come
// fov3D/fov4D nel JSON: i file salvati da questa build restano leggibili dalle
// build precedenti, che li usavano per i path.
// ==========================================================
// ASPETTO PER-MESH: spinbox di selezione
// ==========================================================
// Valuta la direttiva "MESH_VISIBLE := <espressione>;" con le costanti A..S
// correnti. 0 = non dichiarata (o non valutabile): il chiamante usa allora tutte
// le mesh dichiarate, cioe' il comportamento di sempre.
// Si rivaluta ogni volta invece di memorizzare un numero perche' l'espressione
// dipende dalle costanti: nei tori di Hopf e' "E", e deve seguire lo slider.
int MainWindow::meshVisibleCount() const
{
    if (m_meshVisibleExpr.isEmpty()) return 0;

    // const_cast: resolveCascadeConstants non e' const (aggiorna la cache
    // m_lastValidConst dei valori buoni). Qui pero' e' l'unico effetto, e scrive
    // lo stesso valore che i campi hanno gia': con restoreTextOnNegative = false
    // non tocca la UI.
    MainWindow *self = const_cast<MainWindow*>(this);
    const CascadeConstants k = self->resolveCascadeConstants(false);

    // ATTENZIONE, NON passare l'espressione a parseUIConstant cosi' com'e':
    // ExprTk e' CASE-INSENSITIVE e add_constants() registra 'e' = numero di
    // Nepero, che vince su add_constant("E", ...). "MESH_VISIBLE := E" veniva
    // quindi valutato 2.71828 -> floor(+0.5) = 3 mesh invece di 5 (bug visto sui
    // tori di Hopf con E = 5). Stessa famiglia della nota su PI/e/tau riservati
    // nel traduttore.
    // Percio' le lettere delle costanti si sostituiscono QUI col loro valore
    // numerico, prima di dare il testo al parser: cosi' 'E' non arriva mai a
    // ExprTk come simbolo. Le parentesi proteggono le espressioni composte
    // (es. "E-1" con E negativo non diventa "--1").
    QString expr = m_meshVisibleExpr;
    const struct { const char* name; float val; } consts[] = {
        {"A", k.a}, {"B", k.b}, {"C", k.c}, {"D", k.d},
        {"E", k.e}, {"F", k.f}, {"S", k.s},
    };
    for (const auto& c : consts) {
        const QRegularExpression re(QString("\\b%1\\b").arg(c.name),
                                    QRegularExpression::CaseInsensitiveOption);
        expr.replace(re, QString("(%1)").arg(c.val, 0, 'g', 9));
    }

    // E NEMMENO passarla a parseUIConstant per la valutazione: quella funzione
    // comincia con replace(",", "."), una comodita' per chi scrive i decimali
    // all'italiana in un CAMPO (dove c'e' un numero solo e non ci sono virgole
    // separatrici). Qui invece le virgole separano gli ARGOMENTI, e quel replace
    // trasforma min(max(E,1),6) in min(max(E.1).6): non compila, ok = false, e
    // il chiamante leggeva "nessuna direttiva" tornando al conteggio dichiarato.
    // E' il motivo per cui Clifford Labyrinth arrivava a 6 con E = 3, mentre i
    // tori di Hopf funzionavano: "E" da sola non ha virgole.
    // Stessa trappola della memoria sui campi dei path (mod/atan2/min/max rotte
    // dal replace virgola->punto). Percio' qui ExprTk lo usiamo direttamente.
    exprtk::symbol_table<double> st;
    st.add_constants();          // pi, epsilon, inf: nessuna lettera A..F/S,
                                 // che a questo punto sono gia' numeri.
    exprtk::expression<double> compiled;
    compiled.register_symbol_table(st);
    exprtk::parser<double> parser;
    if (!parser.compile(expr.toStdString(), compiled)) return 0;

    // Arrotondamento allo stesso modo dello shader dei tori di Hopf
    // (floor(x + 0.5)), cosi' UI e superficie contano le mesh nello stesso modo:
    // e' la stessa cautela della memoria sulle costanti discrete, dove lo script
    // e lo slider dovevano arrotondare uguale.
    const int nVis = int(std::floor(float(compiled.value()) + 0.5f));
    return nVis > 0 ? nVis : 0;
}

// Riallinea il range dello spinbox al numero di parti della superficie corrente.
// Va chiamata dopo ogni rigenerazione della mesh: con una superficie a mesh
// singola il massimo resta 1, quindi lo spinbox e' inerte e la funzionalita' e'
// invisibile: nulla cambia per i preset che non usano il multi-mesh.
// Il selettore All/Mesh ha senso solo dove esistono davvero piu' fasce E i
// comandi agiscono sulla superficie. Punto UNICO, perche' i due casi che lo
// disabilitano si erano gia' dimostrati facili da dimenticare:
//  - SUPERFICIE A MESH SINGOLA. I preset non salvano "meshScopeAll", quindi
//    applyPendingMeshScope apriva in "Mesh" anche una superficie normale: con
//    la parte 0 attiva ogni comando veniva dirottato su di essa e sembrava che
//    "non funzionasse niente" finche' non si sceglieva All a mano.
//  - BACKGROUND. Li' si edita lo sfondo, che di fasce non ne ha: i controlli si
//    spengono, ma la selezione NON si tocca -- uscendo si deve ritrovare
//    l'ambito con cui si stava lavorando.
bool MainWindow::meshScopeUsable() const
{
    if (!ui->glWidget) return false;
    if (editingBackground()) return false;
    // Scena vuota (tasto NEW): non ci sono parti da selezionare. Il controllo
    // sta QUI e non solo in applyEmptySceneGating perche' updateMeshSelectorRange
    // riabilita i radio All/Mesh per conto suo (~13813) sull'onda di
    // meshPartsChanged: gaterebbe il gate.
    if (isSceneEmpty()) return false;
    return ui->glWidget->meshPartCount() > 1;
}

// Abilita/disabilita il gruppo All/Mesh.
// L'ambito viene riportato ad "All" SOLO quando le fasce non esistono (mesh
// singola): li' "Mesh" non ha significato e lasciare la parte 0 attiva
// dirottava ogni comando su di essa.
// In BACKGROUND invece si spengono solo i controlli: la selezione resta com'e'
// e si ritrova uscendo. Lo sfondo ha i propri stati (bg_*) e non passa dai
// setter per-mesh, quindi non c'e' nulla da proteggere azzerandola.
void MainWindow::updateMeshScopeEnabled()
{
    if (!ui->radioMeshAll || !ui->radioMeshOne || !ui->spinMeshSel || !ui->glWidget) return;
    updateMeshCountControls();

    const bool usable = meshScopeUsable();

    ui->radioMeshAll->setEnabled(usable);
    ui->radioMeshOne->setEnabled(usable);
    ui->spinMeshSel->setEnabled(usable && !meshScopeAll());

    // LIMITI PER-MESH: attivi in ENTRAMBI gli ambiti su una superficie
    // multi-mesh. In "Mesh" tagliano la parte scelta, in "All" tagliano tutte
    // le mesh insieme sospendendo -- senza cancellarli -- i tagli per-parte,
    // esattamente come colore, trasparenza e texture si comportano nei due
    // ambiti. Il dominio di "All" e' un livello suo (SurfaceEngine::setAllDomain),
    // indipendente dai limiti del dock Equations: quelli valgono per le
    // superfici a mesh singola e vengono riscritti dalle direttive "u_min :="
    // a ogni Run, quindi non potevano reggere un taglio dell'utente.
    const bool meshLimitsUsable = usable;
    for (QLineEdit* e : { ui->meshUMinEdit, ui->meshUMaxEdit,
                          ui->meshVMinEdit, ui->meshVMaxEdit }) {
        if (!e) continue;
        e->setEnabled(meshLimitsUsable);
        if (!meshLimitsUsable) {
            QSignalBlocker b(e);
            e->clear();
        }
    }

    if (usable) return;

    // BACKGROUND: i controlli sono gia' spenti qui sopra e basta cosi'. La
    // selezione va LASCIATA com'era, per ritrovarla uscendo.
    if (editingBackground()) return;

    // MESH SINGOLA: qui "Mesh" non ha significato, quindi si torna ad "All".
    // I valori per-parte NON si toccano (restano in MeshPart e ricompaiono se
    // la superficie torna multi-mesh): cambia solo il destinatario dei comandi.
    if (!meshScopeAll()) setMeshScopeAll(true);
    ui->glWidget->setMeshAppearanceUniform(true);
    ui->glWidget->setActiveMeshPart(-1);
}

void MainWindow::updateMeshSelectorRange()
{
    if (!ui->spinMeshSel || !ui->glWidget) return;

    int n = ui->glWidget->meshPartCount();
    // MESH VISIBILI: un //CUTOUT puo' spegnere le mesh oltre un certo indice
    // (tori di Hopf: lo slider E ne accende da 1 a 9 delle 9 dichiarate). Quelle
    // spente esistono come geometria ma non si vedono, quindi selezionarle
    // significava muovere slider che non cambiavano nulla a schermo.
    // Il numero vivo lo DICHIARA lo script con "MESH_VISIBLE := E;": non e'
    // deducibile qui, perche' il cutout e' GLSL eseguito sulla GPU e rifarne il
    // conto in C++ sarebbe la solita logica duplicata che diverge.
    // Senza direttiva nVis = 0 e vale il conteggio dichiarato, come da sempre.
    const int nVis = meshVisibleCount();
    if (nVis > 0) n = std::min(n, nVis);

    // Lo spinbox parte da 1 e sceglie SOLO quale mesh: "All" e' ora un radio a
    // parte, non piu' il valore 0 (specialValueText). Con una parte sola resta
    // 1..1: inerte ma coerente, e non serve piu' il caso speciale maxSel = 0.
    const int maxSel = std::max(1, n);

    // Il valore da RIPRISTINARE non e' quello dello spinbox ma quello del
    // widget: setMaximum() clampa il valore da solo, e lo fa a segnali
    // bloccati, quindi una rigenerazione transitoria con una parte sola
    // azzerava lo spinbox SENZA che il gestore girasse mai.
    // m_activeMeshPart restava all'indice vecchio: da li' in poi il numero
    // mostrato e la mesh realmente selezionata erano due cose diverse, e
    // tornare sul numero visualizzato non emetteva valueChanged perche' lo
    // spinbox ci era gia'. E' il "dopo qualche giro il campo si blocca".
    // In "All" la parte attiva e' -1: li' il numero mostrato non va cambiato,
    // resta quello a cui si tornera' passando a Mesh.
    const int active = ui->glWidget->activeMeshPart();
    int wanted = (active >= 0) ? active + 1 : ui->spinMeshSel->value();

    bool old = ui->spinMeshSel->blockSignals(true);
    ui->spinMeshSel->setMaximum(maxSel);
    ui->spinMeshSel->setValue(qBound(1, wanted, maxSel));
    ui->spinMeshSel->blockSignals(old);

    // AMBITO "ALL": e' il radio a decidere, non il numero. La parte attiva resta
    // -1 (i comandi vanno sul globale) e lo spinbox e' disabilitato ma conserva
    // il suo valore, cosi' tornando su Mesh si ritrova la stessa selezione.
    if (meshScopeAll()) {
        ui->glWidget->setActiveMeshPart(-1);
        ui->spinMeshSel->setEnabled(false);
        ui->radioMeshAll->setEnabled(true);
        if (ui->radioMeshOne) ui->radioMeshOne->setEnabled(true);
        // In "All" non c'e' nessuna mesh da mostrare negli slider, ma il flag va
        // consumato lo stesso: restando alzato forzerebbe un sync spurio al
        // primo giro utile (una rigenerazione qualunque), riallineando gli
        // slider sotto le dita dell'utente.
        m_meshScopeJustApplied = false;
        updateMeshScopeEnabled();
        return;
    }
    ui->spinMeshSel->setEnabled(true);

    // Riallinea SEMPRE il widget al valore che lo spinbox ha davvero adesso
    // (che puo' essere stato clampato qui sopra). Cosi' i due non possono
    // divergere, qualunque cosa abbia fatto setMaximum.
    const int now = ui->spinMeshSel->value() - 1;
    const bool moved = (now != ui->glWidget->activeMeshPart());
    ui->glWidget->setActiveMeshPart(now);

    // Gli slider si riallineano solo se la selezione e' davvero cambiata: qui
    // si passa a ogni rigenerazione della griglia, e risincronizzarli sempre
    // li farebbe saltare sotto le dita mentre si edita una mesh.
    // updateRenderState va chiamata DOPO il sync (che muove i radio a segnali
    // bloccati, quindi non la fa girare da solo): e' lei ad abilitare i tasti
    // densita' U/V in base ai radio appena aggiornati.
    //
    // ECCEZIONE: subito dopo un LOAD. applyPendingMeshScope gira sullo stesso
    // meshPartsChanged, appena prima di questa funzione, e porta gia' la parte
    // attiva all'indice del preset; "moved" risulta quindi falso (spinbox 1 ->
    // indice 0, dove la parte attiva gia' e') e il sync veniva saltato. Gli
    // slider restavano sul colore GLOBALE mentre la mesh 1 disegnava il proprio:
    // il disallineamento visibile GIA' al primo caricamento, senza cambio di
    // preset. Il flag vale un giro solo, quindi non reintroduce il salto degli
    // slider durante l'editing.
    const bool justLoaded = m_meshScopeJustApplied;
    m_meshScopeJustApplied = false;
    if (moved || justLoaded) {
        syncAppearanceControlsToActiveMesh();
        updateRenderState();
    }

    // GATING del gruppo All/Mesh (mesh singola / Background -> disabilitato).
    // Storicamente qui i radio si riabilitavano SEMPRE, perche' disabilitarli
    // "in base al numero di parti" li aveva resi irrecuperabili: bastava una
    // rigenerazione transitoria con una parte sola (cambio tab, un Run fallito
    // prima di ri-estrarre le sezioni) e restavano grigi per sempre.
    // Ora il gating e' recuperabile perche' NON e' one-shot: questa funzione
    // gira a ogni meshPartsChanged, quindi appena le parti tornano il gruppo si
    // riaccende da solo. La condizione vive in meshScopeUsable(), punto unico.
    updateMeshScopeEnabled();
}

// Porta gli slider (colore, trasparenza, Light) sui valori della parte
// selezionata, cosi' mostrano cio' che stanno per modificare. Una parte che
// eredita (valori negativi) mostra lo stato globale.
// Riversa sulle parti l'aspetto letto dal preset. Va chiamata DOPO che la
// griglia e' stata generata: prima le parti non esistono. Se il preset non
// portava nulla la lista e' vuota e non tocca niente, quindi le parti restano
// a "eredita dal globale" come da sempre.
// AMBITO All/Mesh: si ripristina quello con cui il preset e' stato SALVATO.
// Forzare sempre "Mesh" era sbagliato: una superficie messa tutta in wireframe
// da "All" salva renderMode = 2 (globale) e NESSUNA modalita' propria nelle
// parti; riaprendola in "Mesh" quelle parti EREDITAVANO il wireframe globale e
// si vedevano tutte wireframe, ma in un ambito che l'utente non aveva scelto.
// Preset vecchi (nessuna chiave "meshScopeAll"): si apre in "Mesh", com'e'
// sempre stato.
void MainWindow::applyPendingMeshScope()
{
    if (!ui->glWidget || !ui->radioMeshAll || !ui->radioMeshOne) return;

    // "PENDING" davvero: si applica UNA volta sola, al primo giro dopo il load.
    // Il chiamante e' agganciato a meshPartsChanged, che scatta a OGNI
    // rigenerazione della griglia (ogni cambio di costante): senza questo
    // consumo, ogni movimento di uno slider riportava l'ambito allo stato del
    // preset e riscriveva la parte attiva, annullando la scelta dell'utente.
    if (!m_meshScopePending) return;

    // NON si consuma su una griglia che non e' ancora quella del preset.
    // meshPartsChanged scatta anche PRIMA che le parti nuove esistano: durante
    // il load arriva un giro con la superficie precedente ancora nel motore
    // (l'unica parte sintetizzata da resolveMeshParts, perche' clearMeshParts ha
    // gia' svuotato le dichiarate). Consumando li', il flag sarebbe gia' false
    // al giro BUONO e setActiveMeshPart non verrebbe mai chiamato con l'indice
    // del preset. Se il preset non porta aspetto per-mesh (m_pendingMeshParts
    // vuoto) non c'e' nulla da attendere e vale il primo giro, come da sempre.
    // Si aspetta il numero di mesh ATTESO dal preset (m_lastAppliedMeshCount,
    // dal campo Meshes o dalle sezioni), non la lunghezza della lista: con
    // Meshes piu' basso della lista (aspetti in riserva) le parti non la
    // raggiungono mai e l'ambito restava quello del preset precedente.
    if (!m_pendingMeshParts.empty()
        && ui->glWidget->getEngine()
        && ui->glWidget->getEngine()->getMeshPartCount()
               < std::min((int)m_pendingMeshParts.size(), m_lastAppliedMeshCount))
        return;

    m_meshScopePending = false;

    // Superficie a mesh SINGOLA (o Background attivo): l'ambito "Mesh" non ha
    // senso. I preset non salvano "meshScopeAll", quindi senza questa riga una
    // superficie normale si apriva in "Mesh" con la parte 0 attiva, e ogni
    // comando finiva dirottato su di essa: sembrava che "non funzionasse
    // niente" finche' non si sceglieva All a mano.
    const bool wantAll = m_pendingMeshScopeAll || !meshScopeUsable();
    setMeshScopeAll(wantAll);
    ui->glWidget->setMeshAppearanceUniform(wantAll);
    ui->glWidget->setActiveMeshPart(wantAll ? -1 : ui->spinMeshSel->value() - 1);
    ui->spinMeshSel->setEnabled(!wantAll);

    // Il sync degli slider tocca a updateMeshSelectorRange, che gira subito dopo
    // sullo stesso meshPartsChanged. Li' pero' il sync e' condizionato a "moved"
    // (la selezione e' cambiata), e la riga qui sopra ha GIA' portato la parte
    // attiva al valore che quella funzione calcolera': moved risulta falso e gli
    // slider non vengono mai riallineati alla mesh del preset.
    m_meshScopeJustApplied = true;
}

void MainWindow::applyPendingMeshAppearance()
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return;

    // L'AMBITO va ripristinato anche quando il preset NON porta aspetto
    // per-mesh: una superficie salvata in "All" (tutta wireframe dal globale,
    // nessun valore proprio nelle parti) non scrive la chiave "meshParts", e
    // con l'early-return sotto sarebbe riaperta in "Mesh". Percio' sta qui,
    // prima del return.
    applyPendingMeshScope();

    if (m_pendingMeshParts.empty()) return;

    SurfaceEngine *eng = ui->glWidget->getEngine();
    // Gli aspetti oltre il numero di mesh a schermo (campo Meshes piu' basso
    // della lista salvata) restano in riserva: tornano alzando il numero.
    eng->rememberPartAppearance(m_pendingMeshParts);
    const int n = std::min((int)m_pendingMeshParts.size(), eng->getMeshPartCount());
    bool anyTexture = false;
    for (int k = 0; k < n; ++k) {
        MeshPart *dst = eng->mutableMeshPart(k);
        if (!dst) continue;
        const MeshPart &src = m_pendingMeshParts[k];
        dst->colorR = src.colorR;
        dst->colorG = src.colorG;
        dst->colorB = src.colorB;
        dst->alpha = src.alpha;
        dst->lightIntensity = src.lightIntensity;
        dst->renderMode = src.renderMode;
        dst->hasCustomRenderMode = src.hasCustomRenderMode;
        dst->wfStepU = src.wfStepU;
        dst->wfStepV = src.wfStepV;
        dst->textureCode = src.textureCode;
        dst->textureLibName = src.textureLibName;
        dst->textureEnabled = src.textureEnabled;
        dst->hasCustomTexture = src.hasCustomTexture;
        dst->texCol1R = src.texCol1R;
        dst->texCol1G = src.texCol1G;
        dst->texCol1B = src.texCol1B;
        dst->texCol2R = src.texCol2R;
        dst->texCol2G = src.texCol2G;
        dst->texCol2B = src.texCol2B;
        dst->texZoom = src.texZoom;
        dst->texPanX = src.texPanX;
        dst->texPanY = src.texPanY;
        dst->texRotation = src.texRotation;
        // OROLOGIO DELLA PARTE: **derivato dal codice**, non letto dal file.
        // texAnimating e' stato di RUNTIME e non viene serializzato (come il
        // clock globale, che il load ricalcola da hasTimeVariable): restando al
        // default false, ogni fascia texturizzata nasceva FERMA al caricamento
        // di un record. Derivarlo invece di salvarlo fa funzionare anche i
        // record salvati prima di questa feature.
        dst->texAnimating = dst->hasCustomTexture && dst->textureEnabled
                            && !dst->textureCode.isEmpty()
                            && hasTimeVariable(dst->textureCode);
        dst->timeTex = 0.0f;   // il record riparte dall'inizio
        if (src.hasCustomTexture) anyTexture = true;
    }
    eng->syncPartAppearance();
    m_pendingMeshParts.clear();

    // Le texture per-mesh vivono DENTRO il fragment shader (getCustomColor_<k>):
    // caricarle e' un cambio di CODICE, non di uniform, quindi senza ricompilare
    // resterebbero quelle della superficie precedente. Si ricompila solo se il
    // preset ne porta davvero almeno una: le superfici che non usano la feature
    // non pagano nulla.
    if (anyTexture) ui->glWidget->rebuildShader();

    // Le densita' wireframe per-parte appena caricate cambiano la GEOMETRIA
    // delle linee, non solo un uniform: senza ricostruzione si vedrebbero
    // ancora quelle della superficie precedente.
    ui->glWidget->rebuildWireframeGeometry();
    ui->glWidget->update();
}

// BASE / PHONG / WIREFRAME: lo stato (m_scene.renderMode, o la MeshPart della
// fascia selezionata) e la sua vista. Prima i radio facevano anche da stato: li
// leggevano colori, texture e motore, e tre punti li accendevano a segnali vivi
// (il reset e il ritorno da Wireframe in Ray Marching), facendo girare il
// gestore del clic.
int MainWindow::shownRenderMode() const
{
    // Col bersaglio Border i radio mostrano la modalita' del bordo.
    if (editingBorder() && ui->glWidget && !implicitMode())
        return ui->glWidget->borderRenderMode();
    if (ui->glWidget && !implicitMode()
        && ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1)
        return ui->glWidget->activeMeshEffectiveRenderMode();
    return m_scene.renderMode;
}

// DISPLAY: in un QButtonGroup esclusivo setChecked(true) ne deseleziona un
// altro, che emette toggled(false): vanno bloccati i segnali di TUTTI i
// bottoni del gruppo, non solo di quello che si accende. Anche in Ray
// Marching, dove mostrano Base o Phong (m_scene.renderMode) e il Wireframe e'
// spento.
void MainWindow::refreshRenderRadios()
{
    if (!ui->radioBasic || !ui->radioPhong || !ui->radioWF) return;
    const int mode = shownRenderMode();

    QRadioButton *target = (mode == 2) ? ui->radioWF
                         : (mode == 1) ? ui->radioPhong
                                       : ui->radioBasic;
    if (!target || target->isChecked()) return;

    const bool b0 = ui->radioBasic->blockSignals(true);
    const bool b1 = ui->radioPhong->blockSignals(true);
    const bool b2 = ui->radioWF->blockSignals(true);
    target->setChecked(true);
    ui->radioBasic->blockSignals(b0);
    ui->radioPhong->blockSignals(b1);
    ui->radioWF->blockSignals(b2);
}


void MainWindow::syncAppearanceControlsToActiveMesh()
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return;

    // Guardia di rientranza: questa funzione muove slider e radio, e quei
    // widget possono a loro volta far ripartire il giro (updateRenderState ->
    // setGlobalRenderMode -> ...). Senza la guardia un rientro riscriverebbe lo stato
    // mentre lo stiamo leggendo, e il selettore smetterebbe di rispondere.
    if (m_syncingMeshControls) return;
    m_syncingMeshControls = true;
    struct Guard { bool &f; ~Guard(){ f = false; } } guard{m_syncingMeshControls};

    auto setNoSignal = [](QSlider *s, int v) {
        if (!s) return;
        bool old = s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(old);
    };

    // DISPLAY di colore / trasparenza / luce. Punto UNICO per i due ambiti: i
    // valori da mostrare cambiano (globali in "All", della parte in "Mesh"), il
    // modo di mostrarli no. Tenerlo unico e' cio' che impedisce ai due rami di
    // divergere, che e' esattamente com'era nato questo bug.
    // In tutti i casi gli slider si muovono a SEGNALI BLOCCATI, quindi le
    // etichette numeriche vanno scritte a mano: l'unico che le aggiorna e'
    // handleColorChange / i gestori valueChanged, che qui non scattano.
    auto showAppearance = [&](float fr, float fg, float fb, float fa, float fl) {
        const int r = qRound(fr * 255.0f);
        const int g = qRound(fg * 255.0f);
        const int b = qRound(fb * 255.0f);
        setNoSignal(ui->sliderR, r);
        setNoSignal(ui->sliderG, g);
        setNoSignal(ui->sliderB, b);
        ui->valR->setNum(r);
        ui->valG->setNum(g);
        ui->valB->setNum(b);

        if (fa >= 0.0f) {
            setNoSignal(ui->alphaSlider, qRound(fa * 100.0f));
            ui->lblAlphaVal->setText(QString::number(fa, 'f', 2));
        }
        if (fl >= 0.0f) {
            setNoSignal(ui->lightSlider, qRound(fl * 100.0f));
            ui->lblValLight->setText(QString::number(qRound(fl * 100.0f)) + " %");
        }
        // NB: NON si tocca il colore GLOBALE del motore, che e' quello SALVATO
        // nel preset (presetserializer, chiavi r/g/b e surfColor): scriverci il
        // colore della mesh selezionata significherebbe che basta guardare la
        // mesh 3 e salvare per portarsi via il suo rosso come colore globale
        // della superficie. Qui si muove solo la VISTA.
    };

    const int idx = ui->glWidget->activeMeshPart();
    const auto &parts = ui->glWidget->getEngine()->getMeshParts();
    if (idx < 0 || idx >= (int)parts.size()) {
        // AMBITO "ALL": i comandi agiscono sullo stato GLOBALE, quindi i controlli
        // devono mostrare QUELLO.
        // Prima qui gli slider "restavano dove sono" e si riallineavano solo i
        // radio: tornando da "Mesh" ad "All", colore, trasparenza e luce
        // continuavano a mostrare i valori dell'ultima mesh guardata, mentre
        // muoverli agiva sul globale. Il primo tocco faceva quindi saltare il
        // globale al valore della mesh precedente.
        // I RADIO restano indispensabili per un motivo in piu' (vedi 685100e):
        // uscendo da "Mesh" la guardia showingPart di updateRenderState smette di
        // valere, e quella funzione rilegge i radio come fossero il globale; se
        // restassero sul display della mesh precedente, il globale verrebbe
        // sovrascritto con la modalita' di quella mesh.
        float gr, gg, gb;
        ui->glWidget->globalColor(gr, gg, gb);
        showAppearance(gr, gg, gb,
                       ui->glWidget->globalAlpha(),
                       ui->glWidget->globalLightIntensity());
        refreshRenderRadios();

        // EDITOR: in "All" si comanda la texture GLOBALE, quindi va mostrato il
        // suo script. Senza questo l'editor restava sul codice dell'ultima mesh
        // guardata mentre l'ambito diceva "All": premere Run avrebbe applicato
        // alla superficie intera uno script che apparteneva a una fascia.
        // (shownScriptSlot: in "All" lo slot mostrato e' quello della
        // superficie, e solo se il dock e' sul modulo Texture.)
        refreshScriptEditor();

        // FOCUS DEL DOCK LIBRARY, come nel ramo "Mesh": tornando su "All"
        // l'albero deve evidenziare la texture di SUPERFICIE. L'aggancio stava
        // solo nel ramo per-mesh, quindi passando ad "All" il focus restava
        // sulla texture dell'ultima fascia guardata -- o si perdeva del tutto,
        // perche' syncTextureTreeSelection deseleziona quando non trova
        // corrispondenza.
        if (!editingBackground())
            syncTextureTreeSelection();

        // I colori della texture nei picker non vanno riallineati: li legge
        // surfaceTexColor dal motore, secondo l'ambito corrente.

        // TASTI Run/Stop DEL DOCK SCRIPT, come in fondo al ramo "Mesh": il tasto
        // texture descrive l'ambito corrente, quindi tornando su "All" va
        // ricalcolato sulla texture di SUPERFICIE. Questo ramo esce con return e
        // la chiamata mancava: i tasti restavano fermi all'ultimo stato
        // calcolato per la MESH -- il tasto di "All" diceva "Stop" (stato della
        // fascia in movimento) anche con la texture di superficie ferma.
        updateScriptButtonText();

        // SLIDER R/G/B SUL TARGET GIUSTO, come in fondo al ramo "Mesh": anche qui
        // showAppearance() ha appena scritto negli slider il colore SOLIDO, e con
        // una texture colorata attiva devono invece mostrare u_col1/u_col2.
        // Questo ramo esce con return, quindi la chiamata va ripetuta: senza,
        // bastava passare da "All" per perdere l'allineamento.
        onColorTargetChanged();
        return;
    }

    const MeshPart &p = parts[idx];

    // EDITOR DELLA TEXTURE PER-MESH. Selezionando una parte, l'editor mostra il
    // SUO script: e' lo stesso principio dei radio e degli slider, cioe' i
    // controlli mostrano cio' che stanno per modificare.
    // Solo se il dock e' sul modulo Texture col bersaglio Surface
    // (shownScriptSlot): sul ramo Background o sugli altri moduli l'editor
    // mostra un altro slot.
    // Una parte SENZA texture propria SVUOTA l'editor. Prima lo si lasciava
    // fermo, per non "cancellare la texture della superficie" con un Run: ma
    // quel ragionamento valeva quando una fascia non configurata EREDITAVA la
    // texture globale. Ora non la eredita piu' (vedi effectiveTextureEnabledMulti),
    // quindi lasciare in vista lo script di un'altra fascia e' solo un
    // disallineamento: l'editor mostrava una texture che quella fascia non ha,
    // e un Run la applicava alla fascia sbagliata.
    // Si guarda la texture EFFICACE (propria E accesa), non il solo
    // hasCustomTexture: in wireframe la texture viene SPENTA conservando lo
    // script, e mostrarlo lo stesso rimetterebbe in vista una texture che quella
    // mesh non sta disegnando -- col Run che la riapplicherebbe di soppiatto.
    // Lo script non e' perso: riaccendendo il checkbox torna, editor compreso.
    // Lo slot della fascia si ricarica da solo quando cambia la fascia o la sua
    // texture (syncMeshTextureSlot); qui si riallinea la vista.
    refreshScriptEditor();

    // FOCUS DEL DOCK LIBRARY (ramo Texture) SULLA FASCIA SELEZIONATA. Stessa
    // ragione dell'editor qui sopra: i controlli devono indicare cio' su cui si
    // sta lavorando. syncTextureTreeSelection aveva come soli chiamanti i cambi
    // di modalita' (Base/Phong/WF e Surface/Background), quindi al cambio di
    // mesh l'albero restava fermo sulla texture della fascia precedente -- o su
    // quella globale. Non e' condizionata a ScriptModeTexture: l'albero e' un
    // pannello a se', visibile qualunque cosa mostri l'editor.
    if (!editingBackground())
        syncTextureTreeSelection();

    // COLORI u_col1/u_col2 DELLA PARTE nei picker: nessun riallineamento, li
    // legge surfaceTexColor (i propri della parte, o i globali che eredita).
    // Prima erano copie aggiornate qui solo se la parte ne aveva di propri: su
    // una fascia che li eredita restavano quelli della fascia guardata prima.

    // DISPLAY del checkbox Texture: mostra lo stato EFFICACE della parte (il
    // proprio se dichiarato, altrimenti il globale che sta ereditando), come i
    // radio Base/Phong/Wireframe. A segnali bloccati, o il toggled riscriverebbe
    // sulla parte cio' che stiamo solo mostrando.
    if (!editingBackground() && !implicitMode()) {
        // In MULTI-mesh una parte mai configurata NON eredita la texture
        // globale (resta in tinta unita): il display deve dire la stessa cosa
        // del render, o il checkbox risulterebbe acceso su una fascia che si
        // disegna senza texture. Con una mesh sola vale la regola di sempre.
        // La regola sta in refreshTextureCheckbox (unico punto della vista).
        const bool multi = ui->glWidget->meshPartCount() > 1;
        const bool eff = multi ? p.effectiveTextureEnabledMulti()
                               : p.effectiveTextureEnabled(ui->glWidget->isTextureEnabled());
        refreshTextureCheckbox();

        // PICKER Color1/Color2 RIALLINEATI ALLA PARTE. Il checkbox qui sopra
        // mostra gia' lo stato giusto, ma i due picker non venivano rivalutati
        // da nessuno al cambio di mesh (questa funzione non chiamava
        // updateTextureUIState): restavano accesi con i permessi della mesh
        // PRECEDENTE, cioe' offrivano di editare i colori di una texture che
        // sulla mesh selezionata non esiste.
        // Il flag "reset a Color 1" e' false: qui si sta solo CAMBIANDO
        // selezione, non applicando una texture nuova, quindi la scelta
        // Color1/Color2 dell'utente non va spostata sotto le dita.
        // Passa da updateTextureUIState (punto unico) invece di accendere i
        // due radio a mano: e' la stessa funzione che gia' decide il gating in
        // tutti gli altri contesti, e duplicarne la logica qui la farebbe
        // divergere -- che e' esattamente come sono nati gli altri bug di
        // questa famiglia.
        updateTextureUIState(eff, false);
    }

    // AMBITO "MESH": si mostra il valore PROPRIO della parte se c'e', altrimenti
    // quello globale, che e' cio' che la parte sta ereditando (stessa regola dei
    // radio con effectiveRenderMode). Senza il ramo "eredita", selezionando una
    // mesh senza valori propri i controlli restavano su quelli della mesh
    // precedente, mostrando un aspetto che quella mesh non ha.
    float fr = p.colorR, fg = p.colorG, fb = p.colorB;
    if (!p.hasCustomColor()) ui->glWidget->globalColor(fr, fg, fb);
    const float fa = (p.alpha >= 0.0f) ? p.alpha : ui->glWidget->globalAlpha();
    // LUCE: sempre il valore GLOBALE, anche in ambito "Mesh". Lo slider agisce
    // sull'illuminazione della scena (come la speculare), non sulla fascia:
    // il display deve dire cio' che lo slider scrivera'. Mostrare qui il
    // lightIntensity della parte -- che i preset vecchi possono ancora avere --
    // farebbe saltare lo slider al cambio di mesh su un valore che poi non e'
    // quello che si va a modificare.
    const float fl = ui->glWidget->globalLightIntensity();
    showAppearance(fr, fg, fb, fa, fl);

    // LIMITI u/v DELLA PARTE nei quattro campi del pannello Multi Mesh. Stessa
    // regola di editor, slider e radio qui sopra: i controlli mostrano cio' che
    // stanno per modificare. Il ramo "All" non ha bisogno del gemello -- li'
    // updateMeshScopeEnabled li svuota e li spegne, perche' non esiste una
    // parte a cui riferirli.
    syncMeshLimitFields();

    // DISPLAY della modalita' di rendering: i radio mostrano quella EFFICACE
    // della parte (la propria se dichiarata, altrimenti la globale). Siamo
    // dentro la guardia m_syncingMeshControls, quindi il gestore dei radio
    // riconosce questi setChecked come sincronizzazione e NON li riscrive sulla
    // parte. In un QButtonGroup esclusivo setChecked(true) ne deseleziona un
    // altro, che emette toggled(false): vanno bloccati i segnali di TUTTI i
    // bottoni del gruppo, non solo di quello che si accende.
    // In Ray Marching i radio classici non governano nulla: si lascia stare.
    refreshRenderRadios();

    // La densita' wireframe non ha widget di stato da riallineare: i tasti +/-
    // sono incrementali e leggono il valore corrente della parte selezionata
    // (vedi i loro gestori, che passano da GLWidget::*WireframeUDensity).

    // TASTI Run/Stop DEL DOCK SCRIPT. Stessa ragione di editor, slider, radio e
    // picker qui sopra: in ambito "Mesh" il tasto texture parla della PARTE
    // selezionata (vedi updateScriptButtonText), quindi cambiare mesh ne cambia
    // lo stato. Nessuno lo ricalcolava al cambio di selezione: passando su una
    // fascia in wireframe i tasti si spegnevano -- corretto per QUELLA fascia --
    // e tornando su quella texturizzata restavano spenti, perche' erano fermi
    // all'ultimo stato calcolato. Si "riparavano" solo al primo evento che
    // ricalcola i tasti (applicare un'altra texture, o digitare un carattere:
    // isModified). E' lo stesso difetto gia' corretto poco sopra per i picker
    // Color1/Color2, che al cambio di mesh restavano coi permessi della mesh
    // precedente.
    updateScriptButtonText();

    // SLIDER R/G/B SUL TARGET GIUSTO. **ULTIMA COSA**, e l'ordine e' il punto:
    // showAppearance() qui sopra scrive negli slider il colore della SUPERFICIE
    // (o della parte), sovrascrivendo l'allineamento ai colori della TEXTURE che
    // updateTextureUIState aveva gia' fatto poco prima. Risultato: cambiando mesh
    // (o passando per "All") e tornando indietro, gli slider mostravano il colore
    // solido invece dei col1/col2 della texture.
    // onColorTargetChanged e' il punto unico che sceglie COSA gli slider devono
    // mostrare (colore superficie, u_col1/u_col2, o nulla in wireframe): chiamarla
    // per ultima fa vincere quella decisione su tutto il resto.
    onColorTargetChanged();
}

// L'utente ha cliccato un radio Base/Phong/Wireframe (non e' una
// sincronizzazione). Se una mesh e' selezionata la scelta riguarda SOLO quella;
// con "All" e' la modalita' globale, come da sempre.
void MainWindow::onUserRenderModeChosen()
{
    if (!ui->glWidget) return;
    if (implicitMode()) return;   // Ray Marching: non si applica

    const int mode = ui->radioWF->isChecked()    ? 2
                   : ui->radioPhong->isChecked() ? 1
                                                 : 0;

    // Bersaglio Border: la modalita' e' del bordo, e solo sua.
    if (editingBorder()) {
        ui->glWidget->setBorderRenderMode(mode);
        return;
    }

    const bool editingSingleMesh =
        (ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1);

    if (editingSingleMesh) {
        // Scrive la modalita' PROPRIA della parte. m_scene.renderMode (lo stato
        // globale, quello che il preset salva e che le altre mesh ereditano)
        // resta invariato: e' la ragione per cui ricaricare il preset non
        // propaga piu' il wireframe a tutte le parti.
        ui->glWidget->setActiveMeshRenderMode(mode);

        // WIREFRAME => TEXTURE SPENTA SU QUESTA MESH. In wireframe la texture
        // non si disegna (il fragment esce prima con colore piatto), quindi
        // lasciarla "accesa" descriveva uno stato che a schermo non esisteva:
        // togliendo il wireframe ricompariva da sola, e per giunta col checkbox
        // deselezionato -- display e realta' che dicevano il contrario.
        // Si SPEGNE conservando lo script (setActiveMeshTextureEnabled):
        // riaccendendo il checkbox si ritrova la propria texture invece di
        // doverla ricaricare. Non si riaccende da se' uscendo dal wireframe:
        // e' l'utente a decidere quando rimetterla.
        if (mode == 2) ui->glWidget->setActiveMeshTextureEnabled(false);

        // DISPLAY RIALLINEATO A OGNI CAMBIO DI MODALITA', non solo entrando in
        // wireframe: uscendone (mode 0/1) la texture resta spenta, e senza
        // questo giro l'editor continuava a mostrare lo script di prima mentre
        // la mesh tornava in tinta unita.
        // syncAppearanceControlsToActiveMesh e' il punto unico del display
        // per-mesh: svuota/riempie l'editor, allinea checkbox, picker e tasti.
        syncAppearanceControlsToActiveMesh();
        // Il clock della parte puo' essersi fermato: il master va rivalutato, o
        // resterebbe su "STOP" per un'animazione che non c'e' piu'.
        updateMasterButtonState();

        // BASE/PHONG VALGONO PER TUTTA LA FIGURA, anche in ambito "Mesh".
        // Non e' un'incoerenza con la riga sopra: sono due cose diverse che
        // condividono gli stessi tre radio.
        //  - WIREFRAME e' una proprieta' della singola griglia: lo shader lo
        //    decide per parte (u_renderMode == 2 sul valore per-parte), quindi
        //    puo' convivere con mesh solide accanto.
        //  - BASE vs PHONG e' solo la SPECULARE, che nell'UBO e' un unico flag
        //    globale (ubuf.useSpecular, da setSpecularEnabled): non esiste "una
        //    mesh in Phong e una in Base", il modello di illuminazione e' uno
        //    per tutta la figura.
        // Prima m_scene.renderMode non veniva toccato qui, e siccome
        // updateRenderState calcola isPhong proprio da lui, cliccare Phong con
        // una mesh selezionata non accendeva nulla: il tasto sembrava morto.
        // Percio' la scelta fra Base e Phong si scrive anche nel globale. Il
        // Wireframe no: quello resta della sola parte, o si perderebbe l'aspetto
        // misto (e cambierebbe il valore salvato nel preset).
        if (mode != 2 && mode != m_scene.renderMode) {
            // Stessa cautela del ramo "All" qui sotto: le parti che EREDITANO
            // seguono il globale, quindi spostarlo le trascina. Il caso vero:
            // globale in wireframe (mesh ereditanti disegnate a fil di ferro),
            // scelgo Phong su UNA mesh -> il globale passa a 1 e tutte le
            // ereditanti uscirebbero dal wireframe, che l'utente non ha chiesto.
            // Congelando prima l'eredita', restano come sono e cambia solo
            // l'illuminazione.
            ui->glWidget->pinInheritedRenderModes();
            m_scene.renderMode = mode;
        }
    } else {
        // AMBITO "ALL": la scelta e' globale, come da sempre. Ma le parti che
        // NON hanno una modalita' propria ereditano dal globale, quindi
        // cambiarlo qui le trascina tutte, e l'aspetto misto impostato per-mesh
        // sparisce appena si torna su "Mesh".
        // Caso segnalato ("Hopf Tori Mesh Colors"): globale = Phong, due mesh
        // con wireframe proprio. Wireframe da "All" -> le altre ereditano il
        // wireframe -> tornando su "Mesh" e' tutto wireframe. I dati per-mesh
        // non erano andati persi: si era spostata la BASE sotto di loro.
        // Percio' PRIMA di muovere il globale si congela nelle parti la
        // modalita' che stavano ereditando: chi non aveva nulla di suo se la
        // prende com'e' (niente cambia a schermo), e resta li' quando il
        // globale si sposta. Cosi' vale anche per il render mode il contratto
        // gia' valido per colore e trasparenza, quello scritto nel tooltip di
        // "All": le impostazioni per-mesh si ritrovano tornando su "Mesh".
        // NB: va fatto solo se il globale cambia davvero. updateRenderState
        // chiama i gestori dei radio anche per ragioni di sola UI, e congelare
        // a vuoto renderebbe "propria" una modalita' che l'utente non ha mai
        // scelto per quelle mesh (smetterebbero di seguire il globale).
        if (mode != m_scene.renderMode)
            ui->glWidget->pinInheritedRenderModes();
        m_scene.renderMode = mode;
    }
}

// =========================================================================
// CAMPO MESHES (pannello Multi Mesh): quante copie della superficie o della
// curva. Vedi la dichiarazione in mainwindow.h.
// =========================================================================

void MainWindow::setMeshCountText(const QString &text)
{
    m_scene.meshCount = text;
    if (ui->lineMeshCount) {
        const QSignalBlocker blocker(ui->lineMeshCount);
        ui->lineMeshCount->setText(text);
    }
    pushMeshCountSlider();
}

int MainWindow::meshCountValue(bool *ok)
{
    const QString text = m_scene.meshCount.trimmed();
    if (text.isEmpty()) {
        if (ok) *ok = true;
        return 1;
    }
    bool valid = false;
    const float value = parseLimitField(text, &valid);
    valid = valid && std::isfinite(value);
    if (ok) *ok = valid;
    if (!valid) return 1;
    return std::clamp((int)std::lround(value), 1, kMeshCountMax);
}

bool MainWindow::meshCountUsesConstants() const
{
    static const QRegularExpression constRe(QStringLiteral("\\b[A-FSs]\\b"));
    return m_scene.meshCount.contains(constRe);
}

// La sorgente distingue le copie? Solo se usa `mesh`: senza, ogni copia e'
// la stessa superficie, le N mesh si sovrappongono e un aspetto diverso per
// mesh si mescola sullo schermo (il wireframe della mesh 2 sopra il solido
// della mesh 1). `meshes` da solo non basta: vale uguale per tutte le copie.
// I commenti non contano (uno script puo' parlare di "mesh" in un commento).
static bool sourceUsesMeshIndex(const QString &source)
{
    static const QRegularExpression commentRe(QStringLiteral(R"(//[^\n]*|/\*.*?\*/)"),
                                              QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression meshRe(QStringLiteral("\\bmesh\\b"));
    QString code = source;
    code.remove(commentRe);
    return code.contains(meshRe);
}

std::vector<MeshPart> MainWindow::countMeshParts(const std::vector<MeshPart> &sections,
                                                 const QString &source)
{
    m_lastSectionCount = (int)sections.size();
    m_lastSourceUsesMesh = sourceUsesMeshIndex(source);
    if (sections.size() > 1) {
        m_lastAppliedMeshCount = (int)sections.size();
        return sections;
    }
    // Il campo resta com'e' scritto (lo stato e' suo, come con piu' sezioni):
    // torna a valere appena la sorgente usa `mesh`.
    const int count = m_lastSourceUsesMesh ? meshCountValue() : 1;
    m_lastAppliedMeshCount = count;
    if (count <= 1) return sections;

    std::vector<MeshPart> parts;
    parts.reserve(count);
    for (int k = 0; k < count; ++k) {
        MeshPart part;
        if (!sections.empty()) {
            part = sections.front();          // la sezione e' il modello
        } else {
            part.followsGlobalDomain = true;  // il dominio dei limiti, sempre attuale
        }
        part.meshIndex = k;
        parts.push_back(part);
    }
    return parts;
}

bool MainWindow::meshCountUsable() const
{
    if (!ui->glWidget || isSceneEmpty() || implicitMode() || editingBackground()) return false;
    // Flusso geodetico e script metrici: la mesh e' gia' pronta, non da parti.
    if (!m_metricScriptBody.trimmed().isEmpty() || ui->glWidget->isCustomMesh()) return false;
    // Piu' sezioni //MESH_BEGIN: le mesh le dichiara lo script. Senza `mesh`
    // nella sorgente le copie sarebbero identiche.
    return m_lastSectionCount <= 1 && m_lastSourceUsesMesh;
}

int MainWindow::shownMeshCount()
{
    if (m_lastSectionCount > 1) return m_lastSectionCount;
    return m_lastSourceUsesMesh ? meshCountValue() : 1;
}

void MainWindow::pushMeshCountSlider()
{
    QSlider *s = ui->meshCountSlider;
    if (!s) return;
    const int n = shownMeshCount();
    // Range come lo spessore dei tubi (pushTubeThickness): il massimo
    // standard, o il valore scritto se e' piu' grande; mentre lo slider si
    // trascina il range non si stringe.
    const QSignalBlocker blocker(s);
    // Il numero lo decidono le costanti (espressione nel campo): lo slider e'
    // spento e non comanda nulla, quindi resta a zero. Mostrare il valore lo
    // lasciava quasi sempre in fondo, perche' il range si allarga fino a lui
    // (Calabi-Yau: floor(A+0.5)^2 = 25 su un massimo di 24).
    if (m_lastSectionCount <= 1 && meshCountUsesConstants()) {
        s->setRange(1, kMeshCountSliderMax);
        s->setValue(1);
        return;
    }
    int hi = std::max(kMeshCountSliderMax, n);
    if (s->isSliderDown()) hi = std::max(hi, s->maximum());
    s->setRange(1, hi);
    s->setValue(n);
}

void MainWindow::commitMeshCountOnEnter()
{
    if (!meshCountUsable()) return;
    // Vuoto: torna a 1, come lo spessore dei tubi vuoto torna al default.
    if (m_scene.meshCount.trimmed().isEmpty()) setMeshCountText(QStringLiteral("1"));
    bool ok = false;
    const int n = meshCountValue(&ok);
    if (!ok) {
        if (m_constantPopupActive) return;
        m_constantPopupActive = true;
        InputValidator::showInvalidMeshCountError(this, m_scene.meshCount);
        {
            const QSignalBlocker blocker(ui->lineMeshCount);
            ui->lineMeshCount->setFocus();
            ui->lineMeshCount->selectAll();
        }
        QTimer::singleShot(0, this, [this] { m_constantPopupActive = false; });
        return;
    }
    // Un numero si riscrive intero e dentro il massimo (2.4 -> 2, 100 -> 64);
    // un'espressione resta com'e' scritta.
    if (!meshCountUsesConstants() && QString::number(n) != m_scene.meshCount.trimmed())
        setMeshCountText(QString::number(n));
    pushMeshCountSlider();
    applyMeshCount();
    updateMeshCountControls();
}

void MainWindow::applyMeshCount()
{
    if (!meshCountUsable()) return;
    if (meshCountValue() == m_lastAppliedMeshCount) return;
    // La scena com'e' a schermo, col numero nuovo: equazioni applicate,
    // script applicato o curva (lo stesso commit dell'Invio su una costante,
    // che non fa ripartire moti e audio).
    runScene(RunOrigin::ServiceCommit);
    // Con uno script immutato il commit non rigenera la griglia: le parti
    // nuove resterebbero dichiarate ma non costruite.
    if (ui->glWidget) ui->glWidget->updateSurfaceData();
    updateMeshCountControls();
}

void MainWindow::refreshMeshCountFromConstants()
{
    if (!meshCountUsesConstants() || !meshCountUsable()) return;
    pushMeshCountSlider();
    if (meshCountValue() != m_lastAppliedMeshCount)
        QTimer::singleShot(0, this, [this] { applyMeshCount(); });
}

void MainWindow::updateMeshCountControls()
{
    if (!ui->lineMeshCount || !ui->meshCountSlider) return;
    const bool usable = meshCountUsable();
    ui->lblMeshCount->setEnabled(usable);
    ui->lineMeshCount->setEnabled(usable);
    // Con un'espressione il numero lo decidono le costanti: lo slider la segue.
    ui->meshCountSlider->setEnabled(usable && !meshCountUsesConstants());
    // Da spento deve dire PERCHE' (regola dei controlli inerti): il campo di
    // testo spento ha lo stesso aspetto di quello acceso.
    QString why;
    if (!ui->glWidget || isSceneEmpty())
        why = tr("Meshes is unavailable: there is no surface on screen.");
    else if (implicitMode())
        why = tr("Meshes is unavailable in ray marching.");
    else if (editingBackground())
        why = tr("Meshes is unavailable while editing the background.");
    else if (!m_metricScriptBody.trimmed().isEmpty() || ui->glWidget->isCustomMesh())
        why = tr("Meshes is unavailable: this surface is built as a single ready-made mesh.");
    else if (m_lastSectionCount > 1)
        why = tr("Meshes is locked: the script declares %1 //MESH_BEGIN sections, one mesh each. "
                 "To change the count, keep a single section and use 'mesh' and 'meshes' in the script.")
                  .arg(m_lastSectionCount);
    else if (!m_lastSourceUsesMesh)
        why = tr("Meshes is unavailable: the surface does not use 'mesh', so every copy would be "
                 "the same surface drawn on top of the others. Use 'mesh' in the equations or "
                 "in the script to tell the copies apart.");
    const QString sliderWhy = (why.isEmpty() && meshCountUsesConstants())
        ? tr("The count follows the expression in the field: change the constants it uses.")
        : why;
    ui->lblMeshCount->setToolTip(why);
    ui->lineMeshCount->setToolTip(why);
    ui->meshCountSlider->setToolTip(sliderWhy);
    // Con piu' sezioni il campo MOSTRA quante ne dichiara lo script, senza
    // `mesh` nella sorgente mostra 1; lo stato (m_scene.meshCount, che il Save
    // scrive) resta il suo.
    const bool countOverridden = m_lastSectionCount > 1 || !m_lastSourceUsesMesh;
    const QString shown = (countOverridden && !implicitMode())
                              ? QString::number(shownMeshCount()) : m_scene.meshCount;
    if (ui->lineMeshCount->text() != shown && !ui->lineMeshCount->hasFocus()) {
        const QSignalBlocker blocker(ui->lineMeshCount);
        ui->lineMeshCount->setText(shown);
    }
    pushMeshCountSlider();
}
