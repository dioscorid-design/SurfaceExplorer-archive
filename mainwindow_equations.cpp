// mainwindow_equations.cpp - MainWindow: equazioni, costanti A..F/S, limiti u/v/w e taglio
// x/y/z, commit dei campi all'Invio, flusso geodetico.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

// Dock Equations: campi, costanti, Steps, limiti. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupEquationsDock()
{
    // =========================================================================
    // 6. EQUATIONS, CONSTANTS & PARAMETERS
    // =========================================================================

    // 1. INIZIALIZZA LA MEMORIA
    m_lastParametricSteps = 100;
    m_lastImplicitSteps = 400;

    // 2. PREPARA L'INTERFACCIA E LO SLIDER AL LORO STATO INIZIALE (Senza lanciare segnali!)
    // Tab Parametric/Implicit e i due sotto-tab (3D/Cross Section,
    // Surface/Tubes) a tutta larghezza: le linguette si dividono la larghezza
    // del tab widget e la seconda finisce sul bordo, cosi' le due colonne sono
    // allineate in tutti e due i tab. Con un valore fisso non si arrivava al
    // bordo (vedi UiStyleManager::fillTabBarWidth).
    UiStyleManager::fillTabBarWidth(ui->tabModeSelector);
    UiStyleManager::fillTabBarWidth(ui->subTabImplicit);
    UiStyleManager::fillTabBarWidth(ui->subTabParametric);
    setImplicitMode(false);
    // Sotto-tab Constraints/Composition/Geodesic Flow (panelImplicit): stessa
    // logica dei Parametric/Implicit, ma sono TRE voci sullo stesso dock da
    // 400px, quindi min-width piu' piccola (~un terzo) per riempire la barra
    // senza sforare e riattivare le frecce di scorrimento. min-width via
    // stylesheet perche' col CSS globale setExpanding e' ignorato.
    if (ui->panelImplicit->tabBar())
        ui->panelImplicit->tabBar()->setUsesScrollButtons(false);
    ui->panelImplicit->setStyleSheet(
        "QTabBar::tab { min-width: 110px; padding: 6px 0px; }");
    // Sotto-tab Surface/Tubes (subTabParametric): come 3D/Cross Section, sta a
    // margini zero nel tab Parametric, cosi' le sue linguette cadono sotto
    // Parametric/Implicit. I controlli sotto (limiti, Thickness, Run) hanno i
    // margini nel loro contenitore, panelParametricBottom.
    // CAMBIO DI SOTTO-TAB PARAMETRICO (Surface <-> Tubes): come 3D/Cross
    // Section. Aprire una linguetta mostra la sua superficie di DEFAULT (il
    // toro, o il trifoglio), quindi butta via la scena: si chiede prima del
    // lavoro non salvato. La conferma va su tabBarClicked, perche'
    // currentChanged scatta a linguetta gia' cambiata; su Cancel si lascia
    // cambiare, si dice al reset di non fare nulla e la si riporta indietro.
    if (ui->subTabParametric->tabBar()) {
        connect(ui->subTabParametric->tabBar(), &QTabBar::tabBarClicked,
                this, [this](int index) {
            if (index < 0) return;
            const bool toTubes = (ui->subTabParametric->widget(index) == ui->subTabParametricTubes);
            if (toTubes == m_scene.tubesTab) {
                // RICLIC sulla linguetta gia' attiva = "ricomincia da capo"
                // (currentChanged non scatta). Su Cancel non si resetta.
                if (!confirmDiscardUnsaved(ScopeScene)) return;
                applyParametricSubTabReset();
                return;
            }
            if (!confirmDiscardUnsaved(ScopeScene)) {
                const bool back = m_scene.tubesTab;
                m_suppressNextTubesTabReset = true;
                QTimer::singleShot(0, this, [this, back]() {
                    setTubesTab(back);
                    m_suppressNextTubesTabReset = false;
                });
            }
        });
    }
    // Il clic: prima lo STATO, che il reset legge (quale superficie di default
    // disegnare), poi il reset. Il programma scrive la linguetta a segnali
    // bloccati (setTubesTab).
    connect(ui->subTabParametric, &QTabWidget::currentChanged,
            this, [this](int) {
        m_scene.tubesTab = (ui->subTabParametric->currentWidget() == ui->subTabParametricTubes);
        refreshTubeControls();
        applyParametricSubTabReset();
    });
    connect(ui->tabModeSelector, &QTabWidget::currentChanged,
            this, [this](int) { refreshTubeControls(); });
    refreshTubeControls();
    ui->glWidget->setEngineMode(GLWidget::ModeParametric);

    ui->stepSlider->setRange(10, 1000);
    setSteps(m_lastParametricSteps);
    ui->glWidget->setResolution(m_lastParametricSteps);

    ui->lblSteps->setText("Steps=");

    // 3. SOLO ORA COLLEGA IL SEGNALE DEL CAMBIO TAB
    // 3. SOLO ORA COLLEGA IL SEGNALE DEL CAMBIO TAB
    // Il clic (o un cambio a segnali vivi del programma, che vuole il reset):
    // prima lo STATO, che il reset e tutto il resto leggono.
    connect(ui->tabModeSelector, &QTabWidget::currentChanged,
            this, [this](int index) {
        m_scene.implicitMode = (index == 1);
        applyModeTabReset(index);
    });

    // Cambio di sotto-tab dentro Implicit (3D <-> Cross Section). Il pannello
    // Run/Variations/Texture e i LIMITI X/Y/Z sono una sola istanza fisica
    // condivisa; equazione e radio Shell/Solid sono separati. Aprendo un
    // sotto-tab si mostra la sua superficie di DEFAULT, quindi i controlli
    // condivisi vanno riportati al default insieme a lei: restando addosso i
    // valori dell'altro sotto-tab, i limiti la taglierebbero e le costanti la
    // deformerebbero (vedi resetImplicitSharedFields).
    // Nessun reset di camera/texture/animazione: le superfici di default non ne
    // hanno (vedi loadCrossSectionDefaultSurface).
    if (ui->subTabImplicit) {
        // LAVORO NON SALVATO, come per il cambio di modalita' (tabModeSelector
        // qui sopra). Cambiare sotto-tab carica la superficie di DEFAULT e
        // riporta ai default i controlli condivisi: e' una distruzione di scena
        // quanto il passaggio Parametric <-> Implicit, e finora non chiedeva
        // nulla -- un tocco sulla linguetta sbagliata buttava via il lavoro.
        // Stesso schema, per le stesse ragioni: la conferma va su tabBarClicked
        // perche' currentChanged scatta a linguetta GIA' cambiata e da li' non
        // si potrebbe piu' rifiutare; tabBarClicked non permette di annullare il
        // cambio (Qt lo esegue subito dopo), quindi su Cancel si lascia cambiare
        // la linguetta, si dice all'handler di NON resettare e si riporta il
        // sotto-tab dov'era.
        if (ui->subTabImplicit->tabBar()) {
            connect(ui->subTabImplicit->tabBar(), &QTabBar::tabBarClicked,
                    this, [this](int index) {
                if (index < 0) return;

                if (index == (crossSectionTab() ? 1 : 0)) {
                    // RICLIC SULLA LINGUETTA GIA' ATTIVA = "ricomincia da capo",
                    // esattamente come per il tab principale. currentChanged non
                    // scatta se l'indice non cambia, quindi senza questo ramo
                    // ricliccare il sotto-tab in cui sei gia' non faceva nulla:
                    // dopo un New lo schermo restava vuoto e la superficie di
                    // default compariva solo premendo la linguetta NON attiva --
                    // mentre Parametric/Implicit la ricaricano in entrambi i casi.
                    // Su Cancel non si resetta (qui la linguetta non cambia,
                    // basta uscire).
                    if (!confirmDiscardUnsaved(ScopeScene)) return;
                    applyImplicitSubTabReset(index);
                    return;
                }

                if (!confirmDiscardUnsaved(ScopeScene)) {
                    const bool back = crossSectionTab();
                    m_suppressNextSubTabReset = true;
                    QTimer::singleShot(0, this, [this, back]() {
                        setCrossSectionTab(back);
                        m_suppressNextSubTabReset = false;
                    });
                }
            });
        }

        // Il clic: prima lo STATO, che il reset legge (quale superficie di
        // default caricare). Arriva solo dai clic: il programma scrive la
        // linguetta a segnali bloccati (setCrossSectionTab).
        connect(ui->subTabImplicit, &QTabWidget::currentChanged,
                this, [this](int index) {
            m_scene.crossSectionTab = (index == 1);
            applyImplicitSubTabReset(index);
        });
    }

    // RESET SULLA LINGUETTA GIA' ATTIVA. currentChanged non scatta se l'indice
    // non cambia, quindi ricliccare il tab in cui sei gia' non faceva nulla.
    // Ora vale come "ricomincia da capo in questa modalita'": la stessa pulizia
    // totale del cambio tab (superficie di default, script/texture/path azzerati,
    // camera, zoom e limiti di spazio ripristinati) senza dover passare
    // dall'altra modalita' e tornare indietro. Riusa lo STESSO percorso, non una
    // copia: e' quello gia' validato dall'uso quotidiano, e i residui della
    // superficie precedente sono la causa storica delle deformazioni.
    if (ui->tabModeSelector->tabBar()) {
        connect(ui->tabModeSelector->tabBar(), &QTabBar::tabBarClicked,
                this, [this](int index) {
            if (index < 0) return;

            if (index == (implicitMode() ? 1 : 0)) {
                // Riclic sulla linguetta attiva = "ricomincia da capo".
                // Lavoro non salvato: si chiede prima di buttarlo via, per OGNI
                // modulo sporco (il reset azzera anche texture e suono). Su
                // Cancel non si resetta (qui il tab non cambia, basta uscire).
                if (!confirmDiscardUnsaved(ScopeScene)) return;
                // "Ricomincia da capo": a differenza del cambio di modalita',
                // qui la texture non sopravvive, quindi il suo script non va
                // ripristinato nell'editor. Vedi m_sameTabRestart.
                m_sameTabRestart = true;
                applyModeTabReset(index);
                m_sameTabRestart = false;
                return;
            }

            // CAMBIO DI MODALITA' VERO. Anche questo distrugge la scena, via
            // currentChanged -> applyModeTabReset, e finora non chiedeva nulla:
            // un tocco sulla linguetta sbagliata buttava via il lavoro. La
            // conferma va CHIESTA QUI, perche' currentChanged scatta a tab gia'
            // cambiato e da li' non si potrebbe piu' rifiutare.
            // tabBarClicked non permette di annullare il cambio (Qt lo esegue
            // subito dopo), quindi su Cancel si lascia cambiare la linguetta e
            // si dice ad applyModeTabReset di NON resettare, riportando poi il
            // tab dov'era: la scena resta intatta.
            if (!confirmDiscardUnsaved(ScopeScene)) {
                const bool back = implicitMode();
                m_suppressNextModeTabReset = true;
                QTimer::singleShot(0, this, [this, back]() {
                    setImplicitMode(back);
                    m_suppressNextModeTabReset = false;
                });
            }
        });
    }

    setEqText(&EquationTexts::x, QStringLiteral("(0.8 + 0.3*cos(v))*cos(u)"));
    setEqText(&EquationTexts::y, QStringLiteral("(0.8 + 0.3*cos(v))*sin(u)"));
    setEqText(&EquationTexts::z, QStringLiteral("0.3*sin(v)"));
    setEqText(&EquationTexts::p, QStringLiteral("0.0"));

    ui->glWidget->setParametricEquations(m_scene.eq.x, m_scene.eq.y, m_scene.eq.z, m_scene.eq.p);

    // Sotto-tab Tubes: il tubo di default, quello che il cambio di linguetta
    // disegna. Spessore: slider in centesimi, kTubeThicknessMin..Max (il
    // massimo si allarga coi valori scritti, vedi pushTubeThickness).
    ui->tubeThicknessSlider->setRange(qRound(kTubeThicknessMin * 100.0f),
                                      qRound(kTubeThicknessMax * 100.0f));
    {
        const TubeTexts def = defaultTubeTexts();
        for (TubeField f : { &TubeTexts::x, &TubeTexts::y, &TubeTexts::z, &TubeTexts::p,
                             &TubeTexts::thickness })
            setTubeText(f, def.*f);
    }

    // All'avvio, prima che i campi siano agganciati alle derivazioni: solo
    // stato e campo (showLineFields arrivera' coi reset).
    setLineText(m_scene.lim.uMin, QString::number(uMin, 'g', 12));
    setLineText(m_scene.lim.uMax, QString::number(uMax, 'g', 12));
    setLineText(m_scene.lim.vMin, QString::number(vMin, 'g', 12));
    setLineText(m_scene.lim.vMax, QString::number(vMax, 'g', 12));
    setLineText(m_scene.lim.wMin, QString::number(wMin, 'g', 12));
    setLineText(m_scene.lim.wMax, QString::number(wMax, 'g', 12));
    ui->wMinEdit->setEnabled(false);
    ui->wMaxEdit->setEnabled(false);

    updateULimits();
    updateVLimits();

    ui->aSlider->setRange(0, 1000); ui->aSlider->setValue(100);
    ui->bSlider->setRange(0, 1000); ui->bSlider->setValue(100);
    ui->cSlider->setRange(0, 1000); ui->cSlider->setValue(100);
    ui->dSlider->setRange(0, 1000); ui->dSlider->setValue(100);
    ui->eSlider->setRange(0, 1000); ui->eSlider->setValue(100);
    ui->fSlider->setRange(0, 1000); ui->fSlider->setValue(100);
    ui->sSlider->setRange(-1000, 1000); ui->sSlider->setValue(0);

    // --- Inizializzazione Testi di Default ---
    if (ui->lineA->text().isEmpty()) ui->lineA->setText("1");
    if (ui->lineB->text().isEmpty()) ui->lineB->setText("1");
    if (ui->lineC->text().isEmpty()) ui->lineC->setText("1");
    if (ui->lineD->text().isEmpty()) ui->lineD->setText("1");
    if (ui->lineE->text().isEmpty()) ui->lineE->setText("1");
    if (ui->lineF->text().isEmpty()) ui->lineF->setText("1");
    if (ui->lineS->text().isEmpty() || ui->lineS->text() == "0.4") {
        ui->lineS->setText("0");
    }

    if (!m_meshDebounce) {
        m_meshDebounce = new QTimer(this);
        m_meshDebounce->setSingleShot(true);
        m_meshDebounce->setInterval(120);
        connect(m_meshDebounce, &QTimer::timeout, this, [this]() {
            if (ui->glWidget && !implicitMode()) {
                ui->glWidget->setResolution(m_scene.steps);
            }
            // Input nuovo (costanti/steps): un errore geodetico precedente non
            // deve congelare il ricalcolo, altrimenti riportare una costante
            // al valore buono lascia la mesh bloccata sull'ultimo stato.
            m_geodesicErrorPending = false;
            // useAppliedEquations: questo debounce lo fanno partire lo slider
            // Steps e le costanti, che NON sono un Run. Senza il flag il ramo
            // geodetico rileggeva X/Y/Z/P dai campi e applicava equazioni
            // modificate e non confermate -- lo stesso difetto che avevano i
            // limiti: bastava muovere Steps per committarle di straforo.
            checkAndTriggerMeshUpdate(/*useAppliedEquations=*/true);

            // Il Run NON si spegne qui, in nessuno dei due rami. Questo percorso
            // (slider costanti, slider Steps) non applica equazioni: le congela
            // tutte sullo snapshot -- la mappa X/Y/Z/P e, dal disallineamento
            // corretto, anche i 7 campi del flusso geodetico. Se l'utente ha
            // scritto qualcosa in quei campi la modifica e' ancora in attesa, e
            // il Run e' l'unico modo di applicarla: spegnerlo la renderebbe
            // irraggiungibile. Solo un Run vero rialza m_parametricApplied.
            updateMasterButtonState();
        });
    }

    // --- MOTORE COSTANTI A CASCATA --- (vedi MainWindow::evaluateCascade)
    auto connectSlider = [this](ConstField field) {
        QSlider *slider = constantSlider(field);
        QLineEdit *line = constantFieldEdit(field);
        connect(slider, &QSlider::valueChanged, this, [this, field, line, slider](int val) {
            if (!line->hasFocus()) {
                setConstText(field, QString::number(val / 100.0f, 'g', 6));
                // Senza trascinamento (clic accanto alla maniglia, frecce,
                // rotellina) non arriva nessun sliderReleased: lo snap qui.
                if (!slider->isSliderDown()) applyDiscreteConstants();
                evaluateCascade(); // Aggiorna le altre caselle che dipendono da questo!
            }
        });
        // Snap delle costanti discrete ("A := int(1,6)") al RILASCIO, non durante
        // il trascinamento: agganciarlo a valueChanged farebbe scattare il cursore
        // sotto il dito a ogni tacca, e rigenererebbe la mesh a ogni scatto.
        connect(slider, &QSlider::sliderReleased, this, [this]() {
            if (applyDiscreteConstants()) evaluateCascade();
        });
    };

    for (ConstField f : constantFields()) connectSlider(f);

    auto connectLineEdit = [this](QLineEdit* line) {
        connect(line, &QLineEdit::editingFinished, this, [this]() {
            // Prima lo snap delle costanti discrete: cosi' la cascata sotto parte
            // gia' dal valore intero e non ricalcola due volte.
            applyDiscreteConstants();
            evaluateCascade();                           // clamp + cascata + slider + push costanti
            if (m_meshDebounce) m_meshDebounce->stop();  // evita il doppio ridisegno asincrono
            SE_GEO_PROBE("costante editingFinished -> commitFieldsOnEnter");
            const bool okConst = commitFieldsOnEnter();  // valida: se ok ridisegna, altrimenti vecchia immagine + popup
            SE_GEO_PROBE("costante esito applied=%d", int(okConst));
        });
    };

    connectLineEdit(ui->lineA); connectLineEdit(ui->lineB);
    connectLineEdit(ui->lineC); connectLineEdit(ui->lineD);
    connectLineEdit(ui->lineE); connectLineEdit(ui->lineF);
    connectLineEdit(ui->lineS);

    evaluateCascade();

    ui->stepSlider->setRange(10, 1000);
    int initialSteps = 100;
    setSteps(initialSteps);
    ui->glWidget->setResolution(initialSteps);
    ui->lblSteps->setText(QString("Steps="));

    // (1) valueChanged: aggiorna testo + avvia debounce
    connect(ui->stepSlider, &QSlider::valueChanged, this, [this](int val) {
        // Il gesto (o un setValue a segnali vivi, come la riduzione del
        // massimo in checkParametricDependency): lo stato per primo.
        m_scene.steps = val;
        ui->lineSteps->setText(QString::number(val));
        if (!ui->glWidget) return;
        if (implicitMode()) {
            ui->glWidget->setRaySteps(val);
            ui->glWidget->update();
        } else {
            m_meshDebounce->start();
        }
    });

    // (2) sliderPressed: sospende il rendering durante il trascinamento
    connect(ui->stepSlider, &QSlider::sliderPressed, this, [this]() {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
    });

    // (3) sliderReleased: riattiva il rendering e rigenera al rilascio
    connect(ui->stepSlider, &QSlider::sliderReleased, this, [this]() {
        if (ui->glWidget) {
            ui->glWidget->setUpdatesEnabled(true);
            ui->glWidget->update();
        }
        m_meshDebounce->start();
    });

    auto applyStepsFromLine = [this](bool notify) {
        const QString txt = ui->lineSteps->text().trimmed();
        if (txt.isEmpty()) return;   // vuoto durante la digitazione: ignora
        bool ok = false;
        int val = txt.toInt(&ok);
        if (!ok) {
            // notify=true solo al commit (Enter/uscita campo): niente popup mentre si digita
            if (!ok) {
                if (notify && !m_constantPopupActive) {
                    m_constantPopupActive = true;
                    InputValidator::showInvalidStepsError(this, txt);
                    setSteps(m_scene.steps);   // il campo torna a mostrare lo stato
                    ui->lineSteps->selectAll();
                    // Reset RIMANDATO a fine ciclo di eventi, come gli altri
                    // popup di questo modulo. Oggi qui si arriva una volta sola
                    // (i filtri tastiera consumano il Return, quindi dei due
                    // trigger collegati -- editingFinished e returnPressed --
                    // ne scatta uno), ma il reset sincrono rende il doppione
                    // dipendente da quel dettaglio: basterebbe un percorso che
                    // lascia passare il Return per vedere due box in fila.
                    QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
                }
                return;
            }
        }
        // Oltre il fondo corsa dello slider si puo' scrivere, fino a
        // stepsLimit: il fondo corsa diventa il valore scritto (pushStepsRange),
        // o di nuovo mille se e' piu' basso. Prima si allarga, poi il valore a
        // segnali vivi, poi si stringe: stringere prima taglierebbe il valore
        // corrente in silenzio e il setValue non emetterebbe nulla.
        const int limit = stepsLimit();
        val = std::clamp(val, ui->stepSlider->minimum(), limit);
        if (val > ui->stepSlider->maximum()) pushStepsRange(val);
        if (val != m_scene.steps)
            ui->stepSlider->setValue(val);   // emette valueChanged -> stato, glWidget e testo
        else
            ui->lineSteps->setText(QString::number(val));   // tagliato al tetto: il campo lo mostra
        pushStepsRange(val);
    };

    // Trigger "forti": al commit notifichiamo (notify=true)
    connect(ui->lineSteps, &QLineEdit::editingFinished, this, [applyStepsFromLine]() { applyStepsFromLine(true); });
    connect(ui->lineSteps, &QLineEdit::returnPressed,   this, [applyStepsFromLine]() { applyStepsFromLine(true); });

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_stepsDebounce = new QTimer(this);
    m_stepsDebounce->setSingleShot(true);
    m_stepsDebounce->setInterval(350);
    connect(m_stepsDebounce, &QTimer::timeout, this, [applyStepsFromLine]() { applyStepsFromLine(false); });

    connect(ui->lineSteps, &QLineEdit::textEdited, this, [this](const QString&) {
        m_stepsDebounce->start();
    });
#endif

    // 1. Dipendenze delle equazioni principali
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    auto markUserEdit = [this]() {
        // Lavoro dell'utente da proteggere: il reset chiedera' se salvarlo.
        // Il campo va riportato QUAL E': questa lambda serve 13 widget diversi e
        // passava sempre lineX, cosi' un edit alle composizioni (U/V/W) o ai
        // vincoli espliciti veniva contato come "sto scrivendo le equazioni".
        noteSceneEdited(qobject_cast<QWidget*>(sender()));
        // Le equazioni sono cambiate: il Run parametrico "one-shot" (senza 't')
        // torna eseguibile. updateMasterButtonState() riabilita il tasto.
        m_parametricApplied = false;
        updateMasterButtonState();

        // L'evidenziazione del preset in libreria RESTA anche dopo aver
        // modificato le equazioni. Prima cadeva al primo carattere, e solo sul
        // ramo Superfici -- texture, suoni e record non l'hanno mai fatto:
        // un'asimmetria nata dall'aggiunta fatta da un lato solo. Serve a
        // ricordare su quale preset si sta lavorando, che e' la ragione per cui
        // la si guarda. Effetto collaterale utile: annullando il caricamento di
        // un altro preset (Cancel sul popup del lavoro non salvato, ~10407)
        // c'e' sempre un item da rievidenziare, e l'albero non resta spoglio.
    };
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, markUserEdit);

    // Anche composizioni (U/V/W) e vincoli espliciti cambiano la superficie:
    // un loro edit deve riabilitare il Run parametrico one-shot.
    connect(ui->lineU, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineV, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineW, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, markUserEdit);
    // ...e la curva del sotto-tab Tubes, che come le equazioni di Surface
    // decide anche i limiti u (accesi solo se la curva cita u).
    for (QPlainTextEdit *e : { ui->lineTubeX, ui->lineTubeY, ui->lineTubeZ, ui->lineTubeP }) {
        connect(e, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(e, &QPlainTextEdit::textChanged, this, [this] {
            if (tubesShown()) checkParametricDependency();
        });
    }

    // SPESSORE DEL TUBO, campo e slider come le costanti: lo slider scrive il
    // campo (in centesimi) e il tubo lo segue subito (u_tubeRadius e' un
    // uniform, niente Run); il campo si applica alla CONFERMA -- Invio o uscita
    // dal campo --, non a ogni carattere: digitando "1.5" il tubo passava per
    // 1 e poi per 1.5. Sotto il minimo si riporta al minimo; sopra il massimo
    // dello slider vale quel che si scrive, e lo slider si allarga.
    connect(ui->tubeThicknessSlider, &QSlider::valueChanged, this, [this](int value) {
        setTubeText(&TubeTexts::thickness, QString::number(value / 100.0, 'g', 6));
        updateMasterButtonState();   // il campo era forse vuoto (Run spento)
    });
    connect(ui->lineTubeThickness, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const float thickness = tubeThicknessValue(&ok);
        if (ok && thickness > ExpressionParser::evaluateSimple(m_scene.tube.thickness.trimmed()))
            setTubeText(&TubeTexts::thickness, QString::number(double(thickness), 'g', 6));
        pushTubeThickness();
    });
    // Il Run guarda anche lo spessore (vuoto o illeggibile = spento): ogni
    // carattere lo rivaluta, come per la curva.
    connect(ui->lineTubeThickness, &QLineEdit::textChanged, this, [this]() {
        if (tubesShown()) updateMasterButtonState();
    });

    auto markTextureModified = [this]() { m_textureModified = true; };
    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, markTextureModified);

    // Run "one-shot" della texture Ray Marching: modificare lo script di colore
    // (lineTexture) o di displacement (lineVariations) lo riabilita.
    auto markRmTextureEdited = [this]() {
        m_rmTextureApplied = false;
        // NIENTE noteSceneEdited: lineTexture/lineVariations sono campi del
        // modulo TEXTURE, non della geometria (il loro lavoro non salvato e'
        // del modulo: PartTexture nell'impronta della scena, non la SCENA --
        // su un reset uscivano DUE popup di fila, prima "scena" poi "texture",
        // per un solo lavoro). Passando lineTexture non produceva
        // nemmeno l'avviso cross-dock: cade nel ramo "campo che non definisce
        // la superficie" e torna subito.
        //
        // Lavoro non salvato del MODULO texture: lo dice il confronto dello
        // stato (textureModuleDirty) e lo elenca confirmDiscardUnsaved.
        //
        // IL NOME (libName) NON SI AZZERA QUI.
        // Prima si azzerava, col ragionamento "il codice non e' piu' quello
        // della voce, quindi il nome punterebbe a una texture diversa da
        // quella a schermo". Ma libName non vuol dire "il codice e' identico
        // a quella voce": vuol dire **da quale voce questa texture VIENE**.
        // Sono due cose diverse, e la prima si ricalcola quando serve
        // confrontando i codici -- e' esattamente cio' che fa
        // focusedTextureLibraryItem.
        //
        // Azzerandolo si distruggeva il legame proprio nel caso per cui e'
        // stato introdotto. Sequenza misurata: Sync (allinea), poi ritocco a
        // mano della densita' nell'editor -> qui il nome si azzerava -> il
        // salvataggio omette libName (lo scrive solo se non vuoto,
        // presetserializer ~1516) -> il record ricaricato non ha piu'
        // l'ancora, e "Sync Focused Texture" resta GRIGIO per sempre benche'
        // il disallineamento ci sia (record 6.0 contro libreria 12.0).
        // Cioe': la voce si spegneva appena si creava il lavoro che le
        // compete.
        //
        // Tenerlo non fa danni: un codice divergente lo vede il gate, che
        // confronta i due testi e abilita la voce; e se davvero non c'entra
        // piu' nulla, il focus nell'albero ricade sul match per CODICE, che
        // ha sempre la precedenza sul nome (LibraryTreeFocus::selectTexture: due
        // passate, prima il codice).
        updateMasterButtonState();
    };
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, markRmTextureEdited);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, markRmTextureEdited);

    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, [this](){
        // L'editor e' la VISTA dello slot mostrato: cio' che l'utente scrive va
        // li', subito. (Le scritture del programma passano da setScriptText e
        // arrivano qui a segnali bloccati.)
        const ScriptSlot shown = shownScriptSlot();
        const QString typed = ui->txtScriptEditor->toPlainText();
        m_scriptEditorText = typed;
        switch (shown) {
        case SlotSurface:           m_scene.surfaceScriptText = typed; break;
        case SlotSurfaceTexture:    m_scene.surfaceTextureScriptText = typed; break;
        case SlotMeshTexture:       syncMeshTextureSlot(); m_meshTextureScriptText = typed; break;
        case SlotBorderTexture:     syncBorderTextureSlot(); m_borderTextureScriptText = typed; break;
        case SlotBackgroundTexture: m_scene.bgTextureScriptText = typed; break;
        case SlotSound:             m_scene.soundScriptText = typed; break;
        case SlotNone:              break;
        }
        updateScriptButtonText();
        updateConstantsUIState();
        // txtScriptEditor e' UN widget per tre moduli: il lavoro appartiene a
        // quello che sta mostrando, e va marcato SOLO li'. Marcare anche la
        // scena (noteSceneEdited) mentre si scrive una texture o un suono
        // faceva uscire due popup di fila su un reset, per un lavoro solo.
        if (m_currentScriptMode == ScriptModeSurface)
            noteSceneEdited(ui->txtScriptEditor);
        // Mantiene allineato il tasto Save texture (hasSavableTexture legge l'editor
        // in modalità script texture parametrico/sfondo).
        updateMasterButtonState();
    });

    // 2. Mutua esclusione dei vincoli (con blocco segnali per evitare loop a catena!)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_scene.eq.explicitU.isEmpty()) {
            setEqText(&EquationTexts::explicitV, QString());
            setEqText(&EquationTexts::explicitW, QString());
        }
    });
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_scene.eq.explicitV.isEmpty()) {
            setEqText(&EquationTexts::explicitU, QString());
            setEqText(&EquationTexts::explicitW, QString());
        }
    });
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_scene.eq.explicitW.isEmpty()) {
            setEqText(&EquationTexts::explicitU, QString());
            setEqText(&EquationTexts::explicitV, QString());
        }
    });

    // 3. Dipendenze dei vincoli (chiamano checkParametricDependency, che a sua volta chiamerà updateConstraintState)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    // 4. Dipendenze delle composizioni
    connect(ui->lineU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    connect(ui->lineEquation, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineEquationCrossSection, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);

    // Run "one-shot" del tab Ray Marching: modificare l'EQUAZIONE implicita lo
    // riabilita. Solo lineEquation -> texture e displacement sono del modulo
    // texture, non della geometria (coerente con geomAnimated in onStartClicked).
    connect(ui->lineEquation, &QPlainTextEdit::textChanged, this, [this]() {
        m_implicitApplied = false;
        noteSceneEdited(ui->lineEquation);
        updateMasterButtonState();

        // Come nel ramo parametrico qui sopra: l'evidenziazione resta.
    });

    // Stessa cosa per il sotto-tab Cross Section (equazione a 4 variabili):
    // editor distinto, ma stesso contratto Run "one-shot" del tab 3D.
    connect(ui->lineEquationCrossSection, &QPlainTextEdit::textChanged, this, [this]() {
        m_implicitApplied = false;
        noteSceneEdited(ui->lineEquationCrossSection);
        updateMasterButtonState();
    });

    if (ui->lnU) {
        connect(ui->lnU,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnV,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnW,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndU,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndV,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndW,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnW, &QPlainTextEdit::textChanged, this, markUserEdit);

        connect(ui->lndU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndW, &QPlainTextEdit::textChanged, this, markUserEdit);
    }

    checkParametricDependency();
    updateConstraintState();

    // ----------------------------------------------------
    // IMPLICIT MODE SETTINGS (Equazioni e Limiti Spaziali)
    // ---------------------------------------------------

    // --- 0. Impostazioni di Default UI ---
    // DUE GRUPPI ESCLUSIVI SEPARATI. I quattro radio (Shell/Solid e Fast/Precise)
    // sono fratelli nella griglia gridRenderControls, e l'esclusivita' automatica
    // dei QRadioButton vale fra TUTTI i fratelli con lo stesso genitore: senza
    // questi gruppi accendere "Precise" spegneva "Shell". Vanno creati PRIMA dei
    // setChecked e dei connect qui sotto.
    m_shellSolidGroup = new QButtonGroup(this);
    m_shellSolidGroup->setExclusive(true);
    m_shellSolidGroup->addButton(ui->radioShell);
    m_shellSolidGroup->addButton(ui->radioSolid);

    m_marcherGroup = new QButtonGroup(this);
    m_marcherGroup->setExclusive(true);
    m_marcherGroup->addButton(ui->radioMarcherFast);
    m_marcherGroup->addButton(ui->radioMarcherPrecise);

    applyImplicitShellMode(true);   // "Shell" di default: stato, radio e motore (modalita' 1)

    // --- Connessione dei Radio Button (Solid/Shell) ---
    // UNA SOLA COPPIA, in panelRenderControls: il widget comune ai due sotto-tab
    // ("3D" e "Cross Section"), insieme a Thickness e ai radio del marcher.
    //
    // Prima i radio erano DUPLICATI, uno per pannello equazione, perche' i
    // pannelli sono due — ma lo stato Shell/Solid e' sempre stato UNO SOLO (il
    // render mode del motore e' globale, e il preset lo salva in un solo campo).
    // Quella duplicazione ha prodotto una serie di bug: i radio del Cross Section
    // inizialmente non erano nemmeno collegati (cliccarli non faceva nulla), e il
    // serializer leggeva la coppia del 3D anche salvando dal Cross Section.
    // Con un'unica coppia il problema non esiste piu': niente da riallineare.
    auto updateImplicitRenderMode = [this](bool checked) {
        if (!checked) return;   // solo chi si accende, non chi si spegne
        const bool toShell = (sender() == ui->radioShell);
        applyImplicitShellMode(toShell);
        // SOLID: lo spessore non ha piu' un guscio da misurare, quindi torna al
        // minimo (setShellThicknessUI incapsula motore + slider + curva): lo
        // slider spento non mostra un valore che non si applica, e un preset
        // salvato in Solid non si porta dietro un residuo.
        // Il valore di prima pero' si RICORDA, e tornando a Shell si rimette:
        // senza, un giro Shell -> Solid -> Shell perdeva lo spessore del preset
        // (Ding Dong: 0.108 -> 0.005) e la figura cambiava forma -- il collo
        // arrotondato del guscio diventava la punta del cono.
        // SOLO sul click dell'utente, non in applyImplicitShellMode: quella gira
        // anche durante i load, dove il preset puo' avere il proprio spessore
        // salvato e azzerarlo qui lo perderebbe.
        if (!toShell) {
            const float keep = ui->glWidget ? ui->glWidget->shellThickness() : -1.0f;
            setShellThicknessUI(0.005f);          // azzera anche il ricordo...
            m_shellThicknessBeforeSolid = keep;   // ...quindi lo si scrive dopo
        } else if (m_shellThicknessBeforeSolid > 0.0f) {
            setShellThicknessUI(m_shellThicknessBeforeSolid);   // e lo consuma
        }
        // Il pannello Thickness ha senso solo con Shell (in Solid non c'e' guscio
        // di cui regolare la parete): il gate vive in updateRenderState, che va
        // richiamata qui o il pannello resterebbe come era fino al prossimo
        // evento che la fa girare. applyImplicitShellMode non la chiama da se'
        // perche' e' usata anche durante i load, dove gira comunque alla fine.
        updateRenderState();
    };
    connect(ui->radioShell, &QRadioButton::toggled, this, updateImplicitRenderMode);
    connect(ui->radioSolid, &QRadioButton::toggled, this, updateImplicitRenderMode);

    // --- MARCHER: Fast (sphere tracing storico) / Precise (ibrido) ---
    // "Fast" di default: e' il comportamento di sempre, e ogni superficie che non
    // chiede esplicitamente il Precise si disegna come prima. Il T^3 del Cross
    // Section accende Precise da loadCrossSectionDefaultSurface, e i preset se lo
    // portano dietro (chiave "hybridMarcher").
    //
    // UNA SOLA COPPIA, nella zona comune ai due sotto-tab (come il Thickness):
    // la scelta del marcher non dipende dal sotto-tab, quindi non serve il
    // doppione con il riallineamento che Shell/Solid richiede.
    setMarcherUI(false);   // Fast di default: motore e radio
    auto updateMarcherMode = [this](bool checked) {
        if (!checked) return;                 // solo chi si accende
        const bool precise = (sender() == ui->radioMarcherPrecise);
        if (ui->glWidget) ui->glWidget->setHybridMarcher(precise);
        // E' un uniform: nessun rebuildShader, il cambio si vede al frame dopo.
        // Nessun gating: Ray Steps e Step Relax servono con entrambi i marcher
        // (vedi il commento in updateRenderState).
        noteSceneEdited(ui->radioMarcherPrecise);
    };
    if (ui->radioMarcherFast)
        connect(ui->radioMarcherFast, &QRadioButton::toggled, this, updateMarcherMode);
    if (ui->radioMarcherPrecise)
        connect(ui->radioMarcherPrecise, &QRadioButton::toggled, this, updateMarcherMode);

    // --- SFONDO: Fixed / Sphere / Cylinder / Cube (gruppo "Background Controls") ---
    // I quattro radio hanno un contenitore proprio (panelBgLock), quindi si
    // escludono a vicenda senza toccare Base/Phong/WireFrame. Vale per entrambi i
    // modi: anche in Ray Marching lo sfondo e' lo stesso pass.
    // L'indice in bgSkyRadios() E' la modalita' (GLWidget::BgSkyMode): una sola
    // tabella per clic, load, reset e tooltip.
    // Nessun rebuild: la modalita' e' letta dallo shader a ogni frame (vedi
    // GLWidget::render). Si segna la scena come modificata: il record salva la
    // scelta ("background"/"skyMode").
    {
        const QList<QRadioButton*> skyRadios = bgSkyRadios();
        for (int mode = 0; mode < skyRadios.size(); ++mode) {
            if (!skyRadios[mode]) continue;
            connect(skyRadios[mode], &QRadioButton::toggled, this, [this, mode](bool checked) {
                if (!checked) return;         // solo chi si accende
                if (ui->glWidget) ui->glWidget->setBackgroundSkyMode(mode);
            });
        }
    }

    // SPESSORE DEL GUSCIO (Shell). Scala 0..100 -> 0.005..0.30, non lineare:
    // quadratica, cosi' la prima meta' della corsa copre i valori sottili (dove
    // serve precisione) e la seconda arriva ai gusci spessi.
    //
    // Perche' e' un controllo e non una costante: normalizzare il campo col
    // gradiente ha reso lo spessore una LUNGHEZZA VERA, uguale per tutte le
    // equazioni. E' corretto, ma prima valeva 0.01/|grad|, quindi dipendeva da
    // come l'equazione era scritta: le superfici con un fattore di scala davanti
    // avevano gusci molto piu' spessi. Misurato, per tornare all'aspetto storico
    // servono 0.10 a Ding Dong (|grad| 0.096) e 0.26 a Steiner (|grad| 0.039),
    // mentre la sfera e il T^3 del Cross Section stanno bene a 0.005: due ordini
    // di grandezza di differenza, nessun default unico puo' accontentarle tutte.
    // Il tetto 0.30 copre Steiner con margine.
    const double kShellThickMin = 0.005, kShellThickMax = 0.30;
    auto shellThickFromSlider = [=](int v) {
        const double f = v / 100.0;
        return kShellThickMin + (kShellThickMax - kShellThickMin) * f * f;
    };
    if (ui->shellThicknessSlider) {
        ui->shellThicknessSlider->setRange(0, 100);
        ui->shellThicknessSlider->setValue(0);          // 0.005 = comportamento storico
        // Stesso aspetto degli altri slider grandi: l'handle e' 30px con
        // margin -10px, quindi senza questo stile (e senza l'altezza minima nel
        // .ui) il disco viene TAGLIATO dal bordo del widget.
        ui->shellThicknessSlider->setStyleSheet(
            "QSlider::groove:horizontal { border: 1px solid #999; height: 12px;"
            " border-radius: 6px; margin: 2px 0; background: #AAAAAA; }"
            "QSlider::handle:horizontal { background: white; border: 1px solid #5c5c5c;"
            " width: 30px; height: 30px; margin: -10px 0; border-radius: 15px; }");
        ui->shellThicknessSlider->setMinimumHeight(40);
        connect(ui->shellThicknessSlider, &QSlider::valueChanged, this,
                [this, shellThickFromSlider](int v) {
            const double t = shellThickFromSlider(v);
            if (ui->glWidget) ui->glWidget->setShellThickness((float)t);
            noteSceneEdited(ui->shellThicknessSlider);
        });
    }


    // --- 1. Equazioni Implicite di Default (Solo 3D) ---
    setRmText(&ImplicitTexts::equation, QStringLiteral("x^2 + y^2 + z^2 = 1.0"));
    updateConstantsUIState();

    auto updateImplicitEquations = [this]() {
        if(ui->glWidget) {
            QString rawEq = m_scene.rm.equation.trimmed();
            QString implicitEqF;

            // Formatttiamo l'equazione rimuovendo l'uguale per renderla digeribile da GLSL
            if (rawEq.contains("=")) {
                QStringList parts = rawEq.split("=");
                if (parts.size() == 2) {
                    implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
                }
            } else {
                implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
            }

            ui->glWidget->setImplicitEquation(implicitEqF);
        }
    };

    updateImplicitEquations();


    // --- 2. Limiti Spaziali (Facoltativi) ---
    // Partiamo con le caselle vuote = Nessun taglio applicato
    for (QString *t : { &m_scene.lim.xMin, &m_scene.lim.xMax, &m_scene.lim.yMin,
                        &m_scene.lim.yMax, &m_scene.lim.zMin, &m_scene.lim.zMax })
        setLineText(*t, QString());

    // LIMITI SPAZIALI X/Y/Z: si applicano al RUN, non all'Invio -- stessa regola
    // dei limiti u/v/w e delle equazioni. Sono il taglio della scena in Ray
    // Marching, cioe' parte della definizione di cio' che si vede, e come tali
    // vanno confermati insieme all'equazione implicita. Niente connect su
    // editingFinished qui: l'applicazione passa da applySpaceLimits(), che il
    // ramo Ray Marching di onStartClicked chiama a ogni Run.
    // Le modifiche dell'utente riaccendono il Run one-shot, come per ogni altro
    // campo del modulo (vedi le connect textEdited piu' sotto).
    applySpaceLimits(/*notify=*/false);   // allineamento iniziale, silenzioso

    for (QLineEdit* spaceEdit : { ui->lineXMin, ui->lineXMax,
                                  ui->lineYMin, ui->lineYMax,
                                  ui->lineZMin, ui->lineZMax }) {
        // Come i limiti u/v/w, ammettono A..F/S: scrivere "2*A" sblocca subito
        // slider e casella di A.
        connect(spaceEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(spaceEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);
        connect(spaceEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            noteSceneEdited(qobject_cast<QWidget*>(sender()));
            // Il tasto Run del tab Ray Marching torna eseguibile: c'e' un taglio
            // nuovo da applicare. E' l'omologo di m_parametricApplied per il
            // ramo implicito.
            m_implicitApplied = false;
            updateMasterButtonState();
        });
    }

    // LIMITI U/V/W: il dominio fa parte della definizione della superficie e
    // segue la stessa regola delle equazioni -- a superficie FERMA aspetta il
    // Run, IN MOTO entra subito (li' un Run da premere non c'e': il tasto e'
    // "Stop"). La conferma del campo -- Invio o uscita -- passa da
    // commitLimitFieldOnEnter, che valida il numero e registra il dominio
    // nell'engine in entrambi i casi; le connect stanno piu' avanti, insieme a
    // quelle che riaccendono il Run. Il Return non arriva mai ai QLineEdit --
    // lo consumano prima i filtri tastiera (desktop e mobile) -- percio'
    // l'Invio passa di li'. onStartClicked rilegge e valida i campi per conto
    // suo, cosi' al Run i limiti entrano in vigore insieme alle equazioni.
    //
    // I LIMITI SPAZIALI X/Y/Z qui sopra restano invece immediati: sono un
    // taglio della VISTA in Ray Marching, non il dominio dei parametri.

    connect(ui->btnTextureCode, &QPushButton::clicked, this, [this]() {
        onRunRaymarchTextureClicked();
    });

    connect(ui->btnSave, &QPushButton::clicked, this, &MainWindow::onSaveTextureClicked);
}


void MainWindow::checkParametricDependency()
{
    QString eqX = m_scene.eq.x;
    QString eqY = m_scene.eq.y;
    QString eqZ = m_scene.eq.z;
    QString eqP = m_scene.eq.p;

    // Testi espliciti
    QString eqExplU = m_scene.eq.explicitU;
    QString eqExplV = m_scene.eq.explicitV;
    QString eqExplW = m_scene.eq.explicitW;

    // Testi composizione
    QString defU = m_scene.eq.u;
    QString defV = m_scene.eq.v;
    QString defW = m_scene.eq.w;

    // 1. ANALISI RAW: Contiamo ESATTAMENTE cosa ha digitato l'utente
    QString mainEqs = eqX + " " + eqY + " " + eqZ + " " + eqP;

    bool hasRaw_u = mainEqs.contains(kReLowerU);
    bool hasRaw_v = mainEqs.contains(kReLowerV);
    bool hasRaw_w = mainEqs.contains(kReLowerW);
    int rawLowerCount = (hasRaw_u ? 1 : 0) + (hasRaw_v ? 1 : 0) + (hasRaw_w ? 1 : 0);

    bool hasRaw_U = mainEqs.contains(kReUpperU);
    bool hasRaw_V = mainEqs.contains(kReUpperV);
    bool hasRaw_W = mainEqs.contains(kReUpperW);
    int rawUpperCount = (hasRaw_U ? 1 : 0) + (hasRaw_V ? 1 : 0) + (hasRaw_W ? 1 : 0);

    // Variabili di stato per i Tab
    bool needsConstraint = false;
    bool needsComposition = false;
    bool needsGeodesic = false;

    // Controllo se ci sono testi inseriti nelle tab Composition o Geodesic
    bool compHasText = !defU.trimmed().isEmpty() ||
                       !defV.trimmed().isEmpty() ||
                       !defW.trimmed().isEmpty();

   bool geoHasText = hasGeodesicText();

    // Modalità metrica (script che restituisce mat3): la geometria è definita
    // dal tensore g_ij. I campi X/Y/Z/P restano una MAPPA DI VISUALIZZAZIONE
    // (embedding) modificabile: di norma la carta identità x=U,y=V,z=W, ma per
    // certe geometrie (es. paraboloide di Flamm / ponte di Einstein-Rosen) si
    // vuole una mappa esplicita z=±f(U) che pieghi il piano nello spazio 3D.
    // La mappa non altera la metrica: realizza fedelmente la g_ij intrinseca.
    // Constraints resta off (vincoli non hanno senso); Composition resta
    // disponibile per le definizioni intermedie usate dalla mappa.
    const bool metricModeActive = !m_metricScriptBody.trimmed().isEmpty();

    // 2. MACCHINA A STATI: Apertura e Blocco Tab intelligente
    if (ui->panelImplicit) {
        if (metricModeActive) {
            // Vive solo Geodesic Flow: le condizioni iniziali restano l'unico
            // input modificabile dal dock Equations.
            needsGeodesic = true;
            ui->panelImplicit->setTabEnabled(0, false);
            ui->panelImplicit->setTabEnabled(1, false);
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);

            if (ui->panelImplicit->count() > 2 &&
                    !ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                ui->panelImplicit->setCurrentIndex(2);
            }
        }
        else if (rawLowerCount == 3 && rawUpperCount == 0) {
            // Caso 3 minuscole: Solo Vincoli attivi
            needsConstraint = true;
            ui->panelImplicit->setTabEnabled(0, true);
            ui->panelImplicit->setTabEnabled(1, false); // RIMESSO: Spegne Composition
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false); // RIMESSO: Spegne Geodesic

            // Se la tab corrente è disabilitata, allora sposta il focus su Constraints
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                ui->panelImplicit->setCurrentIndex(0);
            }
        }
        else if (rawUpperCount == 3 && rawLowerCount == 0) {
            // Caso 3 maiuscole: Esclusione mutua tra Composition e Geodesic
            ui->panelImplicit->setTabEnabled(0, false); // Vincoli sempre OFF

            if (geoHasText) {
                // Sto scrivendo in Geodesic: blocco Composition
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, false);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }
            else if (compHasText) {
                // Sto scrivendo in Composition: blocco Geodesic
                needsComposition = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);
            }
            else {
                // Entrambi vuoti: entrambi abilitati e pronti all'uso
                needsComposition = true;
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }

            // Gestione Focus dolce
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                if (compHasText) ui->panelImplicit->setCurrentIndex(1);
                else if (geoHasText) ui->panelImplicit->setCurrentIndex(2);
                else ui->panelImplicit->setCurrentIndex(1); // Default su Composition
            }
        }
        else {
            // Qualsiasi altra combinazione (incluse solo 2 maiuscole/minuscole): TUTTO SPENTO
            ui->panelImplicit->setTabEnabled(0, false);
            ui->panelImplicit->setTabEnabled(1, false);
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);

            // Con tutti i tab spenti (es. superficie parametrica normale solo
            // u,v) mostra COMUNQUE Constraints, non Geodesic Flow: cosi' non
            // resta in vista il Conformal Factor con l'1.0 di default, inutile
            // qui. Solo estetico: i tab restano tutti disabilitati.
            ui->panelImplicit->setCurrentIndex(0);
        }
    }

    // 2.5 In modalità metrica i campi X/Y/Z/P restano editabili come mappa di
    // visualizzazione (embedding): vuoti => carta identità, oppure una mappa
    // esplicita z=±f(U) per piegare il piano (es. Flamm). Non vanno disabilitati.

    // 3. Applica stato fisico ai campi dei Vincoli
    ui->lineExplicitU->setEnabled(needsConstraint);
    ui->lineExplicitV->setEnabled(needsConstraint);
    ui->lineExplicitW->setEnabled(needsConstraint);

    // 4. Applica stato fisico ai campi Composition
    ui->lineU->setEnabled(needsComposition && hasRaw_U);
    ui->lineV->setEnabled(needsComposition && hasRaw_V);
    ui->lineW->setEnabled(needsComposition && hasRaw_W);

    // 5. Applica stato fisico ai campi Geodesic
    if (ui->lnU) {
        ui->lnU->setEnabled(needsGeodesic);
        ui->lnV->setEnabled(needsGeodesic);
        ui->lnW->setEnabled(needsGeodesic);
        ui->lndU->setEnabled(needsGeodesic);
        ui->lndV->setEnabled(needsGeodesic);
        ui->lndW->setEnabled(needsGeodesic);
    }

    // 6. ANALISI COMPOSTA: Accensione degli Slider (Rigidamente Case-Sensitive!)
    QString allEqs = mainEqs + " " + eqExplU + " " + eqExplV + " " + eqExplW;
    QString composedAllEqs = composeEquation(allEqs, defU, defV, defW);

    bool isGeodesicActive = (rawUpperCount > 0) && geoHasText && (!implicitMode());

    updateConstraintState();
    updateConstantsUIState();
}

void MainWindow::updateConstraintState()
{
    QString txtU = m_scene.eq.explicitU.trimmed();
    QString txtV = m_scene.eq.explicitV.trimmed();
    QString txtW = m_scene.eq.explicitW.trimmed();

    bool hasConstraintU = !txtU.isEmpty();
    bool hasConstraintV = !txtV.isEmpty();
    bool hasConstraintW = !txtW.isEmpty();

    QString defU = m_scene.eq.u;
    QString defV = m_scene.eq.v;
    QString defW = m_scene.eq.w;

    QString allMainEqs = m_scene.eq.x + " " +
                         m_scene.eq.y + " " +
                         m_scene.eq.z + " " +
                         m_scene.eq.p;

    QString composedEqs = composeEquation(allMainEqs, defU, defV, defW);

    bool usesU = composedEqs.contains(kReLowerU);
    bool usesV = composedEqs.contains(kReLowerV);
    bool usesW = composedEqs.contains(kReLowerW);

    // --- Disabilitazione e svuotamento Limiti W in modalità Geodesic Flow / Composition ---
    int upperCount = (allMainEqs.contains(kReUpperU) ? 1 : 0) +
            (allMainEqs.contains(kReUpperV) ? 1 : 0) +
            (allMainEqs.contains(kReUpperW) ? 1 : 0);

    bool geoHasText = hasGeodesicText();

    bool isGeodesicActive = (upperCount > 0) && geoHasText && (!implicitMode());

    // Aggiungiamo la rilevazione per la modalità Composition
    bool isCompositionActive = (upperCount > 0 || !defU.trimmed().isEmpty() || !defV.trimmed().isEmpty() || !defW.trimmed().isEmpty()) && !geoHasText && (!implicitMode());

    if (isGeodesicActive || isCompositionActive) {
        usesW = false;
    }

    // Modalità metrica: u e v parametrizzano il fascio di geodetiche e il
    // tempo di integrazione, non la mappa di visualizzazione. I limiti devono
    // restare attivi anche se la mappa non cita una delle variabili (es. una
    // carta alla Flamm senza V), altrimenti si svuotano e il flusso non parte.
    if (!m_metricScriptBody.trimmed().isEmpty() &&
            !implicitMode()) {
        usesU = true;
        usesV = true;
        usesW = false;
    }

    // Superficie da SCRIPT (non metrico): la geometria e' getRawPosition(u, v)
    // dello script e X/Y/Z/P sono vuoti, quindi "le equazioni non citano u" non
    // vuol dire che u non serva. Svuotare qui i limiti li cancellava al load di
    // un record da script dopo uno METRICO (exitMetricScriptMode rilancia questa
    // funzione): Clifford Labyrinth e i tori di Hopf si salvavano a 0, a seconda
    // del preset aperto prima. Lo nascondeva il load, che riapplicava le
    // direttive "u_max := ..." dello script dopo.
    const QString scriptApplied = stripCodeComments(m_scene.surfaceScriptApplied);
    if (!implicitMode() && allMainEqs.trimmed().isEmpty() && !scriptApplied.trimmed().isEmpty()) {
        usesU = true;
        usesV = true;
        usesW = usesW || scriptApplied.contains(kReLowerW);
    }

    auto applyLimitsState = [](QLineEdit* minEdit, QLineEdit* maxEdit, bool enable) {
        minEdit->setEnabled(enable);
        maxEdit->setEnabled(enable);
        if (!enable) {
            minEdit->clear();
            maxEdit->clear();
        }
    };

    // Sotto-tab TUBES: u e' il parametro della curva; i suoi limiti si
    // accendono, come in Surface, solo se la curva lo cita (NEW li spegne), o
    // se la curva la da' uno script (come per le superfici qui sopra).
    // v e w non si vedono e restano come sono (sono di Surface). Vincoli e
    // composizione non c'entrano col tubo: il motore li ignora (runSceneTube).
    if (tubesShown()) {
        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, tubeCurveUsesU() || tubeSceneFromScript());
        return;
    }

    if (hasConstraintU) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, false);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
    }
    else if (hasConstraintV) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, false);
        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
    }
    else {
        if (hasConstraintW) {
            UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Active);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);

            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, false);
        } else {
            if (usesW) {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Default);
            } else {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Disabled);
            }
            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);
        }

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
    }
}

// Le costanti che NON appartengono alla superficie: non citate dalle equazioni
// (o dall'equazione implicita in RM), ne' dai campi geodetici, dai limiti u/v/w
// o dai path camera. Sono quelle che un caricamento di texture puo' riportare al
// default senza deformare nulla di cio' che e' gia' a schermo.
//
// Il criterio e' lo STESSO di updateConstantsUIState -- mathText, match
// case-insensitive sui campi exprtk, commenti esclusi -- perche' li' una
// costante citata da un limite o da un path e' "usata" e non va toccata: la
// stessa ragione vale qui, dove il rischio non e' uno slider spento a torto ma
// una superficie che cambia forma da sola.
QSet<QString> MainWindow::constantsNotUsedBySurface() const
{
    QString mathText;

    if (!implicitMode()) {
        mathText = m_scene.eq.x + " " + m_scene.eq.y + " " +
                   m_scene.eq.z + " " + m_scene.eq.p + " " +
                   m_scene.eq.explicitU + " " + m_scene.eq.explicitV + " " +
                   m_scene.eq.explicitW + " " + m_scene.eq.u + " " +
                   m_scene.eq.v + " " + m_scene.eq.w;
        if (ui->lnU) {
            mathText += " " + m_scene.eq.geoU + " " + m_scene.eq.geoV +
                        " " + m_scene.eq.geoW + " " + m_scene.eq.geoDU +
                        " " + m_scene.eq.geoDV + " " + m_scene.eq.geoDW +
                        " " + m_scene.eq.conform;
        }
        // Lo SCRIPT della superficie parametrica e' GLSL, ma e' comunque della
        // SUPERFICIE: una costante che vi compare non va resettata.
        mathText += " " + stripCodeComments(m_scene.surfaceScriptText + "\n" + m_scene.surfaceScriptApplied);
        // La curva dei TUBI anche: senza, caricando una texture le costanti
        // della curva tornavano a 1 (Clifford Knot Rotating diventava un anello).
        mathText += " " + tubeConstantSource();
    } else {
        // Sotto-tab ATTIVO, non lineEquation fisso: il criterio dev'essere lo
        // STESSO di updateConstantsUIState (vedi il commento sopra), che legge
        // gia' l'equazione del sotto-tab a schermo. Divergendo, le costanti usate
        // solo dal Cross Section (A/B/C del T^3) risultavano "non usate dalla
        // superficie" e un caricamento di texture le riportava al default,
        // deformando la superficie 4D a schermo.
        mathText = stripCodeComments(activeImplicitEquationText());
        // Idem per lo script implicito in Ray Marching.
        mathText += " " + stripCodeComments(m_scene.surfaceScriptText + "\n" + m_scene.surfaceScriptApplied);
    }

    // Limiti e path: valutati con A..F/S registrate, quindi contano come uso.
    mathText += " " + m_scene.lim.uMin + " " + m_scene.lim.uMax +
                " " + m_scene.lim.vMin + " " + m_scene.lim.vMax +
                " " + m_scene.lim.wMin + " " + m_scene.lim.wMax;
    // Stessa regola per il taglio x/y/z del Ray Marching.
    mathText += " " + m_scene.lim.xMin + " " + m_scene.lim.xMax +
                " " + m_scene.lim.yMin + " " + m_scene.lim.yMax +
                " " + m_scene.lim.zMin + " " + m_scene.lim.zMax;
    mathText += " " + m_scene.path.x + " " + m_scene.path.y +
                " " + m_scene.path.z + " " + m_scene.path.p +
                " " + m_scene.path.alpha + " " + m_scene.path.beta +
                " " + m_scene.path.gamma +
                " " + m_scene.path.x3D + " " + m_scene.path.y3D +
                " " + m_scene.path.z3D + " " + m_scene.path.roll3D;

    // TEXTURE DELLE FASCE: non sono la superficie, ma le costanti che usano non
    // vanno riportate al default quando si carica una texture GLOBALE -- le
    // fasce continuano a disegnare, e se ne ritroverebbero una cambiata (la F
    // della densita' di Static Holography sui labirinti di Clifford). Sono GLSL:
    // regola case-sensitive, non il match di mathText.
    const QStringList meshCodes = meshTextureCodesForConstants();

    QSet<QString> free;
    for (const QString &letter : { QStringLiteral("A"), QStringLiteral("B"),
                                   QStringLiteral("C"), QStringLiteral("D"),
                                   QStringLiteral("E"), QStringLiteral("F"),
                                   QStringLiteral("S") }) {
        QRegularExpression re("\\b" + letter + "\\b",
                              QRegularExpression::CaseInsensitiveOption);
        if (mathText.contains(re)) continue;
        bool byMesh = false;
        for (const QString &c : meshCodes)
            if (glslUsesConstant(c, letter)) { byMesh = true; break; }
        if (!byMesh) free.insert(letter);
    }
    return free;
}

// Le texture delle FASCE contano come "uso" delle costanti, esattamente come la
// texture globale. Prima non contavano: una costante citata SOLO da una fascia
// finiva nel ramo !used di updateConstantsUIState, che la riporta a 1 e spegne
// lo slider. Emerso spostando il raggio dei Clifford Labyrinth da F ad A: F
// restava della sola Static Holography delle fasce, e all'apertura del record
// tornava a 1 (densita' diversa da quella salvata) con lo slider bloccato.
// TUTTE le parti con texture propria, anche spente e anche in ambito "All" (dove
// sono sospese): tornano a disegnare con i valori di adesso, e azzerarli ora
// vorrebbe dire ritrovarle cambiate. Al peggio uno slider resta acceso senza
// effetto visibile, mai il contrario.
// Anche m_pendingMeshParts: durante il load di un record le parti possono essere
// ancora li' (applyPendingMeshAppearance non e' passata), e gli slider si
// ricalcolano prima. Il vettore si svuota appena applicato, quindi non e' mai
// il residuo di un record precedente.
// Solo in parametrico: in Ray Marching le fasce non esistono.
QStringList MainWindow::meshTextureCodesForConstants() const
{
    QStringList out;
    if (implicitMode()) return out;
    auto add = [&out](const MeshPart &mp) {
        if (mp.hasCustomTexture && !mp.textureCode.trimmed().isEmpty())
            out << stripCodeComments(mp.textureCode);
    };
    if (ui->glWidget && ui->glWidget->getEngine())
        for (const MeshPart &mp : ui->glWidget->getEngine()->getMeshParts()) add(mp);
    for (const MeshPart &mp : m_pendingMeshParts) add(mp);
    // La texture del BORDO, se c'e' (bordo acceso e texture accesa).
    if (ui->glWidget && ui->glWidget->borderRadius() > 0.0f && ui->glWidget->borderTextureEnabled()
        && !ui->glWidget->borderTextureCode().trimmed().isEmpty())
        out << stripCodeComments(ui->glWidget->borderTextureCode());
    return out;
}

// GLSL: conta solo la MAIUSCOLA (il case delle costanti iniettate), e solo se la
// lettera non e' dichiarata come variabile dello shader, singola ("vec2 F =
// fragCoord") o in lista ("float i = 0.0, S = 0.0"): li' e' la locale, non la
// costante. Dichiarazioni con inizializzatori a chiamata di funzione in lista
// sfuggono al pattern: al peggio lo slider resta attivo (status quo), mai il
// contrario.
bool MainWindow::glslUsesConstant(const QString &glsl, const QString &letter)
{
    const QRegularExpression reGlsl("\\b" + letter + "\\b");
    if (!glsl.contains(reGlsl)) return false;
    const QRegularExpression reDecl(
        "\\b(?:float|int|uint|bool|vec[234]|mat[234])\\s+"
        "(?:\\w+\\s*(?:=[^,;()]*)?,\\s*)*" + letter + "\\b");
    return !glsl.contains(reDecl);
}

void MainWindow::updateConstantsUIState() {
    // Due nature di testo, due regole di match:
    // - mathText: campi exprtk (equazioni, geodetica, path). Case-INSENSITIVE
    //   come sempre: 'a' e 'A' sono la stessa costante.
    // - glslBlocks: codice shader (texture, sfondo, editor script GLSL). Nel
    //   GLSL le costanti sono iniettate come 'float A..F' e 'S' (case-
    //   sensitive): le minuscole a..f di uno shader NON sono mai le costanti,
    //   e una lettera DICHIARATA come variabile locale (es. 'vec2 F =
    //   fragCoord') e' la locale che ombreggia la costante, non un uso. Il
    //   vecchio match unico case-insensitive accendeva gli slider a vuoto su
    //   record senza costanti (float a/b/c/d/s locali negli shader).
    QString mathText = "";
    // Un BLOCCO per codice, non un testo unico: una lettera dichiarata come
    // locale in uno shader ("vec2 F = fragCoord") non e' la costante LI', ma
    // non cancella l'uso della costante in un altro. Concatenati, la locale di
    // una texture del record PRECEDENTE (fasce ancora nel motore durante il
    // load) nascondeva la F usata dalle fasce di Clifford Labyrinth 3-Tubes
    // Fibers, che si apriva con F = 1 (round-trip).
    QStringList glslBlocks;
    // MODIFICHE DELL'UTENTE NON ANCORA ESEGUITE. Quando il ricalcolo parte da
    // una digitazione (il segnale di un campo di testo, fuori dal riempimento
    // di un preset), cio' che e' scritto non e' piu' cio' che e' a schermo: da
    // qui al prossimo Run conta anche l'applicato (vedi piu' sotto). Il flag
    // lo spengono refreshConstants -- cioe' load, reset, scelte in Library e
    // Run riuscito -- perche' li' campi e schermo tornano a coincidere.
    // I campi multilinea emettono textChanged solo per la digitazione (il
    // programma li scrive a segnali bloccati); quelli a una riga (limiti, path)
    // anche per setText e clear, quindi per loro lo segna il loro textEdited,
    // che Qt emette PRIMA di textChanged.
    if (m_uiReady && qobject_cast<QPlainTextEdit *>(sender()))
        m_constantsEditPending = true;
    // Ray Marching con il campo equazione CANCELLATO: il ramo !used disabilita
    // le costanti ma NON le resetta (vedi la nota dov'e' usata).
    bool equationFieldIsEmpty = false;
    int currentTab = implicitMode() ? 1 : 0;

    // 1. RACCOLTA TESTO SPECIFICA PER TAB
    if (currentTab == 0) { // MODALITÀ PARAMETRICA
        mathText = m_scene.eq.x + " " + m_scene.eq.y + " " +
                   m_scene.eq.z + " " + m_scene.eq.p + " " +
                   m_scene.eq.explicitU + " " + m_scene.eq.explicitV + " " +
                   m_scene.eq.explicitW + " " + m_scene.eq.u + " " +
                   m_scene.eq.v + " " + m_scene.eq.w;

        // Sotto-tab Tubes: la curva (e lo spessore) usano le costanti come
        // le equazioni della superficie.
        mathText += " " + tubeConstantSource();

        if (ui->lnU) { // Campi Geodetici (Tab 0)
            mathText += " " + m_scene.eq.geoU +
                    " " + m_scene.eq.geoV +
                    " " + m_scene.eq.geoW +
                    " " + m_scene.eq.geoDU +
                    " " + m_scene.eq.geoDV +
                    " " + m_scene.eq.geoDW +
                    " " + m_scene.eq.conform;
        }
        // In parametrica aggiungiamo lo script della superficie se non siamo in Ray Marching
        glslBlocks << stripCodeComments(m_scene.surfaceTextureCode);
        // ...e le texture delle FASCE: una costante citata solo da una di loro e'
        // usata quanto una della texture globale (vedi
        // meshTextureCodesForConstants). Blocco per blocco, come qui sotto.
        for (const QString &c : meshTextureCodesForConstants())
            glslBlocks << c;

        // CIO' CHE E' A SCHERMO, oltre a cio' che e' scritto. Una costante cade
        // in disuso -- e torna al valore neutro -- solo quando non la usa ne'
        // l'applicato ne' il testo dei campi. Guardando i soli campi, riscrivere
        // un'equazione senza B la riportava a 1 PRIMA del Run, con a schermo la
        // superficie che B la usava ancora; rimessa B nell'equazione, il suo
        // valore era perso (deciso con l'utente il 2026-10-01, trovato dal test
        // degli scenari). Le fonti: le equazioni compilate nel motore
        // (composizione compresa) e lo snapshot dell'ultimo Run per il flusso
        // geodetico. Lo script di superficie e' gia' coperto piu' sotto dal suo
        // slot m_scene.surfaceScriptText.
        // SOLO con modifiche in sospeso: a campi e schermo allineati l'applicato
        // non aggiunge nulla, e durante un load e' ancora quello del preset di
        // PRIMA (in modo script le equazioni compilate restano le vecchie; il
        // load di una superficie le applica in differita) -- contarlo sempre
        // rendeva le costanti dipendenti dal preset caricato prima (round-trip).
        if (m_constantsEditPending) {
            mathText += " " + activeEquationsText();
            const bool scriptSurface = ui->glWidget && ui->glWidget->getEngine()
                                       && ui->glWidget->getEngine()->isScriptModeActive();
            if (ui->glWidget && !scriptSurface)
                glslBlocks << ui->glWidget->parametricEquationsApplied();
        }
        // ...e lo script della texture non ancora eseguito (l'applicato e'
        // m_scene.surfaceTextureCode, qui sopra).
        glslBlocks << stripCodeComments(m_scene.surfaceTextureScriptText);
    }
    else { // MODALITÀ RAY MARCHING
        // L'equazione implicita e' matematica utente (translateEquation);
        // texture e variations sono snippet GLSL. Sorgente dell'equazione
        // dipende dal sotto-tab attivo (3D vs Cross Section, stato separato
        // — vedi CLAUDE.md), altrimenti le costanti usate SOLO nell'equazione
        // Cross Section (es. A/B/C del T^3) risultavano "non usate" e
        // updateControl le azzerava/disabilitava a vuoto.
        mathText = stripCodeComments(activeImplicitEquationText());
        // Equazione CANCELLATA: lo si registra PRIMA di concatenare limiti e
        // campi path, che renderebbero mathText non vuoto anche senza
        // equazione. Serve al ramo !used piu' sotto, che senza questo
        // riscriveva le costanti a 1 su una superficie ancora a schermo.
        // Solo in Ray Marching: qui l'equazione e' UNA, e vuota vuol dire
        // "non c'e' niente da cui dedurre". In parametrico le equazioni sono
        // molte e svuotarne una non ha lo stesso significato.
        equationFieldIsEmpty = mathText.trimmed().isEmpty();
        glslBlocks << stripCodeComments(m_scene.rm.texture)
                   << stripCodeComments(m_scene.rm.displacement);
        // L'APPLICATO, come nel ramo parametrico e con la stessa condizione:
        // equazione, texture e rilievo compilati nel marcher.
        if (m_constantsEditPending && ui->glWidget) {
            const bool scriptSurface = ui->glWidget->getEngine()
                                       && ui->glWidget->getEngine()->isScriptModeActive();
            if (!scriptSurface)
                mathText += " " + stripCodeComments(ui->glWidget->activeImplicitEquation());
            glslBlocks << stripCodeComments(ui->glWidget->currentTextureCode())
                       << stripCodeComments(ui->glWidget->currentDisplacementCode());
        }
    }

    // 2. AGGIUNGI CAMPI SEMPRE ATTIVI (Shared)
    // I COMMENTI non contano come "uso" di una costante: un commento italiano
    // basta ad accendere uno slider a vuoto (es. "Se c'è il segmento" in una
    // texture -> l'elisione c' viene presa per la costante C nel match
    // case-insensitive). Strip PER BLOCCO e non sull'insieme: i blocchi sono
    // concatenati con spazi, e un commento di linea non terminato a fine
    // blocco inghiottirebbe l'inizio del blocco successivo (falso negativo:
    // costante vera creduta inutilizzata -> reset a 1).
    glslBlocks << stripCodeComments(m_scene.bgTextureCode); // Lo sfondo è comune
    // ...con il suo script non ancora eseguito (l'applicato e' la riga sopra).
    glslBlocks << stripCodeComments(m_scene.bgTextureScriptText);

    // L'EDITOR E' SEMPRE GLSL. Vale per tutti e tre i modi dello script
    // (superficie, texture, sound) e per entrambe le tab: lo script di
    // superficie in parametrica e' GLSL con "return vec4(...)", quello in ray
    // marching e' GLSL implicito, e gli script metrici restituiscono mat3.
    // Non esiste uno script d'editor in sintassi exprtk.
    //
    // Perche' la distinzione conta: mathText matcha CASE-INSENSITIVE e non
    // riconosce le dichiarazioni locali. Mandandoci il GLSL, una 'a' minuscola
    // dello shader ("float a = 3.0;" nei tubi) accendeva lo slider A a vuoto:
    // sbloccato ma inerte, perche' nessuna costante e' davvero usata. Riguardava
    // 12 preset (Trefoil Tube, Clover Tube, i Clifford Labyrinth, i record
    // Clover/Trefoil/Tube). E' la stessa famiglia di falsi positivi descritta
    // sopra per gli shader; la protezione non arrivava fin qui.
    //
    // Nei blocchi GLSL il match e' case-sensitive (le costanti sono iniettate come
    // "float A..F") ed esclude le lettere dichiarate come variabili locali:
    // coerente con generateGlslHelperVars(), che davanti a "float A" non
    // inietta affatto la costante, quindi li' A e' la locale e lo slider non ha
    // modo di influenzarla.
    glslBlocks << stripCodeComments(scriptText(shownScriptSlot()));

    // LO SCRIPT DELLA SUPERFICIE, scritto e applicato, qualunque modulo il dock
    // stia mostrando (la riga sopra conta solo lo slot in vista).
    // L'editor mostra UN modulo per volta (superficie, texture o suono) e in
    // ambito Mesh la texture della fascia: appena mostra altro, lo script
    // parametrico spariva da questo testo e le costanti citate SOLO li'
    // finivano nel ramo !used, che non si limita a bloccarle -- SCRIVE 1 nel
    // campo. Su Clifford 6-Tubes, dove E (quanti tubi) e F (raggio) vivono solo
    // nello script, bastava passare alla tab Texture o selezionare una mesh per
    // ritrovarsi E=1, F=1: un tubo solo e raggio degenere, cioe' la superficie
    // "collassata" -- e un salvataggio in quello stato scriveva i valori
    // sbagliati nel record.
    glslBlocks << stripCodeComments(m_scene.surfaceScriptText)
               << stripCodeComments(m_scene.surfaceScriptApplied);

    // Anche i path camera 4D/3D valgono come "uso" di una costante: le loro
    // espressioni sono compilate su exprtk con A..F/s registrate
    // (m_pathSymbolTable in SurfaceEngine), quindi una costante citata solo da
    // un path deve restare sbloccata e NON essere resettata dal ramo !used.
    // Anche i limiti U/V/W valgono come "uso": sono valutati da parseLimitField
    // con A..F/S registrate, quindi "uMax = 2*A" deve tenere A sbloccata (e
    // soprattutto NON farla resettare a 1 dal ramo !used, che cambierebbe
    // l'estensione della superficie di sorpresa). Lo stesso il taglio x/y/z
    // del Ray Marching.
    mathText += " " + m_scene.lim.uMin + " " + m_scene.lim.uMax +
                " " + m_scene.lim.vMin + " " + m_scene.lim.vMax +
                " " + m_scene.lim.wMin + " " + m_scene.lim.wMax;
    mathText += " " + m_scene.lim.xMin + " " + m_scene.lim.xMax +
                " " + m_scene.lim.yMin + " " + m_scene.lim.yMax +
                " " + m_scene.lim.zMin + " " + m_scene.lim.zMax;

    mathText += " " + m_scene.path.x + " " + m_scene.path.y +
                " " + m_scene.path.z + " " + m_scene.path.p +
                " " + m_scene.path.alpha + " " + m_scene.path.beta +
                " " + m_scene.path.gamma +
                " " + m_scene.path.x3D + " " + m_scene.path.y3D +
                " " + m_scene.path.z3D + " " + m_scene.path.roll3D;

    // 2b. CIO' CHE E' A SCHERMO, da solo. Con modifiche in sospeso uno slider
    //     SPENTO si accende soltanto se la costante la usa gia' quello che si
    //     vede: una lettera appena scritta in un'equazione non ancora eseguita
    //     sbloccava lo slider, che pero' non muoveva nulla fino al Run (deciso
    //     con l'utente il 2026-10-07: bloccati fino al Run). Le stesse fonti
    //     dell'applicato qui sopra, piu' i path compilati (Departure e Invio li
    //     applicano senza Run) e la curva del tubo nel motore.
    QString appliedMath;
    QStringList appliedGlsl;
    if (m_constantsEditPending && ui->glWidget) {
        SurfaceEngine *eng = ui->glWidget->getEngine();
        const bool scriptSurface = eng && eng->isScriptModeActive();
        if (currentTab == 0) {
            if (eng && eng->isTubeModeActive() && !scriptSurface) {
                appliedMath = eng->tubeX() + " " + eng->tubeY() + " " + eng->tubeZ() + " "
                            + eng->tubeP();
            } else if (scriptSurface) {
                appliedGlsl << stripCodeComments(m_scene.surfaceScriptApplied);
            } else {
                appliedMath = activeEquationsText();
                appliedGlsl << ui->glWidget->parametricEquationsApplied();
            }
            appliedGlsl << stripCodeComments(m_scene.surfaceTextureCode);
            for (const QString &c : meshTextureCodesForConstants())
                appliedGlsl << c;
        } else {
            if (scriptSurface) appliedGlsl << stripCodeComments(m_scene.surfaceScriptApplied);
            else appliedMath = stripCodeComments(ui->glWidget->activeImplicitEquation());
            appliedGlsl << stripCodeComments(ui->glWidget->currentTextureCode())
                        << stripCodeComments(ui->glWidget->currentDisplacementCode());
        }
        appliedGlsl << stripCodeComments(m_scene.bgTextureCode);
        if (eng) appliedMath += " " + eng->appliedPath4D().join(QLatin1Char(' '))
                              + " " + eng->appliedPath3D().join(QLatin1Char(' '));
    }

    // 3. LOGICA DI BLOCCO/SBLOCCO E RESET
    bool resetToNeutral = false;
    auto updateControl = [&](ConstField field) {
        const QString letter = constantName(field);
        QSlider *slider = constantSlider(field);
        QLineEdit *line = constantFieldEdit(field);
        const bool wasEnabled = slider->isEnabled();

        bool used = false;

        // FIX FONDAMENTALE: In Ray Marching (tab 1), "S" funge da Step Relax!
        // Deve rimanere sempre attivo e NON deve mai essere resettato a 0,
        // altrimenti i raggi si congelano causando glitch grafici e cerchi concentrici.
        // Vale per ENTRAMBI i marcher: anche in Precise lo Step Relax governa
        // l'avvicinamento (vedi il commento su Ray Steps in updateRenderState).
        if (currentTab == 1 && letter == "S") {
            used = true;
        } else {
            QRegularExpression reMath("\\b" + letter + "\\b", QRegularExpression::CaseInsensitiveOption);
            used = mathText.contains(reMath);

            if (!used) {
                // GLSL: maiuscola e non dichiarata come locale (vedi
                // glslUsesConstant, unica sede della regola).
                for (const QString &block : glslBlocks)
                    if ((used = glslUsesConstant(block, letter))) break;
            }
        }

        if (!used) {
            // CAMPO EQUAZIONE VUOTO: si DISABILITA ma non si resetta. "Nessuna
            // costante citata" qui non vuol dire "nessuna costante serve": vuol
            // dire che non c'e' ancora un'equazione da cui dedurlo, e il valore
            // corrente e' l'unica informazione buona che abbiamo.
            // Sintomo: caricata la default del Cross Section (T^3, che vive su
            // A=0.9 B=0.4 C=0.2) e cancellata l'equazione, il textChanged
            // arrivava qui e riscriveva A=B=C=1 -- valori per cui il T^3
            // DEGENERA (serve A>B>C). Nessun rebuildShader, perche' le costanti
            // sono uniform: la superficie a schermo cambiava forma da sola,
            // restando quella "deformata" anche dopo il popup di equazione
            // mancante. In 3D non si notava: la sfera di default non usa
            // costanti, quindi non c'era niente da rovinare.
            // Il reset resta per il caso vero -- equazione presente che NON cita
            // quella costante.
            if (!equationFieldIsEmpty) {
                // RESET: S a 0.0, le altre (A-F) a 1.0 (lo slider lo
                // riallinea refreshConstantSliders, in fondo).
                const QString neutral = letter == "S" ? QStringLiteral("0") : QStringLiteral("1");
                if (m_scene.constants.*field != neutral) {
                    setConstText(field, neutral);
                    resetToNeutral = true;
                }
            }

            slider->setEnabled(false);
            line->setEnabled(false);
        } else {
            // Usata. Senza modifiche in sospeso (load, reset, Run riuscito)
            // scritto e schermo coincidono: si accende. Con modifiche in
            // sospeso si accende solo se era gia' accesa o se la usa cio' che
            // e' a schermo (2b); altrimenti resta bloccata fino al Run, che
            // azzera il sospeso e rifa' il giudizio (refreshConstants).
            bool onScreen = !m_constantsEditPending || wasEnabled || (currentTab == 1 && letter == "S");
            if (!onScreen) {
                QRegularExpression reMath("\\b" + letter + "\\b", QRegularExpression::CaseInsensitiveOption);
                onScreen = appliedMath.contains(reMath);
                for (const QString &block : appliedGlsl)
                    if (!onScreen && glslUsesConstant(block, letter)) onScreen = true;
            }
            slider->setEnabled(onScreen);
            line->setEnabled(onScreen);
            // CASCATA: il campo di una costante IN USO puo' essere
            // un'espressione delle precedenti ("A/10"): quelle lettere sono
            // usate tramite lei. Senza, con B = A/10 e le equazioni che citano
            // la sola B, A veniva dichiarata in disuso e riportata a 1 -- e B,
            // cioe' la superficie, cambiava con lei (test degli scenari).
            mathText += " " + m_scene.constants.*field;
            // ...e a schermo, se lei lo e'.
            if (onScreen) appliedMath += " " + m_scene.constants.*field;
        }
    };

    // Dall'ULTIMA alla prima: la cascata va solo in avanti (B puo' citare A, S
    // tutte le altre), quindi quando si giudica una lettera i campi di quelle
    // che possono citarla sono gia' stati aggiunti al testo.
    const auto &fields = constantFields();
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) updateControl(*it);
    if (resetToNeutral) refreshConstantSliders();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool MainWindow::applySpaceLimits(bool notify, bool reapply)
{
    if (!ui->glWidget) return true;

    // Campo vuoto = nessun taglio su quel lato: si usa il default largo, non un
    // errore. Un campo NON valutabile invece ferma tutto: applicare gli assi
    // buoni e saltare quello rotto darebbe un taglio a meta', senza dire perche'.
    // Come i limiti u/v/w, ammettono le costanti A..F/S (parseLimitField).
    auto readField = [this, notify](QLineEdit* edit, const QString& txt, float def,
                                    const QString& axis, const QString& side,
                                    bool* good) -> float {
        *good = true;
        if (txt.trimmed().isEmpty()) return def;
        bool ok = false;
        const float v = parseLimitField(txt, &ok);
        if (!ok) {
            *good = false;
            if (notify && !m_constantPopupActive) {
                m_constantPopupActive = true;
                InputValidator::showInvalidLimitError(this, axis + " " + side, txt);
                edit->setFocus();
                edit->selectAll();
                m_constantPopupActive = false;
            }
            return def;
        }
        return v;
    };

    struct Axis {
        QLineEdit* lo;
        QLineEdit* hi;
        void (GLWidget::*setter)(float, float);
        const char* name;
    };
    const Axis axes[] = {
        { ui->lineXMin, ui->lineXMax, &GLWidget::setRangeX, "X" },
        { ui->lineYMin, ui->lineYMax, &GLWidget::setRangeY, "Y" },
        { ui->lineZMin, ui->lineZMax, &GLWidget::setRangeZ, "Z" },
    };

    // DUE PASSATE: prima si legge e valida tutto, poi si applica. Cosi' un
    // errore sull'asse Z non lascia X e Y gia' tagliati con la scena in uno
    // stato intermedio che nessun campo descrive.
    // I TESTI: al Run quelli scritti; con reapply quelli dell'ultimo taglio
    // applicato, rivalutati perche' sono cambiate le costanti (vedi
    // refreshLimitsFromConstants). Lo scritto dopo il Run aspetta il Run.
    QString txt[6];
    for (int i = 0; i < 3; ++i) {
        txt[2 * i]     = reapply ? m_spaceLimitsApplied[2 * i]     : lineText(axes[i].lo);
        txt[2 * i + 1] = reapply ? m_spaceLimitsApplied[2 * i + 1] : lineText(axes[i].hi);
    }

    float lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        bool okLo = true, okHi = true;
        lo[i] = readField(axes[i].lo, txt[2 * i], -1000.0f, axes[i].name, "min", &okLo);
        if (!okLo) return false;
        hi[i] = readField(axes[i].hi, txt[2 * i + 1], 1000.0f, axes[i].name, "max", &okHi);
        if (!okHi) return false;

        // Intervallo impossibile: si lascia l'asse com'e' (nessun taglio nuovo),
        // senza popup -- e' lo stesso trattamento che aveva prima.
        if (lo[i] >= hi[i]) { lo[i] = -1000.0f; hi[i] = 1000.0f; }
    }

    for (int i = 0; i < 3; ++i) {
        (ui->glWidget->*axes[i].setter)(lo[i], hi[i]);
    }
    if (!reapply) std::copy(txt, txt + 6, m_spaceLimitsApplied);
    return true;
}

bool MainWindow::updateULimits() {
    float lo = parseLimitField(m_scene.lim.uMin);
    float hi = parseLimitField(m_scene.lim.uMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    uMin = lo; uMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeU(uMin, uMax);
    return true;
}

bool MainWindow::updateVLimits() {
    // Tubo: v e' l'angolo attorno alla curva, sempre 0..2pi (i campi v non si
    // vedono e restano di Surface).
    if (tubesShown()) {
        if (ui->glWidget) ui->glWidget->setRangeV(0.0f, 6.28318530718f);
        return true;
    }
    float lo = parseLimitField(m_scene.lim.vMin);
    float hi = parseLimitField(m_scene.lim.vMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    vMin = lo; vMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeV(vMin, vMax);
    return true;
}

bool MainWindow::updateWLimits() {
    // Tubo: w non c'entra.
    if (tubesShown()) {
        if (ui->glWidget) ui->glWidget->setRangeW(0.0f, 1.0f);
        return true;
    }
    float lo = parseLimitField(m_scene.lim.wMin);
    float hi = parseLimitField(m_scene.lim.wMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    wMin = lo; wMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeW(wMin, wMax);
    return true;
}

// Le costanti A..F/S sono cambiate: i limiti u/v/w CONFERMATI che le citano
// ("uMax = A", "6.28/C") vanno rivalutati e registrati di nuovo, come le
// equazioni che le leggono dal vivo. Senza, il dominio restava il numero
// calcolato alla conferma del campo: lo slider cambiava la superficie ma non
// la sua estensione. Il flusso geodetico rilegge i limiti a ogni ridisegno e
// l'Invio su una costante passa dal Run di servizio, che li rilegge anche lui:
// restava scoperto lo slider.
// Si salta l'asse spento, quello con una digitazione in attesa (il dominio
// cambia alla conferma del campo, non mentre si scrive) e quello con un campo
// vuoto o illeggibile, che la conferma ha rifiutato: il parse del vuoto passa
// (0.0) e lo registrerebbe di straforo. min >= max lo rifiuta updateU/V/WLimits.
void MainWindow::refreshLimitsFromConstants()
{
    struct Axis { QLineEdit *lo, *hi; bool (MainWindow::*update)(); };
    const Axis axes[] = {
        { ui->uMinEdit, ui->uMaxEdit, &MainWindow::updateULimits },
        { ui->vMinEdit, ui->vMaxEdit, &MainWindow::updateVLimits },
        { ui->wMinEdit, ui->wMaxEdit, &MainWindow::updateWLimits },
    };
    for (const Axis &a : axes) {
        if (!a.lo->isEnabled() || !a.hi->isEnabled()) continue;
        if (userEditPending(a.lo)
            || userEditPending(a.hi)) continue;
        const QString loTxt = lineText(a.lo).trimmed();
        const QString hiTxt = lineText(a.hi).trimmed();
        if (loTxt.isEmpty() || hiTxt.isEmpty()) continue;
        bool okLo = false, okHi = false;
        parseLimitField(loTxt, &okLo);
        parseLimitField(hiTxt, &okHi);
        if (okLo && okHi) (this->*a.update)();
    }

    // Taglio x/y/z del Ray Marching: si applica al Run, quindi qui si
    // rivalutano i testi dell'ultimo taglio applicato, non quelli scritti dopo.
    applySpaceLimits(/*notify=*/false, /*reapply=*/true);
}

MainWindow::CascadeConstants MainWindow::resolveCascadeConstants(bool restoreTextOnNegative,
                                                                 CascadeIssues *issues)
{
    const auto &fields = constantFields();

    // v[i] resta 0 finche' la sua costante non e' stata valutata: ogni campo
    // vede solo le costanti che lo precedono.
    float v[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 7; ++i) {
        QLineEdit *edit = constantFieldEdit(fields[i]);
        bool ok = true;
        float raw = parseUIConstant(m_scene.constants.*fields[i], v[0], v[1], v[2], v[3], v[4], v[5], 0, &ok);

        if (!ok) {
            // Testo che non si valuta: vale 0 e NON diventa l'ultimo valore valido.
            if (issues && !issues->invalidEdit) {
                issues->invalidEdit = edit;
                issues->invalidName = constantName(fields[i]);
            }
        } else if (i < 6) {
            // A..F non ammettono valori negativi: si torna all'ultimo valore
            // valido. S invece li accetta (in parametrica servono per invertire
            // il tempo).
            if (raw < 0.0f) {
                const float prev = m_lastValidConst.value(edit, 1.0f);
                if (restoreTextOnNegative)
                    setConstValue(fields[i], prev);   // a segnali bloccati: niente ricorsione
                if (issues && issues->negativeName.isEmpty())
                    issues->negativeName = constantName(fields[i]);
                raw = prev;
            } else {
                m_lastValidConst[edit] = raw;
            }
        }
        v[i] = raw;
    }
    return { v[0], v[1], v[2], v[3], v[4], v[5], v[6] };
}

void MainWindow::evaluateCascade()
{
    CascadeIssues issues;
    const CascadeConstants kc = resolveCascadeConstants(/*restoreTextOnNegative=*/true, &issues);

    if (issues.invalidEdit) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showInvalidConstantError(this, issues.invalidName,
                                                     issues.invalidEdit->text());

            // Ripristino focus senza ri-emettere editingFinished
            {
                QSignalBlocker blocker(issues.invalidEdit);
                issues.invalidEdit->setFocus();
                issues.invalidEdit->selectAll();
            }
            // Reset rimandato: il flag resta true per tutto il resto
            // del ciclo di eventi (compreso il setFocus in onStartClicked)
            QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
        }
        return;
    }

    syncConstantSliders(kc);

    if (ui->glWidget) {
        setEngineConstants(kc, /*onlyIfChanged=*/false);
        refreshLimitsFromConstants();   // i limiti che citano le costanti
        refreshMeshCountFromConstants();   // e il campo Meshes, se le cita
        m_meshDebounce->start();
    }

    if (!issues.negativeName.isEmpty() && !m_constantPopupActive) {
        m_constantPopupActive = true;
        InputValidator::showNegativeConstantError(this, issues.negativeName);
        QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
    }
}

// Ritorna true se il limite e' stato ACCETTATO (false = rifiutato: numero non
// valido o min>=max). Il limite accettato viene REGISTRATO subito nell'engine;
// a schermo entra subito se la superficie e' in moto (la mesh viene rigenerata
// qui: nessun tick lo farebbe), al Run se e' ferma. Vedi il blocco finale.
bool MainWindow::commitLimitFieldOnEnter(const QString& fieldName)
{
    // Limite u/v/w confermato (Invio o uscita dal campo): si VALIDA e si
    // REGISTRA nell'engine, ma a superficie ferma non si ridisegna. I limiti
    // sono parte della definizione della superficie come X/Y/Z/P, composizioni,
    // vincoli e flusso geodetico: il modulo equazioni aspetta il Run, cosi' il
    // dominio nuovo entra in vigore INSIEME alle equazioni nuove e non si vede
    // mai la superficie vecchia tagliata dai limiti nuovi (un'immagine che non
    // corrisponde ne' a cio' che c'era ne' a cio' che si sta scrivendo).
    // A superficie IN MOTO il ridisegno non si aspetta: lo porta il tick.
    // Restano fuori da questa regola i soli parametri che non ridefiniscono la
    // superficie -- costanti A..F/S e Steps -- che si applicano all'Invio.
    // Deliberatamente NON si passa da commitFieldsOnEnter: quella strada rifa'
    // un Run intero e committerebbe anche le equazioni in corso di scrittura.
    // NB: chiamata dai filtri tastiera (che consumano il Return prima che il
    // QLineEdit possa emettere returnPressed) e da editingFinished, cosi' anche
    // il solo cambio di focus conferma il campo: l'Invio non e' obbligatorio.

    // Il campo appena editato deve essere valutabile: se contiene una formula
    // rotta ("2*", "2*Z") o una costante spenta, avvisiamo e NON registriamo,
    // lasciando in vista la superficie valida precedente.
    QLineEdit* edited = nullptr;
    QString axisLabel;
    // Etichette MINUSCOLE: sono i parametri del dominio (u, v, w). Le maiuscole
    // U/V/W nel progetto sono le variabili di COMPOSIZIONE, un'altra cosa:
    // nominarle qui indicava all'utente il campo sbagliato.
    if      (fieldName == "uMinEdit") { edited = ui->uMinEdit; axisLabel = "u min"; }
    else if (fieldName == "uMaxEdit") { edited = ui->uMaxEdit; axisLabel = "u max"; }
    else if (fieldName == "vMinEdit") { edited = ui->vMinEdit; axisLabel = "v min"; }
    else if (fieldName == "vMaxEdit") { edited = ui->vMaxEdit; axisLabel = "v max"; }
    else if (fieldName == "wMinEdit") { edited = ui->wMinEdit; axisLabel = "w min"; }
    else if (fieldName == "wMaxEdit") { edited = ui->wMaxEdit; axisLabel = "w max"; }
    if (!edited) return false;

    // Campo disabilitato (es. W in Composition, svuotato apposta): niente da fare.
    if (!edited->isEnabled()) return false;

    // CONFERMA UNICA. Il flag userEditPending dice che c'e' una digitazione
    // dell'utente in attesa: qui si CONTROLLA e si consuma, in questo ordine.
    // Chi arriva secondo lo trova gia' consumato ed esce senza rivalidare.
    //
    // Serve perche' un solo Invio produce DUE chiamate: il filtro tastiera fa
    // prima w->clearFocus(), che emette subito editingFinished -- il cui
    // handler chiama gia' questa funzione -- e solo al ritorno il filtro
    // chiama a sua volta commitLimitFieldOnEnter. Limitandosi ad azzerare il
    // flag senza leggerlo, la seconda chiamata rivalidava lo stesso testo
    // sbagliato: su desktop notify() e' un exec() bloccante senza guardia
    // s_boxActive, quindi si vedevano due popup in fila (chiuso il primo,
    // compariva il secondo). Stesso effetto sul setFocus() del ramo d'errore
    // qui sotto, che alla chiusura del box fa perdere di nuovo il focus.
    //
    // Il ramo Android di notify() non salva: li' s_boxActive scarta il secondo
    // box, ma la seconda validazione girava comunque.
    if (!userEditPending(edited)) return false;
    setUserEditPending(edited, false);

    const QString currentText = lineText(edited);

    // COPPIA INCOMPLETA: il campo e' vuoto, o lo e' ancora il suo compagno
    // dello stesso asse (si scrive il primo e si passa al secondo). Niente
    // popup e niente registrazione: il dominio si sta ancora scrivendo, e il
    // vuoto si leggerebbe 0 (min >= max con "0" e un max non ancora scritto).
    // Il Run resta spento finche' la coppia non e' completa
    // (hasCompleteParametricInput / hasCompleteTubeInput), e chi lo forza
    // (Invio su un'equazione, master) trova la validazione del Run. Prima il
    // popup usciva anche cliccando NEW con un limite cancellato.
    QLineEdit* partner = nullptr;
    if      (edited == ui->uMinEdit) partner = ui->uMaxEdit;
    else if (edited == ui->uMaxEdit) partner = ui->uMinEdit;
    else if (edited == ui->vMinEdit) partner = ui->vMaxEdit;
    else if (edited == ui->vMaxEdit) partner = ui->vMinEdit;
    else if (edited == ui->wMinEdit) partner = ui->wMaxEdit;
    else if (edited == ui->wMaxEdit) partner = ui->wMinEdit;
    if (currentText.trimmed().isEmpty() || (partner && lineText(partner).trimmed().isEmpty())) {
        updateMasterButtonState();
        return false;
    }

    bool ok = false;
    parseLimitField(currentText, &ok);
    if (!ok) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showInvalidLimitError(this, axisLabel, currentText,
                                                  /*emptyMeansNoLimit=*/false);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    // Coerenza dell'ASSE e REGISTRAZIONE del dominio. updateU/V/WLimits rifiuta
    // da sola i limiti impossibili (min >= max) restituendo false senza toccare
    // nulla; se accetta, scrive i membri uMin/uMax... e il dominio dell'engine
    // (setRangeU/V/W).
    //
    // La registrazione va fatta ORA, non al Run, e il motivo e' il caso animato:
    // a superficie in moto per 't' il tasto e' "Stop" -- non c'e' nessun Run da
    // premere -- e il tick rigenera la mesh dal dominio DELL'ENGINE. Lasciando
    // il dominio vecchio la superficie restava identica: si riducevano gli
    // intervalli e non cambiava niente. Registrare non e' "applicare a meta'
    // digitazione": il dominio e' un intervallo numerico, non un'espressione da
    // comporre con le altre, e non puo' trovarsi in uno stato incoerente come
    // una X aggiornata con Y e Z ancora vecchie.
    //
    // Cio' che resta legato al Run e' il RIDISEGNO a superficie ferma (vedi
    // sotto): li' ridisegnare mostrerebbe la superficie VECCHIA tagliata dai
    // limiti NUOVI, un'immagine che non corrisponde a niente.
    bool applied = true;
    if      (fieldName.startsWith("u")) applied = updateULimits();
    else if (fieldName.startsWith("v")) applied = updateVLimits();
    else                                applied = updateWLimits();

    if (!applied) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            // Qui il numero e' LEGGIBILE: updateU/V/WLimits ha rifiutato perche'
            // min >= max. Il messaggio "non e' un numero valido" era fuorviante,
            // mostrava il valore appena scritto come se fosse illeggibile.
            const QChar axis = axisLabel.isEmpty() ? QChar('u') : axisLabel.at(0);
            InputValidator::showLimitOrderError(this, axis);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    // SUPERFICIE IN MOTO (geometria animata da 't' o flusso geodetico in corsa):
    // qui il dominio va anche RIGENERATO subito, perche' nessun tick lo fara'.
    // L'animazione da 't' della parametrica vive nel VERTEX SHADER (glwidget
    // manda m_uboData.time alla GPU a ogni frame): la mesh CPU non viene
    // ricalcolata, e il dominio u/v e' invece campionato da computeMesh() sulla
    // CPU. setRangeU/V/W qui sopra alza meshNeedsUpdate, ma quel flag governa
    // solo il RE-UPLOAD alla GPU dei vertici che l'engine ha gia' in pancia:
    // senza updateSurfaceData() si ricarica la mesh VECCHIA e a schermo non
    // cambia niente -- e' esattamente il "riduco gli intervalli e la superficie
    // non cambia" che si vedeva.
    // Non c'e' il problema di coerenza che consiglia di aspettare il Run a
    // superficie ferma: la forma a schermo e' quella delle equazioni GIA'
    // applicate (le modifiche in corso di scrittura aspettano il Run come
    // sempre), e qui se ne cambia il solo dominio.
    const bool geodesicActive = isGeodesicRoutingActive();

    const bool geomMoving = ui->glWidget && ui->glWidget->isSurfaceAnimating();
    const bool geoFlowMoving = (m_geoAnimTimer && m_geoAnimTimer->isActive());
    if (geomMoving || geoFlowMoving) {
        if (ui->glWidget) {
            if (geodesicActive) {
                // Flusso geodetico: la mesh NON viene dalle equazioni X/Y/Z/P
                // (sola mappa di visualizzazione) ma dall'integrazione, che ha
                // il suo imbuto. updateSurfaceData() la valuterebbe come mesh
                // parametrica, collassandola in una LAMINA.
                // useAppliedEquations: si applica SOLO il dominio; equazioni e
                // condizioni iniziali modificate restano in attesa del Run.
                updateGeodesicMesh(/*useAppliedLimits=*/false,
                                   /*useAppliedEquations=*/true);
            } else {
                ui->glWidget->updateSurfaceData();
            }
            ui->glWidget->update();
        }
        updateMasterButtonState();
        // Il dominio nuovo e' a schermo: da qui e' lavoro da proteggere.
        noteSurfaceApplied({ QStringLiteral("limits/") });
        return true;
    }

    // SUPERFICIE FERMA: il limite e' REGISTRATO e vale al prossimo Run, ma non
    // si ridisegna. Il tasto Run e' gia' acceso (il textEdited del campo ha
    // azzerato m_parametricApplied al primo carattere) ed e' lui a dire che c'e'
    // qualcosa in attesa; qui si riallinea comunque lo stato dei tasti, perche'
    // il dominio appena completato puo' aver chiuso l'ultima casella mancante
    // del gate (hasCompleteParametricInput).
    updateMasterButtonState();
    return true;
}

// Limite u/v della MESH SELEZIONATA confermato (Invio o uscita dal campo).
// Gemello di commitLimitFieldOnEnter: stessa validazione, stessi popup, stesso
// meccanismo userEditPending contro il doppio commit di un solo Invio.
//
// LA DIFFERENZA CHE CONTA: qui si applica SEMPRE subito, mentre i limiti
// globali a superficie ferma aspettano il Run. Il motivo di quell'attesa e' la
// coerenza fra dominio ed equazioni -- ridisegnare mostrerebbe la superficie
// VECCHIA tagliata dai limiti NUOVI. Qui quel rischio non c'e': le equazioni (o
// lo script) sono gia' applicate e si sta cambiando il dominio di una sola
// parte, esattamente come si cambia il suo colore o la sua densita' wireframe.
// Non applicarlo subito, oltretutto, lo renderebbe inutilizzabile: il Run dello
// script RISCRIVE i domini per-mesh dalle sezioni //MESH_BEGIN, quindi un
// dominio "in attesa del Run" verrebbe cancellato proprio da cio' che lo
// dovrebbe applicare.
bool MainWindow::commitMeshLimitFieldOnEnter(const QString& fieldName)
{
    if (!ui->glWidget) return false;

    QLineEdit* edited = nullptr;
    if      (fieldName == "meshUMinEdit") edited = ui->meshUMinEdit;
    else if (fieldName == "meshUMaxEdit") edited = ui->meshUMaxEdit;
    else if (fieldName == "meshVMinEdit") edited = ui->meshVMinEdit;
    else if (fieldName == "meshVMaxEdit") edited = ui->meshVMaxEdit;
    if (!edited) return false;

    // Campo spento (ambito "All", mesh singola, Ray Marching): niente da fare.
    if (!edited->isEnabled()) return false;

    // CONFERMA UNICA, come nei limiti globali: un solo Invio produce DUE
    // chiamate (il filtro tastiera fa clearFocus(), che emette subito
    // editingFinished, e poi chiama a sua volta il commit). Il flag si
    // CONTROLLA e si consuma qui, in questo ordine: chi arriva secondo lo
    // trova gia' consumato ed esce senza rivalidare, o si vedrebbero due
    // popup in fila per un solo errore.
    if (!userEditPending(edited)) return false;
    setUserEditPending(edited, false);

    // AMBITO: in "Mesh" si scrive nella parte selezionata, in "All" nel dominio
    // di All (che vale per tutte le mesh e sospende i tagli per-parte senza
    // cancellarli). Si parte sempre dal dominio IN VIGORE per quell'ambito,
    // cosi' i tre campi non editati restano quelli che sono.
    const bool editingAll = (ui->glWidget->activeMeshPart() < 0);
    float uLo = 0.0f, uHi = 0.0f, vLo = 0.0f, vHi = 0.0f;
    const bool haveDomain = editingAll
                          ? ui->glWidget->allMeshDomain(uLo, uHi, vLo, vHi)
                          : ui->glWidget->activeMeshDomain(uLo, uHi, vLo, vHi);
    if (!haveDomain) return false;

    // Il campo appena editato deve essere leggibile. Gli altri tre NON si
    // rileggono dai widget: si parte dal dominio in vigore nella parte, cosi'
    // un campo lasciato a meta' da un'altra digitazione non entra nel commit.
    const QString currentText = lineText(edited);
    bool ok = false;
    const float value = parseLimitField(currentText, &ok);
    if (!ok || currentText.trimmed().isEmpty()) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            // Etichetta con il NUMERO della mesh: col pannello Multi Mesh
            // aperto su piu' fasce, "u min" da solo non direbbe quale.
            const QString axisLabel =
                QString("mesh %1 %2").arg(ui->spinMeshSel->value())
                                     .arg(fieldName.contains("UMin") ? "u min"
                                        : fieldName.contains("UMax") ? "u max"
                                        : fieldName.contains("VMin") ? "v min" : "v max");
            InputValidator::showInvalidLimitError(this, axisLabel, currentText,
                                                  /*emptyMeansNoLimit=*/false);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    if      (fieldName == "meshUMinEdit") uLo = value;
    else if (fieldName == "meshUMaxEdit") uHi = value;
    else if (fieldName == "meshVMinEdit") vLo = value;
    else                                  vHi = value;

    // Entrambi i setter rifiutano da se' i limiti impossibili (min >= max)
    // senza toccare nulla, come updateU/V/WLimits per i globali.
    const bool applied = editingAll
                       ? ui->glWidget->setAllMeshDomain(uLo, uHi, vLo, vHi)
                       : ui->glWidget->setActiveMeshDomain(uLo, uHi, vLo, vHi);
    if (!applied) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showLimitOrderError(this, fieldName.contains('U') ? QChar('u')
                                                                              : QChar('v'));
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        // Il campo mostra un valore che NON e' stato applicato: si riallinea al
        // dominio vero della parte, o resterebbe a mentire fino al prossimo
        // cambio di selezione.
        syncMeshLimitFields();
        return false;
    }

    // Applicato. setActiveMeshDomain ha gia' rigenerato la griglia (e riemesso
    // meshPartsChanged, che ripassa da qui a riallineare i campi): non serve
    // altro. Il master non va rivalutato -- nessun clock e' cambiato.
    return true;
}

void MainWindow::discardPendingLimitEdits()
{
    for (QLineEdit *e : { ui->uMinEdit, ui->uMaxEdit, ui->vMinEdit, ui->vMaxEdit,
                          ui->wMinEdit, ui->wMaxEdit,
                          ui->meshUMinEdit, ui->meshUMaxEdit, ui->meshVMinEdit, ui->meshVMaxEdit })
        if (e) setUserEditPending(e, false);
}

// Porta i quattro campi u/v del pannello Multi Mesh sul dominio della parte
// selezionata. A segnali bloccati: questi setText sono DISPLAY, e senza il
// blocco il textEdited li segnerebbe come digitazione in attesa (userEditPending),
// facendo validare all'uscita dal campo un testo che l'utente non ha scritto.
void MainWindow::syncMeshLimitFields()
{
    if (!ui->glWidget || !ui->meshUMinEdit) return;

    // LA FONTE DIPENDE DALL'AMBITO, come per ogni altro controllo per-mesh: i
    // campi mostrano cio' che stanno per modificare. In "Mesh" e' il dominio
    // della parte scelta; in "All" quello di All -- se c'e' un taglio, quello,
    // altrimenti il dominio che la figura sta disegnando adesso (la prima
    // parte), cosi' il campo non e' mai vuoto su una superficie visibile.
    float uLo = 0.0f, uHi = 0.0f, vLo = 0.0f, vHi = 0.0f;
    const bool hasPart = (ui->glWidget->activeMeshPart() >= 0)
                       ? ui->glWidget->activeMeshDomain(uLo, uHi, vLo, vHi)
                       : ui->glWidget->allMeshDomain(uLo, uHi, vLo, vHi);

    auto show = [this](QLineEdit *e, bool on, float v) {
        if (!e) return;
        QSignalBlocker b(e);
        // 'g' con 12 cifre come i limiti globali (mainwindow.cpp ~1807): un
        // 6.28318531 dello script non deve tornare indietro arrotondato.
        e->setText(on ? QString::number(v, 'g', 12) : QString());
        // La digitazione eventualmente in sospeso su questo campo non vale
        // piu': il testo l'ha appena riscritto il programma.
        setUserEditPending(e, false);
    };

    show(ui->meshUMinEdit, hasPart, uLo);
    show(ui->meshUMaxEdit, hasPart, uHi);
    show(ui->meshVMinEdit, hasPart, vLo);
    show(ui->meshVMaxEdit, hasPart, vHi);
}

// Equazioni parametriche sufficienti a definire una superficie: almeno TRE dei
// quattro campi X/Y/Z/P non vuoti. Stessa idea della soglia >=2 di
// hasPath4DInput, tarata sui dati: fra le superfici parametriche in libreria
// nessuna ha 1 o 2 campi pieni -- sono 4 campi (la gran parte) o 3 (P vuoto,
// che e' legittimo: le superfici puramente 3D non usano la quarta coordinata).
// Con meno di tre non c'e' una superficie da disegnare, e premere Run
// costruiva una forma degenere sui campi rimasti dalla superficie precedente.
// NB: le superfici da SCRIPT hanno tutti e quattro i campi vuoti e non passano
// di qui -- il tasto Run parametrico e' gia' spento per loro
// (surfaceFromScript in updateMasterButtonState).
bool MainWindow::hasParametricEquationInput() const
{
    int filled = 0;
    if (ui->lineX && !m_scene.eq.x.trimmed().isEmpty()) filled++;
    if (ui->lineY && !m_scene.eq.y.trimmed().isEmpty()) filled++;
    if (ui->lineZ && !m_scene.eq.z.trimmed().isEmpty()) filled++;
    if (ui->lineP && !m_scene.eq.p.trimmed().isEmpty()) filled++;
    return filled >= 3;
}

// Gate del TASTO RUN del dock Equations: tutti i campi che servono davvero
// alla superficie che si sta scrivendo sono compilati. E' piu' severo di
// hasParametricEquationInput (che guarda i soli X/Y/Z/P ed e' la validazione
// del master START, con il suo popup dedicato): qui si aggiungono il dominio e
// i campi del TAB ATTIVO del pannello, perche' con quelli incompleti il Run
// non ha niente di sensato da costruire e il tasto deve restare spento invece
// di produrre una mesh degenere. Il tasto e' cosi' anche un indicatore di
// completezza: si accende quando c'e' tutto.
//
// Nessun popup qui: e' un predicato di UI, chiamato di continuo da
// updateMasterButtonState a ogni carattere digitato.
bool MainWindow::hasCompleteParametricInput()
{
    // 1. Equazioni principali: la soglia storica (>=3 di X/Y/Z/P).
    if (!hasParametricEquationInput()) return false;

    // 2. DOMINIO: i limiti dei parametri effettivamente in uso devono essere
    //    compilati e coerenti. Si guarda isEnabled(), che checkParametricDependency
    //    ha gia' allineato all'uso reale di u/v/w (un asse spento -- es. w in
    //    Composition -- non fa testo). I limiti non si applicano piu' da soli
    //    all'Invio: se sono incompleti, il Run costruirebbe sul dominio vecchio.
    auto axisOk = [this](QLineEdit* lo, QLineEdit* hi) -> bool {
        if (!lo || !hi || !lo->isEnabled() || !hi->isEnabled()) return true;  // asse non in uso
        const QString loTxt = lineText(lo).trimmed();
        const QString hiTxt = lineText(hi).trimmed();
        if (loTxt.isEmpty() || hiTxt.isEmpty()) return false;
        bool okLo = false, okHi = false;
        const float a = parseLimitField(loTxt, &okLo);
        const float b = parseLimitField(hiTxt, &okHi);
        if (!okLo || !okHi) return false;
        return a < b;
    };
    if (!axisOk(ui->uMinEdit, ui->uMaxEdit)) return false;
    if (!axisOk(ui->vMinEdit, ui->vMaxEdit)) return false;
    if (!axisOk(ui->wMinEdit, ui->wMaxEdit)) return false;

    // 3. Campi del TAB ATTIVO del pannello. Si guarda il tab CORRENTE e solo se
    //    e' abilitato: checkParametricDependency spegne i tab che non c'entrano
    //    con le equazioni scritte, e un tab spento non ha campi da pretendere.
    if (ui->panelImplicit) {
        const int tab = ui->panelImplicit->currentIndex();
        if (ui->panelImplicit->isTabEnabled(tab)) {
            if (tab == 1) {
                // COMPOSITION: ogni variabile maiuscola citata dalle equazioni
                // dev'essere definita. Un campo abilitato e vuoto e' una U senza
                // definizione: la superficie non e' scrivibile.
                for (QPlainTextEdit* e : { ui->lineU, ui->lineV, ui->lineW }) {
                    if (e && e->isEnabled() && e->toPlainText().trimmed().isEmpty())
                        return false;
                }
            }
            else if (tab == 2 && ui->lnU) {
                // GEODESIC FLOW: serve una DIREZIONE iniziale non nulla, cioe'
                // almeno uno fra du/dv/dw compilato. Con tutte e tre a zero non
                // c'e' geodetica da integrare e il Run non produce nulla.
                //
                // NON si pretendono tutti e sei i campi compilati: un campo
                // vuoto vale ZERO, che e' una condizione iniziale legittima e
                // usatissima (22 dei 37 preset geodetici di fabbrica lasciano
                // vuoto w0, o du/dv/dw non usati). Pretenderli avrebbe spento
                // il Run su piu' di meta' della libreria.
                // Il punto di partenza u0/v0/w0 puo' quindi essere tutto vuoto:
                // e' l'origine, un punto come un altro. Il fattore conforme
                // (lineConform) ha un default di 1.0 e non si pretende.
                bool anyDir = false;
                for (QPlainTextEdit* e : { ui->lndU, ui->lndV, ui->lndW }) {
                    if (!e || !e->isEnabled()) continue;
                    const QString txt = e->toPlainText().trimmed();
                    // "0" esplicito conta come non-direzione, esattamente come
                    // il campo vuoto: entrambi danno componente nulla.
                    if (!txt.isEmpty() && txt != "0" && txt != "0.0") anyDir = true;
                }
                if (!anyDir) return false;
            }
            // tab == 0 (CONSTRAINTS): i vincoli sono FACOLTATIVI e mutuamente
            // esclusivi (scriverne uno svuota gli altri due: vedi il blocco
            // "Mutua esclusione dei vincoli" nel costruttore): pretenderli
            // compilati spegnerebbe il Run su ogni normale superficie u,v.
        }
    }

    return true;
}


 // --- Parsing, Strings & Scripts ---

float MainWindow::parseMath(const QString &text, bool *ok)
{
    QString clean = text.trimmed();
    if (clean.isEmpty()) {
        if (ok) *ok = false;
        return 0.0f;
    }

    // Uniformiamo la virgola decimale (locale italiano)
    clean.replace(',', '.');

    bool parseOk = false;
    float v = ExpressionParser::evaluateSimple(clean, parseOk);
    if (ok) *ok = parseOk;
    return parseOk ? v : 0.0f;
}

float MainWindow::parseLimitField(const QString &text, bool *ok)
{
    // I limiti ammettono le costanti A..F/S oltre a pi/e/tau. Non passiamo da
    // parseMath: la sua symbol table non conosce le costanti e un "2*A"
    // cadrebbe nel fallback toFloat() -> 0.0 silenzioso (il limite diventa 0
    // e, peggio, viene salvato 0 nel preset).
    //
    // restoreTextOnNegative=false: qui stiamo solo LEGGENDO un limite, non
    // editando le costanti; riscrivere i campi A..F da dentro la lettura di
    // uMax sarebbe un effetto collaterale a sorpresa.
    const CascadeConstants k = resolveCascadeConstants(false);
    return parseUIConstant(text, k.a, k.b, k.c, k.d, k.e, k.f, k.s, ok);
}

float MainWindow::parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S, bool* ok)
{
    QString cleanExpr = exprStr.trimmed();
    if (cleanExpr.isEmpty()) { if (ok) *ok = true; return 0.0f; }

    // 1. Uniformiamo la punteggiatura
    cleanExpr.replace(",", ".");

    // 2. PASSAGGIO A DOUBLE: ExprTk è nativo e infallibile in double
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    symbol_table_t symbol_table;
    symbol_table.add_constants();

    // 3. AGGIUNTA MANUALE FORZATA: Nel caso add_constants() faccia i capricci
    symbol_table.add_constant("pi", 3.14159265358979323846);
    symbol_table.add_constant("PI", 3.14159265358979323846);
    symbol_table.add_constant("e",  2.71828182845904523536);
    symbol_table.add_constant("tau", 6.28318530717958647692);
    symbol_table.add_constant("TAU", 6.28318530717958647692);

    // 4. FIX FONDAMENTALE: Usiamo add_constant invece di add_variable!
    // Inserendo i numeri come costanti assolute evitiamo qualsiasi crash
    // di puntatori o reference in memoria da parte di ExprTk.
    symbol_table.add_constant("A", (double)A);
    symbol_table.add_constant("B", (double)B);
    symbol_table.add_constant("C", (double)C);
    symbol_table.add_constant("D", (double)D);
    symbol_table.add_constant("E", (double)E);
    symbol_table.add_constant("F", (double)F);
    symbol_table.add_constant("S", (double)S); symbol_table.add_constant("s", (double)S);

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    parser_t parser;

    // 5. COMPILAZIONE
    if (parser.compile(cleanExpr.toStdString(), expression)) {
        if (ok) *ok = true;
        return static_cast<float>(expression.value());
    } else {
        if (ok) *ok = false;
        return 0.0f;
    }
}

QString MainWindow::composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef) {
    if (eq.isEmpty()) return eq;

    QString res = eq;

    // Se un campo è vuoto, il suo valore di default è la rispettiva variabile minuscola.
    // Usiamo le parentesi per garantire l'ordine delle operazioni matematiche!
    QString subU = uDef.trimmed().isEmpty() ? "u" : "(" + uDef.trimmed() + ")";
    QString subV = vDef.trimmed().isEmpty() ? "v" : "(" + vDef.trimmed() + ")";
    QString subW = wDef.trimmed().isEmpty() ? "w" : "(" + wDef.trimmed() + ")";

    // \b indica un "word boundary", così sostituisce la "U" isolata,
    // ma ignora ad esempio la "U" dentro una parola fittizia
    res.replace(kReUpperU, subU);
    res.replace(kReUpperV, subV);
    res.replace(kReUpperW, subW);

    return res;
}

bool MainWindow::hasTimeVariable(const QString& code) const {
    // Commenti rimossi per evitare falsi positivi (es. "don't")
    QString cleaned = stripCodeComments(code);

    // Rimuoviamo il blocco audio //SOUND_BEGIN..//SOUND_END prima del match: la
    // firma mainSound(int samp, float time) e le sue 'time'/'t' LOCALI sono del
    // sintetizzatore audio, NON dell'uniforme tempo grafico. Senza questo, ogni
    // record con un suono (la maggioranza) risultava "animato" anche a geometria
    // statica -> clock accesi a vuoto e potenziali riavvii spuri. La 't'/'iTime'
    // del codice GRAFICO (es. "float t = iTime;") resta e segnala animazione vera.
    // Stesso regex (con blocchi annidati) di TextureCode::cleanForComparison.
    QRegularExpression soundBlock(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    while (cleaned.contains(soundBlock)) cleaned.remove(soundBlock);

    return cleaned.contains(kReTimeVar);
}

bool MainWindow::applyDiscreteConstants()
{
    if (m_scene.discreteConsts.isEmpty() && m_scene.minConsts.isEmpty()) return false;

    bool changed = false;
    for (ConstField field : constantFields()) {
        bool ok = false;
        const float cur = (m_scene.constants.*field).trimmed().toFloat(&ok);
        if (!ok) continue;   // espressione (es. "A*2"): non la tocchiamo

        const float target = snapConstant(cur, constantName(field),
                                          m_scene.discreteConsts, m_scene.minConsts);
        if (qFuzzyCompare(cur, target)) continue;

        // Solo il campo: slider e cascata li riallinea il chiamante, una volta
        // sola, altrimenti ogni riga qui ne scatenerebbe una (e con essa un
        // ricalcolo di mesh per ciascuna costante). 'g' evita lo zero decimale
        // sugli interi (4, non 4.00) e tiene le frazioni dei minimi continui (0.3).
        setConstValue(field, target);
        changed = true;
    }
    return changed;
}

void MainWindow::syncConstantSliders(const CascadeConstants &k)
{
    auto setSmartSlider = [this](QSlider* s, float v, bool isS) {
        bool old = s->blockSignals(true);
        int intVal = static_cast<int>(v * 100.0f);

        int newMin;
        int newMax;

        // --- NUOVA LOGICA: Se siamo sul Tab 1 (Ray Marching) e stiamo aggiornando lo slider S ---
        if (isS && implicitMode()) {
            newMin = 0; // In Ray Marching lo slider parte rigorosamente da 0
            // Il massimo è 100 (1.0), ma se l'utente digita un numero enorme, si espande!
            newMax = std::max(100, intVal);

            // Forza anche il valore in modo che non scenda mai sotto lo 0
            if (intVal < 0) { intVal = 40; /* 0.4 di default */ }
        }
        // --- VECCHIA LOGICA: Parametrica o altri slider ---
        else {
            // Garantisce un limite standard di 10 (1000) o espande se il valore digitato è maggiore
            newMin = isS ? std::min(-1000, intVal) : 0;
            newMax = std::max(1000, intVal);
        }

        // Protezione "anti-collasso" se usi il mouse
        if (s->hasFocus() || s->isSliderDown() || s->underMouse()) {
            newMin = std::min(newMin, s->minimum());
            newMax = std::max(newMax, s->maximum());
        }

        s->setRange(newMin, newMax);
        s->setValue(intVal);
        s->blockSignals(old);
    };

    setSmartSlider(ui->aSlider, k.a, false);
    setSmartSlider(ui->bSlider, k.b, false);
    setSmartSlider(ui->cSlider, k.c, false);
    setSmartSlider(ui->dSlider, k.d, false);
    setSmartSlider(ui->eSlider, k.e, false);
    setSmartSlider(ui->fSlider, k.f, false);
    setSmartSlider(ui->sSlider, k.s, true); // true = questo è lo slider S!

    // Costanti discrete ("A := int(1,6)"): clic accanto alla maniglia, frecce
    // e rotellina spostano di un intero (100 centesimi). Coi passi standard
    // (0.1 e 0.01) l'arrotondamento riporterebbe la costante dov'era.
    for (ConstField f : constantFields()) {
        const bool discrete = m_scene.discreteConsts.contains(constantName(f));
        QSlider *s = constantSlider(f);
        s->setSingleStep(discrete ? 100 : 1);
        s->setPageStep(discrete ? 100 : 10);
    }
}

void MainWindow::setEngineConstants(const CascadeConstants &kc, bool onlyIfChanged)
{
    if (!ui->glWidget) return;
    if (onlyIfChanged) {
        // setEquationConstants segna la mesh da rifare: chi passa di qui a ogni
        // uscita di Run non deve farla rigenerare a valori invariati.
        const QMap<QString, float> &now = ui->glWidget->getConstantsMap();
        const float want[7] = { kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s };
        static const char *const names[7] = { "A", "B", "C", "D", "E", "F", "S" };
        bool same = now.size() == 7;
        for (int i = 0; same && i < 7; ++i)
            same = now.value(QLatin1String(names[i]), want[i] + 1.0f) == want[i];
        if (same) return;
    }
    ui->glWidget->setEquationConstants(kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s);
}

MainWindow::CascadeConstants MainWindow::pushConstantsToEngine(bool restoreTextOnNegative,
                                                               bool always)
{
    const CascadeConstants kc = resolveCascadeConstants(restoreTextOnNegative);
    // Gli slider mostrano i valori RISOLTI: una costante definita come
    // espressione di un'altra cambia con lei anche quando a cambiarla non e'
    // un gesto dell'utente.
    syncConstantSliders(kc);
    setEngineConstants(kc, /*onlyIfChanged=*/!always);
    return kc;
}

void MainWindow::refreshConstants(bool restoreTextOnNegative)
{
    // Chi chiama da qui ha appena cambiato cio' che e' a schermo (load, reset,
    // Library, Run riuscito): campi e applicato coincidono di nuovo.
    m_constantsEditPending = false;
    updateConstantsUIState();
    pushConstantsToEngine(restoreTextOnNegative);
}

bool MainWindow::isGeodesicRoutingActive() const
{
    if (implicitMode()) return false;
    if (!hasGeodesicText()) return false;

    const QString mainEqs = m_scene.eq.x + " " + m_scene.eq.y + " "
                          + m_scene.eq.z + " " + m_scene.eq.p;
    const int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0)
                         + (mainEqs.contains(kReUpperV) ? 1 : 0)
                         + (mainEqs.contains(kReUpperW) ? 1 : 0);
    // Con uno script METRICO la mappa X/Y/Z/P puo' legittimamente non citare
    // U/V/W (resta la carta identita'): il solo upperCount non riconoscerebbe
    // il caso.
    const bool metricScriptActive = !m_metricScriptBody.trimmed().isEmpty();
    return upperCount > 0 || metricScriptActive;
}

void MainWindow::commitUiFieldsDuringMotion() {
    // Conta come commit da Invio: il ramo generico del filtro mobile chiama
    // QUESTA, non commitFieldsOnEnter, e va contata sullo stesso contatore --
    // altrimenti il confronto attorno al clearFocus() non la vedrebbe e il
    // doppione resterebbe (qui i popup sono quelli di validateAndParseLimits e
    // di tutto onStartClicked).
    ++m_commitOnEnterCount;

    // Questa funzione applica AL VOLO le equazioni della geometria: ha senso
    // solo se e' il MODULO EQUAZIONI a essere in moto. Guardare il master
    // button (su STOP anche per un path camera, una rotazione o l'audio, con la
    // geometria ferma) la faceva girare su una superficie STATICA, committando
    // equazioni che dovevano attendere il Run -- vedi il commento in
    // commitFieldsOnEnter. Il ramo generico del filtro mobile la chiama
    // direttamente, quindi la guardia deve stare anche qui.
    if (!isEquationModuleMoving()) return;
    m_geodesicErrorPending = false;

    QString mainEqs = m_scene.eq.x + " " + m_scene.eq.y + " " +
                      m_scene.eq.z + " " + m_scene.eq.p;
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);
    bool geoHasText = hasGeodesicText();
    // Su Tubes i campi di Surface (nascosti) non decidono nulla: il Run va al
    // ramo dei tubi.
    bool isGeodesicActive = (upperCount > 0) && geoHasText &&
            (!implicitMode()) && !tubesShown();
    if (!isGeodesicActive) {
        onStartClicked();
        return;
    }

    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };
    QVector<float> dummy;
    auto parseFn = [this](const QString& s, bool* ok) { return this->parseLimitField(s, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, dummy)) {
        return;  // popup mostrato dal validator, niente dry-run
    }

    // Snapshot dei valori attuali prima di provare i nuovi.
        const std::optional<EquationTexts> previous = m_eqApplied;
        bool wasTimerActive = isGeodesicMotionActive();

        // Tentativo: applichiamo i nuovi e validiamo.
        snapshotActiveEquations();
        if (updateGeodesicMesh()) {
            return;  // OK: i nuovi valori restano in m_eqApplied, il moto continua.
        }

        // Errore: ripristiniamo i valori validi precedenti.
        // Il tasto rimane su STOP; mostriamo un solo popup.
        m_eqApplied = previous;
        m_geodesicErrorPending = false;
        m_geoErrorType = GeoError::None;

        if (!m_geoErrorShown) {
            m_geoErrorShown = true;
            InputValidator::showGeodesicSingularityError(this);
        }

        // Riavvia il timer con i dati ripristinati (se era in moto).
        if (wasTimerActive) {
            if (m_geoAnimTimer && !m_geoAnimTimer->isActive())
                m_geoAnimTimer->start();
        }
    }

// Ritorna true se la modifica e' stata APPLICATA (vedi commitLimitFieldOnEnter).
bool MainWindow::commitFieldsOnEnter() {
    // Contatore dei commit: i filtri tastiera lo confrontano prima e dopo il
    // loro w->clearFocus() per non chiamare due volte per un solo Invio (vedi
    // commitOnEnterCount in mainwindow.h e il ramo generico dei filtri).
    ++m_commitOnEnterCount;

    m_geodesicErrorPending = false;

    // MODULO EQUAZIONI in moto: usa la logica live già esistente, che applica al
    // volo senza fermare l'animazione.
    //
    // Si guarda il MODULO EQUAZIONI, non il master button. Il master va su STOP
    // per QUALUNQUE cosa si muova -- clock della texture o dello sfondo, path
    // camera, rotazioni, audio -- anche con la GEOMETRIA FERMA. In quel caso si
    // finiva nel ramo live, che fa un Run NON di servizio, quindi
    // senza il congelamento delle equazioni sullo snapshot.
    // Basta un path camera in corsa, o una texture animata, perche' il master
    // dica STOP: da li' si finiva nel ramo live e la t appena scritta veniva
    // committata al primo Invio, senza Run.
    // E' lo stesso difetto gia' corretto per il commit di servizio e per
    // l'avvio del flusso geodetico: qualcosa entra senza un Run, e lo stato dei
    // tasti descrive una realta' diversa da quella a schermo. La radice comune
    // e' usare il master button come se dicesse "la geometria e' in moto".
    // A geometria ferma si prosegue sotto, dove il commit e' di servizio
    // e le equazioni restano in attesa del Run.
    if (isEquationModuleMoving()) {
        commitUiFieldsDuringMotion();
        return true;
    }

    // A superficie ferma: applica solo nel tab parametrico.
    if (implicitMode()) return false;

    // Stesso routing di checkAndTriggerMeshUpdate: geodetico vs standard.
    QString mainEqs = m_scene.eq.x + " " + m_scene.eq.y + " " +
                      m_scene.eq.z + " " + m_scene.eq.p;
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);

    if ((upperCount > 0) && hasGeodesicText() && !tubesShown()) {
        // useAppliedEquations: qui arriva l'Invio sulle COSTANTI A..F/S, che non
        // e' un Run. Si aggiorna il VALORE delle costanti sulla superficie gia'
        // applicata; tutte le equazioni -- la mappa X/Y/Z/P e i 7 campi del
        // flusso -- restano congelate sullo snapshot e aspettano il Run,
        // esattamente come nella parametrica standard.
        // (L'Invio sui 7 campi del flusso non passa piu' di qui a superficie
        // ferma: il filtro tastiera li tratta come campi differiti.)
        const bool meshOk = updateGeodesicMesh(/*useAppliedLimits=*/false,
                                               /*useAppliedEquations=*/true);
        if (!meshOk
                && m_geoErrorType == GeoError::Singularity
                && !m_geoErrorShown) {
            m_geoErrorShown = true;
            InputValidator::showGeodesicSingularityError(this);
        }

        // Il Run NON si spegne qui: questo percorso non ha applicato alcuna
        // equazione (le congela tutte), quindi se ce n'e' una in sospeso il Run
        // e' l'unico modo di applicarla. Lo rialza solo un Run vero.
        updateMasterButtonState();
        return meshOk;
    }

    // Standard / composition / constraint: applica equazioni, composizioni e
    // vincoli correnti e rigenera la mesh, senza far ripartire moto/rotazioni/audio.
    runScene(RunOrigin::ServiceCommit);   // niente toggle START/STOP
    return true;
}

bool MainWindow::updateGeodesicMesh(bool useAppliedLimits, bool useAppliedEquations)
{
    // Resettiamo il flag degli errori per questa esecuzione
    m_geoErrorType = GeoError::None;

    if (m_geodesicErrorPending) return false;

    // GUARD RAII anti-congelamento: più sotto, con isInitialLoad, disabilitiamo
    // gli update del glWidget (setUpdatesEnabled(false)) e li riabilitiamo solo
    // nel ramo di successo. Ognuno dei numerosi `return false` di errore qui
    // sotto, se raggiunto DOPO quella disabilitazione, lascerebbe il widget
    // congelato sull'ultima mesh valida. Questo guard riabilita SEMPRE gli
    // update all'uscita per errore (e su qualunque return futuro), e va
    // disarmato esplicitamente solo sul percorso di successo.
    bool meshSucceeded = false;
    auto updatesGuard = qScopeGuard([this, &meshSucceeded]() {
        if (!meshSucceeded && ui->glWidget && !ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    });

    // --- 1. LETTURA E VALIDAZIONE DEI LIMITI (U/V sempre attivi nel geodesic) ---
    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };

    QVector<float> limitValues;
    bool allOk = true;
    for (const auto& field : limitFields) {
        if (!field.active) {
            limitValues.append(0.0f);
            continue;
        }
        bool ok = false;
        float v = parseLimitField(field.edit->text(), &ok);
        if (!ok) { allOk = false; break; }
        limitValues.append(v);
    }
    if (!allOk) {
        return false;
    }

    float uMin = limitValues[0], uMax = limitValues[1];
    float vMin = limitValues[2], vMax = limitValues[3];

    // MOTO IN CORSO: il dominio e' quello GIA' APPLICATO, non il testo dei
    // campi. Questa funzione gira a ogni tick del timer su un record animato,
    // e leggendo i campi raccoglieva le cifre a meta' digitazione: il limite
    // entrava in vigore al primo carattere, senza attendere l'Invio. I campi
    // restano la sorgente per i percorsi interattivi (Run, Invio sui limiti),
    // che passano da updateU/VLimits e quindi aggiornano proprio questi valori.
    if (useAppliedLimits && ui->glWidget && ui->glWidget->getEngine()) {
        const auto *eng = ui->glWidget->getEngine();
        uMin = eng->getUMin(); uMax = eng->getUMax();
        vMin = eng->getVMin(); vMax = eng->getVMax();
    }

    int steps = m_scene.steps;
    int safeSteps = std::min(steps, 500);  // Limite fisico per le geodetiche

    // Controllo Min >= Max (U e V sempre attivi nel geodesic)
    if (!InputValidator::validateLimits(this, uMin, uMax, true, vMin, vMax, true, 0.0f, 0.0f, false)) {
        stopGeodesicAnimation();
        return false;
    }

    // 1. GESTIONE TEMPO (Animazione)
    double t = m_geoTime;

    if (m_geoInitialLoad) {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
        if (m_statusLabel) {
            m_statusLabel->setStyleSheet("color: #00bfff; font-weight: bold;");
        }
        m_geoInitialLoad = false;
    }

    // --- LOGICA DI DISACCOPPIAMENTO ---
    // Due gruppi di campi, stessa regola: a schermo ci va solo cio' che e' gia'
    // stato confermato con un Run.
    //
    // A MOTO ATTIVO (isRunning) si congela TUTTO: il tick gira di continuo e
    // leggere i campi raccoglierebbe le cifre a meta' digitazione.
    //
    // A MOTO FERMO con useAppliedEquations (percorsi che NON sono un Run:
    // costanti A..F/S da slider o Invio, slider Steps, limiti u/v/w, ripristino
    // path, navigazione) si congela di nuovo TUTTO:
    //  - X/Y/Z/P sono la MAPPA DI VISUALIZZAZIONE e appartengono al dock
    //    Equations: si applicano SOLO col Run. Vanno presi dallo snapshot,
    //    altrimenti il commit di un limite committa di straforo equazioni
    //    modificate e non confermate -- il bug per cui il tasto Run restava
    //    acceso senza piu' niente da applicare.
    //  - I 7 campi del flusso (u/v/w iniziali, du/dv/dw, fattore conforme)
    //    DEFINISCONO la superficie geodetica, ma anche loro aspettano il Run,
    //    esattamente come le equazioni della parametrica standard. Prima erano
    //    letti DAL VIVO qui, e da li' nasceva il disallineamento: scritta una
    //    costante nuova in un campo del flusso, bastava muovere lo slider di
    //    quella costante (o dare Invio sul suo campo) perche' la modifica
    //    entrasse senza Run -- mentre nella parametrica standard restava in
    //    attesa. L'Invio sui 7 campi non passa piu' di qui a moto fermo: il
    //    filtro tastiera li tratta come campi differiti (isDeferredEquationField)
    //    e a superficie ferma non esegue nulla.
    const bool motionRunning = isGeodesicMotionActive();
    const bool freezeMapEqs  = motionRunning || useAppliedEquations;   // X/Y/Z/P
    const bool freezeFlowEqs = motionRunning || useAppliedEquations;   // i 7 campi

    // Snapshot mancante (mai fatto un Run: preset appena caricato da script
    // metrico, avvio a freddo). Ripiegare sui campi qui rimetterebbe in gioco
    // proprio le equazioni non confermate: meglio non ridisegnare affatto. Il
    // chiamante interattivo ha gia' REGISTRATO la sua modifica (limiti scritti
    // nell'engine, campi del flusso nella UI) e al prossimo Run vale tutto.
    SE_GEO_PROBE("updateGeodesicMesh useAppliedEqs=%d motion=%d snapshot=%d "
                 "freezeMap=%d freezeFlow=%d",
                 int(useAppliedEquations), int(motionRunning),
                 int(m_eqApplied.has_value()),
                 int(freezeMapEqs), int(freezeFlowEqs));

    if (useAppliedEquations && !m_eqApplied.has_value()) {
        SE_GEO_PROBE("ABORT: snapshot assente, non ridisegno "
                     "(il campo resta registrato, vale al prossimo Run)");
        return false;
    }

    auto pick = [this](bool frozen, EqField f) -> QString {
        return (frozen && m_eqApplied) ? (*m_eqApplied).*f : m_scene.eq.*f;
    };

    QString rawX = pick(freezeMapEqs, &EquationTexts::x);
    QString rawY = pick(freezeMapEqs, &EquationTexts::y);
    QString rawZ = pick(freezeMapEqs, &EquationTexts::z);
    QString rawP = pick(freezeMapEqs, &EquationTexts::p);

    QString rawU = pick(freezeFlowEqs, &EquationTexts::geoU);
    QString rawV = pick(freezeFlowEqs, &EquationTexts::geoV);
    QString rawW = pick(freezeFlowEqs, &EquationTexts::geoW);

    QString rawDU = pick(freezeFlowEqs, &EquationTexts::geoDU);
    QString rawDV = pick(freezeFlowEqs, &EquationTexts::geoDV);
    QString rawDW = pick(freezeFlowEqs, &EquationTexts::geoDW);

    QString rawConf = pick(freezeFlowEqs, &EquationTexts::conform);
    // --- FINE LOGICA DI DISACCOPPIAMENTO ---

    QRegularExpression varRegex("\\b(u|v|w|U|V|W|x|y|z|t|iTime|u_time)\\b");

    // Costanti dai CAMPI (cascata), le stesse che vanno al motore piu' sotto.
    // Leggendo gli slider (passo 0.01) un fattore conforme "200*A" con
    // A = 0.005 risultava nullo, e il Run dava errore.
    const CascadeConstants kc = resolveCascadeConstants(true);

    if (!rawConf.contains(varRegex)) {
        float lambdaVal = parseUIConstant(rawConf, kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s);

        if (lambdaVal <= 1e-8f) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            if (!m_geoErrorShown) {
                m_geoErrorShown = true;
                InputValidator::showInvalidConformalConstantError(this);
            }
            return false;
        }
    }

    SurfaceEngine* engine = ui->glWidget->getEngine();
    QRhi* rhi = ui->glWidget->getRhi(); // <-- Recuperiamo l'interfaccia GPU

    if (!rhi) {
        return false;
    }

    // ==============================================================
    // 2.5 ESTRAZIONE DELLE COSTANTI
    // ==============================================================
    {
        float cA = kc.a, cB = kc.b, cC = kc.c, cD = kc.d, cE = kc.e, cF = kc.f, cS = kc.s;
        setEngineConstants(kc, /*onlyIfChanged=*/false);

        if (!m_inGeoAnimTick &&
                !geodesicFieldsAreFinite({rawU, rawV, rawW, rawDU, rawDV, rawDW,
                                         rawConf, rawX, rawY, rawZ, rawP},
                                         uMin, uMax, vMin, vMax,
                                         cA, cB, cC, cD, cE, cF, cS)) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            m_geoErrorType = GeoError::NonFinite;
            if (!m_geoErrorShown) {
                m_geoErrorShown = true;
                InputValidator::showGeodesicSingularityError(this);
            }
            return false;
        }
    }

    // Avviso "costante ambigua" (una sola volta per configurazione): la stessa
    // A..F nella metrica e nelle condizioni iniziali rende lo slider ambiguo.
    // Qui intercetta anche le modifiche fatte dal dock, che non passano da
    // runMetricScript. Non durante i tick di animazione, per non interromperla.
    if (!m_inGeoAnimTick)
        checkMetricConstantAmbiguity();

    // Recupera la mappa delle costanti (già calcolate a cascata da onStartClicked)
    QMap<QString, float> constantsMap = ui->glWidget->getConstantsMap();

    QString shaderError; // <--- Prepariamo la stringa per l'errore

    // 3. CALCOLO SINCRONO SU GPU
    QVector<QVector<QVector4D>> grid = engine->computeGeodesicFlow(
                rhi,
                rawX, rawY, rawZ, rawP,
                rawU, rawV, rawW,
                rawDU, rawDV, rawDW,
                rawConf,
                m_metricScriptBody,
                uMin, uMax, safeSteps,
                vMin, vMax, safeSteps,
                constantsMap,
                static_cast<float>(t),       // <--- NUOVO: tempo come uniform
                &shaderError
                );

    // --- GESTIONE DEGLI ERRORI ---
    if (grid.isEmpty()) {
        if (!shaderError.isEmpty()) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            m_geoErrorShown = true;
            showShaderError("Geodesic Shader Error", shaderError);
            m_geoErrorType = GeoError::Syntax;
        } else {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            m_geoErrorType = GeoError::Singularity;
        }
        return false;
    }

    // 4. APPLICHIAMO IMMEDIATAMENTE IL RISULTATO (grid non vuota: verificato sopra)
    // uMin/uMax/vMin/vMax: il dominio appena integrato. Ancora le coordinate
    // texture al dominio invece che agli indici di griglia, cosi' restringere
    // l'intervallo TAGLIA la texture invece di comprimerla (vedi setCustomMesh).
    if (!ui->glWidget->setCustomMesh(grid, !m_metricScriptBody.trimmed().isEmpty(),
                                     uMin, uMax, vMin, vMax)) {
        m_geodesicErrorPending = true;
        if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
        m_geoErrorType = GeoError::Singularity;
        return false;
    }

    // Percorso di successo: disarma il guard anti-congelamento e ripristina la
    // UI post-caricamento riabilitando gli update del glWidget.
    meshSucceeded = true;
    if (ui->glWidget && !ui->glWidget->updatesEnabled())
        ui->glWidget->setUpdatesEnabled(true);

    // 5. LOGICA DEL TIMER CPU PER ANIMAZIONI
    QString geoEqs = rawX + " " + rawY + " " + rawZ + " " + rawP + " " +
            rawU + " " + rawV + " " + rawW + " " +
            rawDU + " " + rawDV + " " + rawDW + " " + rawConf + " " +
            m_metricScriptBody;

    bool hasTime = hasTimeVariable(geoEqs);

    if (!m_geoAnimTimer) {
        m_geoAnimTimer = new QTimer(this);
        m_geoAnimTimer->setObjectName("geoAnimTimer");   // VideoRecorder lo cerca per nome
        m_geoAnimTimer->setInterval(16);

        connect(m_geoAnimTimer, &QTimer::timeout, this, [this]() {
            // Guardia 1: finestra nascosta (app in background su iOS/Android)
            if (!isVisible()) return;

            // Guardia 2: contesto GPU non ancora ripristinato dopo il ritorno
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            // Guardia 3: l'utente sta editando una costante (A..F / S).
            {
                QWidget* fw = qApp->focusWidget();
                if (fw == ui->lineA || fw == ui->lineB || fw == ui->lineC ||
                        fw == ui->lineD || fw == ui->lineE || fw == ui->lineF ||
                        fw == ui->lineS)
                    return;
            }

            bool meshOk = advanceGeodesicFlowBy(m_geoAnimTimer->interval() / 1000.0);
            if (!meshOk) {
                if (m_geoErrorType == GeoError::Singularity) {
                    InputValidator::showAnimatedGeodesicSingularityError(this);
                }
            }
        });
    }

    // Il timer del ricalcolo geodetico (animazione di t nella metrica/condizioni)
    // dipende dallo stato logico "in moto", non dal TESTO del bottone: al primo
    // Start il bottone diventa "STOP" solo dopo updateGeodesicMesh, quindi qui
    // leggerlo darebbe ancora "START" e il timer non partirebbe mai.
    // Il flusso appartiene al modulo Equations: se l'utente l'ha fermato col
    // suo Stop (performEquationsStop), un ricalcolo mesh qualsiasi (slider
    // costanti, navigazione) NON deve riavviarlo. Run del dock / master Start
    // riarmano il flag prima di arrivare qui.
    // Durante la registrazione il tempo lo detta il loop del recorder
    // (advanceGeodesicFlowBy per frame): il timer asincrono resta fermo,
    // altrimenti i suoi tick nei processEvents avanzerebbero geoTime due volte.
    // useAppliedEquations distingue i RUN veri (false: master Start, Run del dock,
    // avvio del flusso) dai ricalcoli DI SERVIZIO (true: debounce di costanti e
    // Steps, Invio su un singolo campo, commit di un limite). Solo un Run puo'
    // AVVIARE il flusso: un ricalcolo di servizio lo mantiene se gia' in corsa,
    // ma non lo fa partire.
    // Senza questa condizione bastava scrivere 't' in un campo del flusso e poi
    // muovere uno slider: il ricalcolo del debounce rileggeva allora i 7 campi
    // dal vivo, vedeva il 't' appena scritto e faceva partire il timer da solo.
    // I tasti passavano a "Stop" -- il master pure -- senza che l'utente avesse
    // premuto Run, e per l'utente l'animazione non era partita affatto: era
    // partito il timer, non l'attesa applicazione delle equazioni. Oggi anche i
    // 7 campi sono congelati sui ricalcoli di servizio, quindi un 't' non
    // confermato non arriva nemmeno fin qui: la guardia resta come rete.
    const bool runCanStartFlow = !useAppliedEquations;
    if (hasTime && !m_masterStopped && !m_userStoppedGeomClock && !m_isRecording) {
        // Il flusso puo' ESSERE AVVIATO solo da un Run vero. La condizione va
        // QUI, sull'avvio, e non sull'if esterno: messa la' faceva cadere nel
        // ramo else ogni ricalcolo di servizio, che FERMA un timer gia' in
        // corsa -- il moto si arrestava modificando un limite ad animazione
        // avviata. Un ricalcolo di servizio non avvia e non ferma: lascia il
        // flusso com'e'.
        if (!m_geoAnimTimer->isActive() && runCanStartFlow) {
            m_geoAnimTimer->start();
            // Il bottone master deve riflettere subito il timer appena avviato,
            // senza dipendere dal fatto che un chiamante a valle richiami
            // updateMasterButtonState: lo facciamo qui, fuori dai tick (durante
            // l'animazione il bottone è già coerente e non va ritoccato).
            if (!m_inGeoAnimTick) updateMasterButtonState();
        }
    } else {
        if (m_geoAnimTimer->isActive()) {
            m_geoAnimTimer->stop();
            if (!m_inGeoAnimTick) updateMasterButtonState();
        }
    }

    m_geoErrorShown = false;
    m_geodesicErrorPending = false;
    return true;
}

bool MainWindow::advanceGeodesicFlowBy(double dtSeconds)
{
    // Stessa velocita' del tick live: 0.015 unita' di geoTime per tick
    // nominale del timer (16 ms). Il tick passa il proprio intervallo e
    // avanza esattamente di 0.015; il recorder passa il dt virtuale del
    // frame (1/fps) e il video riproduce la velocita' vista a schermo.
    const int intervalMs = (m_geoAnimTimer && m_geoAnimTimer->interval() > 0)
                               ? m_geoAnimTimer->interval() : 16;
    const double step = 0.015 * (dtSeconds * 1000.0 / intervalMs);
    m_geoTime += step;

    m_inGeoAnimTick = true;
    // useAppliedLimits: il tick del moto NON deve raccogliere il testo dei campi
    // a meta' digitazione (vedi updateGeodesicMesh). Il dominio cambia quando
    // l'utente conferma con l'Invio, non mentre scrive.
    bool meshOk = updateGeodesicMesh(/*useAppliedLimits=*/true);
    m_inGeoAnimTick = false;
    return meshOk;
}

void MainWindow::checkAndTriggerMeshUpdate(bool useAppliedEquations) {
    if (!ui->glWidget) return;

    // Ray marching (tab implicito): la superficie è calcolata per pixel dallo
    // shader, non è una mesh poligonale. Cambiare "steps" agisce su setRaySteps,
    // non sulla griglia: qui basta un update() delle uniform. NB: lo script
    // PARAMETRICO (tab 0, es. tubi come Otto) è invece una mesh poligonale la cui
    // densità dipende da numU/numV (setResolution -> computeMesh): DEVE rigenerare,
    // altrimenti lo slider Steps risulta inerte sugli script parametrici. Prima
    // l'early-return copriva ogni script (isScriptModeActive) e bloccava proprio
    // quel caso.
    if (implicitMode()
        && ui->glWidget->getEngine() && ui->glWidget->getEngine()->isScriptModeActive()) {
        ui->glWidget->update(); // Aggiorna solo la visualizzazione (Uniforms)
        return;                 // Uscita anticipata per proteggere la GPU
    }

    // 1. Recupero equazioni principali
    QString mainEqs = m_scene.eq.x + " " + m_scene.eq.y + " " + m_scene.eq.z + " " + m_scene.eq.p;

    // 2. Analisi variabili composte (U, V, W)
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);

    // 3. Verifica presenza campi Geodetici
    bool geoHasText = hasGeodesicText();

    // 4. Routing: se Geodetico è attivo e siamo nel tab Parametrico (0), usa il
    // calcolatore Tensoriale. Lo script metrico forza il routing geodetico anche
    // se la mappa di visualizzazione X/Y/Z/P non cita U/V/W.
    const bool metricScriptActive = !m_metricScriptBody.trimmed().isEmpty();
    if ((upperCount > 0 || metricScriptActive) && geoHasText && (!implicitMode())) {
        if (m_geodesicErrorPending) return;

        bool success = updateGeodesicMesh(/*useAppliedLimits=*/false,
                                          useAppliedEquations);
        if (!success) {
            // Allinea il percorso Script al percorso dock (onStartClicked): se il
            // flusso è degenerato in una singolarità, l'avviso va mostrato anche
            // qui, altrimenti il Run dallo Script fallisce in silenzio. La guard
            // geoErrorShown evita doppioni se più chiamanti si concatenano; i
            // rami "nonfinite"/"syntax" mostrano già da soli il loro popup.
            if (m_geoErrorType == GeoError::Singularity
                    && !m_geoErrorShown) {
                m_geoErrorShown = true;
                InputValidator::showGeodesicSingularityError(this);
            }
            return;
        }
    } else {
        // Altrimenti, rigenera la griglia poligonale standard
        ui->glWidget->updateSurfaceData();
        ui->glWidget->update();
    }
}

void MainWindow::stopGeodesicAnimation()
{
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();

    if (m_btnStart) m_btnStart->setText("START");

    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
        if (!ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    }
}

bool MainWindow::isGeodesicMotionActive() const {
    // Un tick in corso conta come moto attivo anche a timer FERMO: durante la
    // registrazione il tempo lo detta il loop (advanceGeodesicFlowBy alza
    // m_inGeoAnimTick) e il timer resta spento per contratto. Senza questo,
    // updateGeodesicMesh nel REC saltava il disaccoppiamento scritto/applicato e
    // leggeva i campi UI (vuoti/diversi negli script metrici): la superficie
    // si appiattiva in una lamina solo in registrazione.
    return m_inGeoAnimTick || (m_geoAnimTimer && m_geoAnimTimer->isActive());
}

bool MainWindow::hasGeodesicText() const {
    if (!ui->lnU) return false;
    return !m_scene.eq.geoU.trimmed().isEmpty()  ||
           !m_scene.eq.geoV.trimmed().isEmpty()  ||
           !m_scene.eq.geoW.trimmed().isEmpty()  ||
           !m_scene.eq.geoDU.trimmed().isEmpty() ||
           !m_scene.eq.geoDV.trimmed().isEmpty() ||
           !m_scene.eq.geoDW.trimmed().isEmpty();
}

bool MainWindow::geodesicFieldsAreFinite(const QStringList& exprs,
                                         float uMin, float uMax,
                                         float vMin, float vMax,
                                         float A, float B, float C,
                                         float D, float E, float F, float S)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    // Variabili legate per riferimento. u,v,t li facciamo variare;
    // gli altri restano a un valore interno "sicuro" così le espressioni che li
    // citano compilano e non incappano nella loro singolarità naturale di bordo
    // (es. il fattore conforme di Poincaré che esplode solo a U^2+V^2+W^2 = 1).
    double u = 0, v = 0, t = 0;
    double w = 0.137, U = 0.137, V = 0.137, W = 0.137;
    double x = 0.137, y = 0.137, z = 0.137, iTime = 0, u_time = 0;

    symbol_table_t st;
    st.add_constants();
    st.add_constant("pi", 3.14159265358979323846);
    st.add_constant("PI", 3.14159265358979323846);
    st.add_constant("e",  2.71828182845904523536);
    st.add_constant("tau", 6.28318530717958647692);
    st.add_constant("TAU", 6.28318530717958647692);
    st.add_constant("A", (double)A); st.add_constant("B", (double)B);
    st.add_constant("C", (double)C); st.add_constant("D", (double)D);
    st.add_constant("E", (double)E); st.add_constant("F", (double)F);
    st.add_constant("S", (double)S); st.add_constant("s", (double)S);
    st.add_variable("u", u); st.add_variable("v", v); st.add_variable("w", w);
    st.add_variable("t", t);
    st.add_variable("U", U); st.add_variable("V", V); st.add_variable("W", W);
    st.add_variable("x", x); st.add_variable("y", y); st.add_variable("z", z);
    st.add_variable("iTime", iTime); st.add_variable("u_time", u_time);

    // Punti campione interni: evitiamo gli estremi esatti per non scambiare una
    // singolarità legittima di bordo (es. il fattore conforme di Poincaré, che
    // esplode solo a U^2+V^2+W^2 = 1) per un errore.
    const double fu = uMax - uMin, fv = vMax - vMin;
    const double us[3] = { uMin + 0.25*fu, uMin + 0.5*fu, uMin + 0.75*fu };
    const double vs[3] = { vMin + 0.25*fv, vMin + 0.5*fv, vMin + 0.75*fv };
    const double ts[3] = { 0.0, 0.5, 1.0 };

    for (const QString& raw : exprs) {
        QString clean = raw.trimmed();
        if (clean.isEmpty()) continue;
        clean.replace(",", ".");

        expression_t expr;
        expr.register_symbol_table(st);
        parser_t parser;
        if (!parser.compile(clean.toStdString(), expr))
            continue;   // errore di sintassi: lo gestisce la validazione esistente

        for (double su : us)
            for (double sv : vs)
                for (double stime : ts) {
                    u = su; v = sv; t = stime; iTime = stime; u_time = stime;
                    if (!isMeshSafeValue(expr.value()))
                        return false;
                }
    }
    return true;
}
