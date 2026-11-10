// mainwindow.cpp - MainWindow: costruttore (cablaggio di dock, segnali e filtri
// d'input), distruttore ed eventi della finestra.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"
#include "docsearch.h"


class DesktopInputFilter : public QObject {
public:
    DesktopInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                const QString on = obj->objectName();
                if (on == "txtScriptEditor" || on == "lineVariations" || on == "lineTexture") {
                    return false;
                }

                // Campi del modulo equazioni (principali, composizioni, vincoli,
                // flusso geodetico). Due regimi, ed e' la stessa regola per
                // TUTTI i campi del modulo, limiti u/v/w compresi:
                //  - superficie FERMA: l'Invio non esegue nulla, si aspetta il
                //    Run. Applicare a meta' digitazione mostrerebbe una forma
                //    che non corrisponde a cio' che si sta scrivendo (X
                //    aggiornata, Y e Z ancora vecchie).
                //  - superficie IN MOTO: il modulo e' gia' avviato e il tasto
                //    e' "Stop", quindi un Run da premere non c'e'; l'Invio
                //    applica al volo (commitUiFieldsDuringMotion), senza il
                //    giro Stop+Run che spezzerebbe l'animazione.
                if (isDeferredEquationField(on)) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    return true;
                }

                // Campi del sotto-tab Tubes. La CURVA segue la regola dei campi
                // qui sopra (ferma aspetta il Run, in moto applica al volo); lo
                // SPESSORE si applica alla conferma, cioe' all'editingFinished
                // che il clearFocus emette.
                if (isTubeEquationField(on)) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (on == QLatin1String("lineTubeThickness"))
                            mainWin->commitTubeThicknessOnEnter();   // vuoto -> popup
                        else if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    return true;
                }

                // LIMITI U/V/W: hanno un ramo proprio ma seguono la stessa
                // regola dei campi qui sopra -- a superficie ferma il dominio
                // aspetta il Run, in moto entra subito. Il ramo separato serve
                // perche' commitLimitFieldOnEnter, oltre ad applicare, VALIDA il
                // numero (popup su valore illeggibile o min>=max) e registra il
                // dominio nell'engine anche quando non si ridisegna.
                // NON si passa da commitFieldsOnEnter: quella strada rifa' un
                // Run intero, e committerebbe anche equazioni modificate ma non
                // ancora confermate. Qui si tocca il solo dominio.
                if (on == "uMinEdit" || on == "uMaxEdit" ||
                    on == "vMinEdit" || on == "vMaxEdit" ||
                    on == "wMinEdit" || on == "wMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitLimitFieldOnEnter(on);
                    }
                    return true;
                }

                // Limiti PER-MESH: ramo proprio per la stessa ragione dei
                // globali qui sopra (il commit valida e avvisa), ma con la
                // funzione gemella -- questi scrivono il dominio della sola
                // parte selezionata, non quello della superficie.
                if (on == "meshUMinEdit" || on == "meshUMaxEdit" ||
                    on == "meshVMinEdit" || on == "meshVMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitMeshLimitFieldOnEnter(on);
                    }
                    return true;
                }

                // Campo MESHES (pannello Multi Mesh): si conferma solo lui. Il
                // ramo generico qui sotto rifarebbe anche il commit dei campi
                // delle equazioni. Il commit si chiama qui e non si lascia al
                // solo editingFinished, come per lo spessore dei tubi: se il
                // campo non aveva davvero il focus, il clearFocus non lo emette.
                // Un secondo commit non cambia nulla (numero gia' applicato).
                if (on == "lineMeshCount") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent()))
                        mainWin->commitMeshCountOnEnter();
                    return true;
                }

                // Campi path camera: l'Invio AVVIA il path se e' fermo e i campi
                // lo definiscono, lo ricompila al volo se e' gia' in corsa, e lo
                // ferma se i campi sono stati svuotati (vedi
                // commitPathFieldOnEnter, che tiene i tre casi in un punto solo).
                if (isPathEquationField(on)) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitPathFieldOnEnter(on);
                    }
                    return true;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    MainWindow* mainWin = qobject_cast<MainWindow*>(parent());

                    // UN SOLO COMMIT PER INVIO. clearFocus() emette subito
                    // editingFinished, e per i campi COSTANTE quell'handler
                    // chiama gia' commitFieldsOnEnter. Chiamandolo di nuovo qui
                    // sotto, onStartClicked girava due volte e ogni validatore
                    // al suo interno mostrava il suo popup due volte di fila
                    // (equazioni incomplete, sintassi, parentesi, uso di W...):
                    // leggono i campi dal vivo, quindi la seconda passata
                    // ritrovava lo stesso testo sbagliato. Si confronta il
                    // contatore attorno al clearFocus(): se e' cambiato, il
                    // commit e' gia' avvenuto e qui non si ripete.
                    const quint64 before = mainWin ? mainWin->commitOnEnterCount() : 0;
                    w->clearFocus();

                    // Applica i campi modificati (sia in moto che a superficie ferma)
                    if (mainWin && mainWin->commitOnEnterCount() == before) {
                        mainWin->commitFieldsOnEnter();
                    }
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

// ==========================================
// Filtro per Mobile: Tastiera e Navigazione
// ==========================================
#if defined(Q_OS_IOS)
// Ultimo editor la cui selezione è stata modificata coi pallini: quando il
// trascinamento entra nella tastiera, il gesto del plugin la chiude azzerando
// il focus (focusWidget() diventa nullo) — questo puntatore permette comunque
// di ripresentare il menu sulla selezione superstite (vedi visibleChanged).
static QPointer<QWidget> s_lastSelectionEditor;

// Autoscroll durante la selezione coi pallini. Trascinando il pallino fino
// al bordo del campo visibile il testo non scorre (Qt non scrolla finché il
// tocco resta nel viewport) e la selezione muore lì. Il trigger è
// QPlainTextEdit::selectionChanged (connesso nel setup iOS): il drag dei
// pallini non genera eventi mouse e il canale QInputMethodEvent non è un
// flusso continuo (entrambi provati su device), mentre il segnale scatta a
// ogni variazione qualunque sia il canale. Quando l'estremo MOBILE della
// selezione entra nella fascia al bordo del viewport (in basso tagliata
// dalla tastiera), un timer scrolla un passo per tick; il plugin ri-mappa
// il dito fermo sul testo che gli scorre sotto ed estende da solo la
// selezione (è il meccanismo osservato nella "valanga" del
// keyboard-avoidance — qui però a passo fisso, vincolato alla fascia).
// La tastiera NON è un requisito: il gesto nascondi-tastiera del plugin
// la uccide spesso a metà drag, ma pallini e drag sopravvivono.
class SelectionAutoScroller : public QObject {
public:
    static SelectionAutoScroller* instance() {
        static SelectionAutoScroller s;
        return &s;
    }

    // Chiamata su ogni selectionChanged. La valutazione è accodata per
    // lavorare a stato del widget assestato (il segnale può arrivare nel
    // mezzo dell'applicazione di un evento).
    void onSelectionEvent(QPlainTextEdit* editor) {
        QMetaObject::invokeMethod(this, [this, ed = QPointer<QPlainTextEdit>(editor)]() {
            if (ed)
                evaluate(ed.data());
        }, Qt::QueuedConnection);
    }

private:
    static constexpr int kBandPx = 60;   // fascia di attivazione al bordo
    static constexpr int kTickMs = 90;   // ~11 righe/secondo

    SelectionAutoScroller() {
        m_timer.setInterval(kTickMs);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void evaluate(QPlainTextEdit* editor) {
        const QTextCursor c = editor->textCursor();
        if (!c.hasSelection()) {
            stop();
            return;
        }
        if (editor != m_editor) {
            m_editor = editor;
            m_lastAnchor = m_lastPosition = -1;
            m_movingEndIsPosition = true;
        }
        // L'estremo mobile è quello cambiato rispetto all'evento precedente.
        if (c.position() != m_lastPosition)
            m_movingEndIsPosition = true;
        else if (c.anchor() != m_lastAnchor)
            m_movingEndIsPosition = false;
        m_lastAnchor = c.anchor();
        m_lastPosition = c.position();

        if (bandDirection(editor) != 0) {
            // start() su timer attivo lo RIAZZERA e durante il drag i
            // selectionChanged arrivano di continuo: il tick non
            // arriverebbe mai.
            if (!m_timer.isActive()) {
                m_stallTicks = 0;
                m_tickAnchor = m_tickPosition = -1;
                m_timer.start();
            }
        } else {
            stop();
        }
    }

    // Direzione di scroll dalla posizione dell'estremo mobile della
    // selezione rispetto alle fasce sul bordo visibile del viewport (quello
    // inferiore tagliato dalla tastiera). Si usa l'estremo mobile, non il
    // dito: durante il drag dei pallini non esiste alcun evento con la
    // posizione del tocco, ma il pallino sta comunque sul carattere
    // dell'estremo che trascina.
    int bandDirection(QPlainTextEdit* editor) const {
        QWidget* viewport = editor->viewport();
        QRect vis = viewport->visibleRegion().boundingRect();
        if (vis.isEmpty())
            vis = viewport->rect();
        int bottomEdge = vis.bottom();
        const QRectF kb = QGuiApplication::inputMethod()->keyboardRectangle();
        if (!kb.isEmpty()) {
            const int kbTop = viewport->mapFrom(viewport->window(), kb.topLeft().toPoint()).y();
            if (kbTop > vis.top())
                bottomEdge = qMin(bottomEdge, kbTop);
        }
        QTextCursor moving = editor->textCursor();
        if (!m_movingEndIsPosition)
            moving.setPosition(moving.anchor());
        const QRect r = editor->cursorRect(moving);
        if (r.bottom() >= bottomEdge - kBandPx)
            return +1;
        if (r.top() <= vis.top() + kBandPx)
            return -1;
        return 0;
    }

    void tick() {
        // NESSUN vincolo sulla visibilità della tastiera: il gesto
        // nascondi-tastiera del plugin la uccide spesso proprio durante il
        // drag del pallino (il tocco entra nel keyboardEndRect), ma pallini
        // e drag sopravvivono e l'autoscroll deve continuare (visto su
        // device: selezione che cresceva con la tastiera già morta).
        QPlainTextEdit* editor = m_editor.data();
        if (!editor || !editor->textCursor().hasSelection()) {
            stop();
            return;
        }
        const int dir = bandDirection(editor);
        if (dir == 0) {
            stop();
            return;
        }
        // Criterio di vita = il feedback scroll→estensione: se il drag è
        // attivo, il plugin ri-mappa il dito sul testo scrollato e la
        // selezione avanza tra un tick e l'altro. Ferma per 2 tick = drag
        // finito o cambio programmatico (es. Select All): fermarsi qui
        // evita di scrollare da soli fino a fine documento.
        const QTextCursor cur = editor->textCursor();
        if (cur.anchor() == m_tickAnchor && cur.position() == m_tickPosition) {
            if (++m_stallTicks >= 2) {
                stop();
                return;
            }
        } else {
            m_stallTicks = 0;
            m_tickAnchor = cur.anchor();
            m_tickPosition = cur.position();
        }

        // Prima la scrollbar dell'editor (in RIGHE), ma su mobile l'editor
        // spesso mostra tutto il testo ed è la QScrollArea del dock a
        // scorrere (in pixel). A fine corsa non c'è nulla da fare: la
        // selezione smette di avanzare e il criterio qui sopra ferma tutto.
        QScrollBar* bar = editor->verticalScrollBar();
        if (bar && bar->maximum() > bar->minimum()) {
            bar->setValue(bar->value() + dir);
        } else {
            const int stepPx = qMax(1, editor->fontMetrics().lineSpacing());
            for (QWidget* w = editor->parentWidget(); w; w = w->parentWidget()) {
                if (auto* area = qobject_cast<QAbstractScrollArea*>(w)) {
                    QScrollBar* outer = area->verticalScrollBar();
                    if (outer && outer->maximum() > outer->minimum()) {
                        outer->setValue(outer->value() + dir * stepPx);
                        break;
                    }
                }
            }
        }
    }

    void stop() { m_timer.stop(); }

    QPointer<QPlainTextEdit> m_editor;
    int m_lastAnchor = -1;
    int m_lastPosition = -1;
    bool m_movingEndIsPosition = true;
    int m_tickAnchor = -1;
    int m_tickPosition = -1;
    int m_stallTicks = 0;
    QTimer m_timer;
};
#endif

class MobileInputFilter : public QObject {
    bool m_isProcessingQuery = false; // Evita loop infiniti durante l'intercettazione
public:
    MobileInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {

        // 1. MENU CONTESTUALI
        if (event->type() == QEvent::ContextMenu) {
#if defined(Q_OS_ANDROID)
            // Mouse esterno / DeX: stesso menu completo, senza selezione parola.
            QContextMenuEvent* cme = static_cast<QContextMenuEvent*>(event);
            showEditMenu(obj, cme->globalPos(), false);
            return true;
#elif defined(Q_OS_IOS)
            // Su iOS il menu lo presentiamo noi con UIEditMenuInteraction
            // (ioseditmenu.mm): i percorsi Qt sono entrambi morti sugli iOS
            // recenti (UIMenuController e' un no-op da iOS 17, il QMenu
            // widget non viene renderizzato). L'evento arriva a fine lente
            // (long press) e al tap sul cursore; le voci si adattano alla
            // selezione (Cut/Copy solo se esiste, senno' Paste/Select All/
            // Undo/Redo). Il tap secco altrove resta solo tastiera: per
            // quello il plugin non genera alcun evento.
            if (qobject_cast<QPlainTextEdit*>(obj) || qobject_cast<QLineEdit*>(obj)) {
                iosPresentEditMenu(qobject_cast<QWidget*>(obj),
                    static_cast<QContextMenuEvent*>(event)->globalPos());
                return true; // consumato: niente QMenu Qt ne' fallback del plugin
            }
            return false;
#endif
        }

        // NB (iOS): il doppio tap seleziona la parola (comportamento Qt
        // nativo) ma NON presenta più il menu, per scelta: il menu copriva
        // la zona dei pallini e intralciava l'estensione della selezione.
        // Il menu arriva al rilascio dei pallini, a fine lente (long press)
        // e — via visibleChanged — se la tastiera muore con una selezione.

#if defined(Q_OS_ANDROID)
        // 1b. LONG-PRESS: il mini-popup di sistema (lato Java) è disattivato con
        // QT_QPA_NO_TEXT_HANDLES in main(). Riproduciamo qui il comportamento
        // nativo: selezione della parola sotto il dito + menu completo di Qt
        // (undo, redo, taglia, copia, incolla, elimina, seleziona tutto).
        if (event->type() == QEvent::Gesture) {
            QGestureEvent* ge = static_cast<QGestureEvent*>(event);
            if (QGesture* g = ge->gesture(Qt::TapAndHoldGesture)) {
                if (g->state() == Qt::GestureFinished) {
                    // position() del TapAndHold è in coordinate globali (schermo)
                    showEditMenu(obj, static_cast<QTapAndHoldGesture*>(g)->position().toPoint(), true);
                }
                ge->accept(g);
                return true;
            }
        }
#endif

#if defined(Q_OS_IOS)
        // 1c. Il menu di modifica presentato (UIEditMenuInteraction) non si
        // chiude da solo quando si digita: lo congediamo al primo input di
        // tastiera. Sulla tastiera virtuale il testo arriva come evento
        // input-method, ma conta SOLO se porta testo (commit o preedit): il
        // trascinamento dei pallini genera QInputMethodEvent di sola
        // selezione, che non devono chiudere il menu a fine trascinamento.
        // Su quegli eventi di sola-selezione ricordiamo invece l'editor: se
        // il trascinamento entra nella tastiera e la uccide (gesto del
        // plugin, resign -> focus perso), il connect su visibleChanged usa
        // questo puntatore per ripresentare il menu sulla selezione rimasta.
        if (event->type() == QEvent::KeyPress) {
            iosDismissEditMenu();
        } else if (event->type() == QEvent::InputMethod) {
            auto* ime = static_cast<QInputMethodEvent*>(event);
            if (!ime->commitString().isEmpty() || !ime->preeditString().isEmpty()) {
                iosDismissEditMenu();
            } else if (auto* w = qobject_cast<QWidget*>(obj)) {
                s_lastSelectionEditor = w;
            }
        }
#endif

        // 2. GESTIONE TASTI INVIO E TAB
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            // Intercettiamo Return, Enter e Tab
            if (keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Tab) {

                if (obj->objectName() == "txtScriptEditor") {
                    return false;
                }

                // Campi del modulo equazioni: stessi due regimi del filtro
                // desktop -- fermo aspettano il Run, in moto l'Invio applica al
                // volo. Il ramo generico qui sotto chiamerebbe comunque
                // commitUiFieldsDuringMotion, ma senza distinguere i due casi.
                if (isDeferredEquationField(obj->objectName())) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Campi del sotto-tab Tubes: come nel filtro desktop (la curva
                // in moto applica al volo, lo spessore alla conferma).
                if (isTubeEquationField(obj->objectName())) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (obj->objectName() == QLatin1String("lineTubeThickness"))
                            mainWin->commitTubeThicknessOnEnter();   // vuoto -> popup
                        else if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Campi path camera: avvia / ricompila / ferma secondo lo stato,
                // come nel filtro desktop.
                if (isPathEquationField(obj->objectName())) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitPathFieldOnEnter(obj->objectName());
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Limiti U/V/W: stessa regola e stesso ramo del filtro desktop
                // (validazione + registrazione del dominio; a schermo subito
                // solo se la superficie e' in moto). Senza questo ramo
                // cadrebbero nel generico qui sotto (commitUiFieldsDuringMotion),
                // che e' un'altra cosa.
                const QString limitName = obj->objectName();
                if (limitName == "uMinEdit" || limitName == "uMaxEdit" ||
                    limitName == "vMinEdit" || limitName == "vMaxEdit" ||
                    limitName == "wMinEdit" || limitName == "wMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitLimitFieldOnEnter(limitName);
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Limiti PER-MESH: stesso ramo del filtro desktop.
                if (limitName == "meshUMinEdit" || limitName == "meshUMaxEdit" ||
                    limitName == "meshVMinEdit" || limitName == "meshVMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitMeshLimitFieldOnEnter(limitName);
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Campo MESHES: come nel filtro desktop.
                if (limitName == "lineMeshCount") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent()))
                        mainWin->commitMeshCountOnEnter();
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    MainWindow* mainWin = qobject_cast<MainWindow*>(parent());

                    // UN SOLO COMMIT PER INVIO, come nel filtro desktop:
                    // clearFocus() emette editingFinished, che per i campi
                    // costante commtta gia'. Vedi commitOnEnterCount.
                    const quint64 before = mainWin ? mainWin->commitOnEnterCount() : 0;
                    w->clearFocus();

                    // Notifica a MainWindow di applicare i campi modificati
                    if (mainWin && mainWin->commitOnEnterCount() == before) {
                        mainWin->commitUiFieldsDuringMotion();
                    }

                    QGuiApplication::inputMethod()->hide();
                    return true;
                }
            }
        }

        return QObject::eventFilter(obj, event);
    }

#if defined(Q_OS_ANDROID)
private:
    // Menu di modifica completo, lo stesso che Qt mostra su desktop.
    // selectWordUnderFinger: su long-press emula il comportamento nativo
    // selezionando la parola sotto il dito (se non c'è già una selezione).
    void showEditMenu(QObject* obj, const QPoint& globalPos, bool selectWordUnderFinger) {
        QMenu* menu = nullptr;

        if (auto* pte = qobject_cast<QPlainTextEdit*>(obj)) {
            if (selectWordUnderFinger && !pte->textCursor().hasSelection()) {
                QTextCursor c = pte->cursorForPosition(pte->viewport()->mapFromGlobal(globalPos));
                c.select(QTextCursor::WordUnderCursor);
                if (c.hasSelection()) pte->setTextCursor(c);
            }
            menu = pte->createStandardContextMenu();
        } else if (auto* le = qobject_cast<QLineEdit*>(obj)) {
            if (selectWordUnderFinger && !le->hasSelectedText()) {
                const QString t = le->text();
                const int pos = le->cursorPositionAt(le->mapFromGlobal(globalPos));
                auto isWordChar = [](QChar ch){ return ch.isLetterOrNumber() || ch == '_'; };
                int s = pos, e = pos;
                while (s > 0 && isWordChar(t.at(s - 1))) --s;
                while (e < t.size() && isWordChar(t.at(e))) ++e;
                if (e > s) le->setSelection(s, e - s);
            }
            menu = le->createStandardContextMenu();
        }

        if (!menu) return;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    }
#endif
};

#if defined(Q_OS_IOS)
// ==========================================
// Filtro iOS: tap-vs-scroll sugli editor
// ==========================================
// Su iOS la tastiera si apre al focus-in, e il focus arriva già col PRESS:
// bastava toccare un QPlainTextEdit per iniziare uno scroll e la tastiera
// compariva senza alcuna intenzione di digitare. Il press su un editor NON
// focalizzato viene trattenuto: se il dito si muove è uno scroll (muoviamo
// noi il contenuto dell'editor, o la QScrollArea che lo contiene se l'editor
// non ha nulla da scrollare), se resta fermo è un tap e il focus — quindi la
// tastiera — scatta solo al rilascio. Un editor già focalizzato mantiene il
// comportamento nativo (posizionamento cursore, selezione).
class TapVsScrollFilter : public QObject {
public:
    TapVsScrollFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* viewport = qobject_cast<QWidget*>(obj);
        auto* editor = viewport ? qobject_cast<QPlainTextEdit*>(viewport->parentWidget()) : nullptr;
        if (!editor)
            return QObject::eventFilter(obj, event);

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton || editor->hasFocus())
                return false;
            m_pending = true;
            m_dragging = false;
            m_pressPos = m_lastPos = me->globalPosition();
            m_scrollRemainder = 0.0;
            return true; // niente focus al press = niente tastiera
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (!m_pending)
                return false;
            const QPointF pos = me->globalPosition();
            if (!m_dragging && (pos - m_pressPos).manhattanLength() > kDragThreshold)
                m_dragging = true;
            if (m_dragging) {
                scrollBy(editor, m_lastPos.y() - pos.y());
                m_lastPos = pos;
            }
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_pending) return false;
            m_pending = false;
            if (!m_dragging) {
                auto* me = static_cast<QMouseEvent*>(event);
                editor->setFocus(Qt::MouseFocusReason);
                editor->setTextCursor(editor->cursorForPosition(me->position().toPoint()));
            }
            return true;
        }
        default:
            return QObject::eventFilter(obj, event);
        }
    }

private:
    static constexpr int kDragThreshold = 12; // px: oltre = scroll, sotto = tap

    void scrollBy(QPlainTextEdit* editor, qreal dyPixels) {
        QScrollBar* bar = editor->verticalScrollBar();
        if (bar && bar->maximum() > bar->minimum()) {
            // La scrollbar verticale del QPlainTextEdit lavora in righe, non in pixel
            m_scrollRemainder += dyPixels / qMax(1, editor->fontMetrics().lineSpacing());
            const int lines = int(m_scrollRemainder);
            if (lines != 0) {
                bar->setValue(bar->value() + lines);
                m_scrollRemainder -= lines;
            }
            return;
        }
        for (QWidget* w = editor->parentWidget(); w; w = w->parentWidget()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(w)) {
                QScrollBar* outer = area->verticalScrollBar();
                if (outer && outer->maximum() > outer->minimum()) {
                    outer->setValue(outer->value() + qRound(dyPixels));
                    return;
                }
            }
        }
    }

    bool m_pending = false;
    bool m_dragging = false;
    QPointF m_pressPos;
    QPointF m_lastPos;
    qreal m_scrollRemainder = 0.0;
};
#endif // Q_OS_IOS

class EnterApplyFilter : public QObject {
public:
    std::function<void()> onEnter;
    EnterApplyFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    if (onEnter) onEnter();
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // =========================================================================
    // 1. CORE INITIALIZATION
    // =========================================================================
    ui->setupUi(this);
    bindEquationFields();   // prima di ogni altro connect sui campi delle equazioni
    bindConstantFields();   // ...e su quelli delle costanti e lo slider Steps
    bindLineFields();       // ...e su limiti e path

    // FULL IMMERSION
    if (this->centralWidget() && this->centralWidget()->layout()) {
        this->centralWidget()->layout()->setContentsMargins(0, 0, 0, 0);
        this->centralWidget()->layout()->setSpacing(0);
    }

    //QSettings().clear(); // Primo avvio dell'applicazione

    setWindowTitle("Surface Explorer");
    setAttribute(Qt::WA_AcceptTouchEvents);

    m_blockTextureGen = false;
    m_currentTexturePresetPath = "";
    m_scene.surfaceTextureState = false;

#if defined(Q_OS_ANDROID)
    // 1. Controlla prima l'API Level (Deve essere >= 30 per questa funzione)
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {

        // 2. Ora è sicuro chiamare isExternalStorageManager
        bool isStorageManager = QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");

        if (!isStorageManager) {
            QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                              QJniObject::fromString("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION").object<jstring>());

            // 3. Recupera dinamicamente il VERO nome del pacchetto dell'app, niente hardcoding!
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
            QString uriString = "package:" + packageName.toString();

            QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                QJniObject::fromString(uriString).object<jstring>());

            intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
            intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

            if (context.isValid()) {
                context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
            }
        }
    }

    connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
            this, [this]() {

        QRectF kbdRect = QGuiApplication::inputMethod()->keyboardRectangle();
        int kbdHeight = kbdRect.height();

        // 1. Assicuriamoci che il central widget (e la vista 3D) rimangano
        // intatti a schermo intero senza innescare la Status Bar.
        if (ui->centralwidget) {
            ui->centralwidget->setContentsMargins(0, 0, 0, 0);
        }

        // 2. Applichiamo la compensazione SOLO ai pannelli laterali
        QList<QDockWidget*> docks = {
            ui->dockEquations, ui->dockScripts, ui->dockRenders,
            ui->dock3D, ui->dock4D, ui->dockSurfaces
        };

        for (QDockWidget* dock : docks) {
            if (!dock->widget()) continue;
            // Il margine destro e' quello che tiene i controlli fuori dalla
            // scrollbar (vedi addScrollToDock): tocchiamo SOLO il bottom.
            QMargins m = dock->widget()->contentsMargins();
            if (kbdHeight > 0) {
                // Comprime solo il dock realmente visibile (quello su cui si digita).
                if (!dock->isVisible()) continue;
                m.setBottom(kbdHeight);
                dock->widget()->setContentsMargins(m);

                // Trova il campo di testo su cui stiamo digitando
                QWidget* fw = this->focusWidget();
                if (fw) {
                    // Un piccolo timer permette al layout di aggiornarsi prima di scorrere
                    QTimer::singleShot(100, fw, [fw]() {
                        QWidget* p = fw->parentWidget();
                        while (p) {
                            if (QScrollArea* sa = qobject_cast<QScrollArea*>(p)) {
                                // Forza lo scorrimento verso il campo, lasciando 50px di margine
                                sa->ensureWidgetVisible(fw, 0, 50);
                                break;
                            }
                            p = p->parentWidget();
                        }
                    });
                }
            } else {
                // Tastiera chiusa: ripristina SEMPRE il margine, anche se il dock non
                // e' visibile in questo istante (vedi ramo iOS): condizionarlo a
                // isVisible() lascia il dock dimezzato dopo un MobileSaveDialog.
                m.setBottom(0);
                dock->widget()->setContentsMargins(m);
            }
        }
    });

#endif

    // NOTA (2026-07-01): su iPhone NON comprimiamo i dock all'apparizione della
    // tastiera. Si era ipotizzato che il dock allungato dipendesse dalla tastiera,
    // ma iOS porta gia' i campi in-dock sopra la tastiera da solo e il dock si rompe
    // anche senza tastiera. Bug del dock ancora IRRISOLTO: vedi DOCK_BUG_REPORT.md.

    // --- SBLOCCO DEI CAMPI COSTANTI (Permette lettere, 'pi', formule) ---
    ui->lineA->setValidator(nullptr);
    ui->lineB->setValidator(nullptr);
    ui->lineC->setValidator(nullptr);
    ui->lineD->setValidator(nullptr);
    ui->lineE->setValidator(nullptr);
    ui->lineF->setValidator(nullptr);
    ui->lineS->setValidator(nullptr);

    setupStyling();
    setupDockLayout();
    setupActionsAndMenus();
    setupStatusBar();
    setupEquationsDock();
    setupRendererDock();
    setupMotionDocks();
    setupScriptDock();
    setupLibraryDock();
    finishStartup();
    setupDesktopFilters();
}

// Stile, font e, su mobile, compattazione dei dock e gestione della tastiera. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupStyling()
{
    // =========================================================================
    // 2. STYLING & FONTS
    // =========================================================================
    UiStyleManager::applyPlatformStyle(this);
    UiStyleManager::applyDarkTheme(this);

    QList<QWidget*> mainInputFields = {
        ui->lineX, ui->lineY, ui->lineZ, ui->lineP,
        ui->lineTubeX, ui->lineTubeY, ui->lineTubeZ, ui->lineTubeP,
        ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D,
        ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
        ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P
    };
    UiStyleManager::applyInputFieldsStyle(mainInputFields);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // ==========================================
    // 1. COMPATTAZIONE INTERFACCIA
    // ==========================================
    QList<QWidget*> dockContents = {
        ui->dockEquations->widget(),
        ui->dockRenders->widget(),
        //ui->dockScripts->widget(),
        ui->dock3D->widget(),
        ui->dock4D->widget(),
        ui->dockSurfaces->widget()
    };

    // CHIAMATA UNICA CENTRALIZZATA
    UiStyleManager::compactForMobile(dockContents);

    // ==========================================
    // FONT E LENTE IOS (VERSIONE CORRETTA VIA LAYOUT)
    // ==========================================
    QString mobileInputStyle = "QLineEdit, QPlainTextEdit, QTextEdit { font-size: 14px; }";

    if (ui->dockEquations->widget()) {
        QString currentEqStyle = ui->dockEquations->widget()->styleSheet();
        ui->dockEquations->widget()->setStyleSheet(currentEqStyle + " " + mobileInputStyle);
    }

    if (ui->dockScripts->widget()) {
        QWidget* originalWidget = ui->dockScripts->widget();

        // Applichiamo la dimensione del font pulita al widget originale
        QString currentScriptStyle = originalWidget->styleSheet();
        originalWidget->setStyleSheet(currentScriptStyle + " " + mobileInputStyle);

        // --- SOLUZIONE: INIEZIONE DI UNA QSCROLLAREA PER L'INTERO DOCK ---
        // Creiamo la scroll area che conterrà l'intero blocco del pannello script
        QScrollArea* dockScrollArea = new QScrollArea(ui->dockScripts);
        dockScrollArea->setWidgetResizable(true);
        dockScrollArea->setFrameShape(QFrame::NoFrame); // Rimuove bordi interni antiestetici di Qt
        dockScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dockScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // FORZATURA ALTEZZA: Garantisce che l'editor abbia sempre uno spazio di digitazione
        // umano (es. 300px) e non venga mai schiacciato a zero pixel dalla tastiera.
        ui->txtScriptEditor->setMinimumHeight(300);

        // Niente scroll cinetico a dito: sui pannelli con controlli interattivi
        // il TouchGesture selezionava/attivava i figli durante il trascinamento.
        // Come gli altri dock, si scrolla solo con la scroll bar.

        // Sostituiamo il widget principale del dock inserendovi la ScrollArea,
        // e mettiamo il vecchio widget all'interno della scroll area.
        dockScrollArea->setWidget(originalWidget);
        ui->dockScripts->setWidget(dockScrollArea);

    }


    // ==========================================
    // 2. GESTIONE TASTIERA E NAVIGAZIONE CURSORE
    // ==========================================
    MobileInputFilter* mobileFilter = new MobileInputFilter(this);
    QList<QWidget*> allTextInputs;

    // Riempiamo la lista iterando sui risultati per permettere il cast a QWidget*
    for(auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        allTextInputs.append(textEdit);
    }
    for(auto* lineEdit : this->findChildren<QLineEdit*>()) {
        allTextInputs.append(lineEdit);
    }

    for (QWidget* input : allTextInputs) {
        input->installEventFilter(mobileFilter);

#if defined(Q_OS_ANDROID)
        // Long-press -> menu di modifica completo (vedi MobileInputFilter):
        // il gesto parte anche se il tocco avviene sul viewport dei QPlainTextEdit,
        // perché il gesture manager risale la gerarchia fino al widget che ha il grab.
        input->grabGesture(Qt::TapAndHoldGesture);
#endif

        // Hint tastiera: niente predizione né maiuscole automatiche.
        // MultiLine SOLO per lo script editor: sugli altri campi la tastiera
        // mostra il tasto "Fatto/Vai" che innesca la chiusura via Invio.
        Qt::InputMethodHints hints = Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase;
        if (input->objectName() == "txtScriptEditor")
            hints |= Qt::ImhMultiLine;
        input->setInputMethodHints(hints);
    }

#if defined(Q_OS_IOS)
    // Tap-vs-scroll (vedi TapVsScrollFilter): va installato sul viewport,
    // è lì che arrivano i mouse event dei QPlainTextEdit.
    // NoFocus è essenziale: su iOS setFocusOnTouchRelease=true e il focus da
    // click viene assegnato al RILASCIO a livello di QWidgetWindow, PRIMA che
    // l'evento raggiunga i filtri sul viewport e senza controllare se il dito
    // ha scrollato. Con NoFocus quel percorso è neutralizzato e il focus lo dà
    // solo il filtro (setFocus programmatico ignora la policy) sul tap pulito.
    auto* tapVsScroll = new TapVsScrollFilter(this);
    for (auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        textEdit->setFocusPolicy(Qt::NoFocus);
        textEdit->viewport()->installEventFilter(tapVsScroll);
        // Trigger dell'autoscroll di selezione: scatta a ogni variazione
        // della selezione qualunque sia il canale con cui il plugin la
        // applica (il drag dei pallini non genera eventi mouse e il canale
        // IM non è un flusso continuo, provato su device).
        connect(textEdit, &QPlainTextEdit::selectionChanged, this, [textEdit]() {
            SelectionAutoScroller::instance()->onSelectionEvent(textEdit);
        });
    }

    // Se la tastiera sparisce mentre c'è una selezione attiva (trascinando
    // il pallino dentro l'area della tastiera scatta il gesto del plugin,
    // che fa resign E azzera il focus: pallini e menu muoiono), la selezione
    // superstite resterebbe inutilizzabile — qualsiasi tap per riaprire il
    // menu la cancella. Ripresentiamo quindi noi il menu: sul widget ancora
    // focalizzato se c'è, altrimenti sull'ultimo editor di cui i pallini
    // stavano modificando la selezione (il focus a quel punto è già nullo).
    // Cut/Copy/Paste agiscono sul widget e funzionano anche senza focus;
    // UIEditMenuInteraction vive anche senza tastiera.
    connect(QGuiApplication::inputMethod(), &QInputMethod::visibleChanged, this, []() {
        if (QGuiApplication::inputMethod()->isVisible())
            return;
        auto hasSelection = [](QWidget* w) {
            if (auto* pte = qobject_cast<QPlainTextEdit*>(w))
                return pte->textCursor().hasSelection();
            if (auto* le = qobject_cast<QLineEdit*>(w))
                return le->hasSelectedText();
            return false;
        };
        QWidget* editor = QApplication::focusWidget();
        if (!hasSelection(editor)) {
            editor = s_lastSelectionEditor.data();
            if (!hasSelection(editor))
                return;
        }
        // Campi che hanno dichiarato "niente menu di modifica" (proprieta'
        // noEditMenu): questo connect e' GLOBALE all'app e ripresenterebbe il
        // menu anche sui campi numerici dei dialoghi, dove il NoEditMenuFilter
        // blocca il QContextMenuEvent ma non questo percorso — che non passa da
        // li'. E' la via per cui il menu ricompariva sui campi del recorder.
        if (editor && editor->property("noEditMenu").toBool())
            return;
        const QRect r = editor->inputMethodQuery(Qt::ImCursorRectangle).toRect();
        const QPoint gp = editor->mapToGlobal(r.center());
        QTimer::singleShot(0, editor, [editor, gp]() { iosPresentEditMenu(editor, gp); });
    });
#endif

    UiStyleManager::setupRaymarchTabMobile(ui->dockEquations->widget());
#endif
}

// Disposizione dei dock e delle schede. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupDockLayout()
{
    // =========================================================================
    // 3. DOCK WIDGETS & TAB LAYOUT
    // =========================================================================
    // --- RIORDINO SCHEDE LIBRARY (FIX DESIGNER) ---
    ui->tabWidget->clear();
    // "Shapes" e non "Surfaces": il ramo tiene superfici, curve solide e forme
    // implicite, e "Surface" e' gia' il sotto-tab del Parametrico. Solo
    // l'etichetta: la cartella resta surfaces/, come chiavi e codice.
    ui->tabWidget->addTab(ui->Surface, "Shapes");
    ui->tabWidget->addTab(ui->Texture, "Textures");
    ui->tabWidget->addTab(ui->Sounds, "Sounds");
    ui->tabWidget->addTab(ui->Motions, "Records");
    ui->tabWidget->setCurrentIndex(0);
    // Niente frecce di scorrimento sui tab Library: il cambio si fa cliccando
    // la linguetta.
    if (ui->tabWidget->tabBar())
        ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    // Tab a piena larghezza: 4 tab su un dock bloccato a 400px. min-width fissa
    // (come il dockEquations, che funziona) tarata sui 400: ~76px/tab riempie
    // la barra. Col CSS globale setExpanding e' ignorato, la min-width no.
    ui->tabWidget->setStyleSheet(
        "QTabBar::tab { min-width: 76px; padding: 5px 10px; }");

    UiStyleManager::setupDockScroll(ui->dockEquations, true);
    UiStyleManager::setupDockScroll(ui->dockRenders, true);
    UiStyleManager::setupDockScroll(ui->dock3D, true);
    UiStyleManager::setupDockScroll(ui->dock4D, true);
    UiStyleManager::setupDockScroll(ui->dockScripts, true);
    UiStyleManager::setupDockScroll(ui->dockSurfaces, true);

    if (ui->dockEquations) UiStyleManager::addScrollToDock(ui->dockEquations);
    if (ui->dockRenders) UiStyleManager::addScrollToDock(ui->dockRenders);
    if (ui->dock3D) UiStyleManager::addScrollToDock(ui->dock3D);
    if (ui->dock4D) UiStyleManager::addScrollToDock(ui->dock4D);
    if (ui->dockScripts) UiStyleManager::addScrollToDock(ui->dockScripts);

    // --- BLOCCO SPOSTAMENTO DOCK ---
    auto lockDock = [](QDockWidget* dock) {
        dock->setFeatures(QDockWidget::DockWidgetClosable);
    };

    lockDock(ui->dockEquations);
    lockDock(ui->dockRenders);
    lockDock(ui->dock3D);
    lockDock(ui->dock4D);
    lockDock(ui->dockScripts);
    lockDock(ui->dockSurfaces);
    // --------------------------------

    // Diciamo a Qt di mettere TUTTI i pannelli a DESTRA (RightDockWidgetArea)
    addDockWidget(Qt::RightDockWidgetArea, ui->dockEquations);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockRenders);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock3D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock4D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockScripts);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockSurfaces);

    // IMPILIAMO i pannelli uno sopra l'altro (Tabify)
    tabifyDockWidget(ui->dockEquations, ui->dockRenders);
    tabifyDockWidget(ui->dockRenders, ui->dock3D);
    tabifyDockWidget(ui->dock3D, ui->dock4D);
    tabifyDockWidget(ui->dock4D, ui->dockScripts);
    tabifyDockWidget(ui->dockScripts, ui->dockSurfaces);

    // INVISIBILITÀ INIZIALE
    ui->dockEquations->close();
    ui->dockRenders->close();
    ui->dock3D->close();
    ui->dock4D->close();
    ui->dockScripts->close();
    ui->dockSurfaces->close();
}

// Azioni globali e menu. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupActionsAndMenus()
{
    // =========================================================================
    // 4. GLOBAL ACTIONS & MACOS MENUS
    // =========================================================================
    connect(ui->actionSave, &QAction::triggered, this, [this](){ saveSurfaceToFile(); });
    connect(ui->actionDelete, &QAction::triggered, this, &MainWindow::deleteSelectedExample);
    // Il comando e' GLOBALE: non dipende dalla linguetta aperta. Qui si ricavava
    // un LibraryType dal tab corrente per passarlo a onAddRepositoryClicked, che
    // lo ignorava -- vedi la voce "Change Library Folder..." in
    // librarymenucontroller, dove c'era lo stesso calcolo inutile.
    connect(ui->actionSelectFolder, &QAction::triggered, this, [this](){
        onAddRepositoryClicked();
    });

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    ui->actionSelectFolder->setVisible(false);
#endif

    connect(ui->actionCut, &QAction::triggered, this, [this](){ performCut(nullptr); });
    connect(ui->actionPaste, &QAction::triggered, this, [this](){
        if (!m_cutFilePaths.isEmpty()) onPasteExample();
        else if (!m_cutTexturePaths.isEmpty()) onPasteTexture();
    });

    connect(ui->actionUndoDelete, &QAction::triggered, this, &MainWindow::onUndoDelete);

    // MACOS MENU
    ui->actionQuit->setMenuRole(QAction::QuitRole);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);

    ui->actionAbout->setMenuRole(QAction::AboutRole);
    connect(ui->actionAbout, &QAction::triggered, this, [this](){
        // La versione arriva dal build (APP_VERSION, da project() nel
        // CMakeLists): qui NON va mai scritta a mano, o resta indietro a ogni
        // bump come e' successo fino alla 1.1.
        QMessageBox::about(this, "About Surface Explorer",
                           "<b>Surface Explorer</b><br>"
                           "Version " APP_VERSION "<br><br>"
                           "Developed by: <b>Gaetano Moschetti</b><br>"
                           "License: <b>GNU GPL v3</b><br>"
                           // L'URL e' scritto per esteso anche come testo del link:
                           // se il message box non apre i link esterni resta
                           // comunque leggibile e selezionabile.
                           "Website: <a href=\"https://dioscorid-design.github.io/SurfaceExplorer/\">"
                           "dioscorid-design.github.io/SurfaceExplorer</a><br><br>"
                           "This is free software: you are free to change and redistribute it "
                           "under the terms of the GNU General Public License as published by "
                           "the Free Software Foundation.<br><br>"
                           "<i>Exploring the beauty of four-dimensional geometry.</i>");
    });

    ui->actionDocumentation->setMenuRole(QAction::NoRole);
    connect(ui->actionDocumentation, &QAction::triggered, this, [this](){

        QDialog* docDialog = new QDialog(this);
        docDialog->setWindowTitle("Documentation");

        QVBoxLayout* layout = new QVBoxLayout(docDialog);

        QTextBrowser* browser = new QTextBrowser(docDialog);
        browser->setOpenExternalLinks(true);
        // Il motore rich text di Qt ignora il grosso del CSS del file html:
        // la dimensione del testo va imposta qui, PRIMA di caricare la pagina.
        // div copre i riquadri "note", td/th le tabelle; i titoli restano
        // leggermente più grandi del testo per mantenere la gerarchia.
        browser->document()->setDefaultStyleSheet(
            "p, li, div, td, th { font-size: 17px; }"
            " h2 { font-size: 22px; }"
            " h3 { font-size: 19px; }"
            " h4 { font-size: 18px; }");
        browser->setSource(QUrl("qrc:/docs/documentation.html"));

        // --- RICERCA NELL'INTERO MANUALE ---
        // Campo SEMPRE VISIBILE in cima al dialogo. Il manuale e' spezzato in
        // 17 pagine, quindi il Ctrl+F del browser (che vede solo quella aperta)
        // non basta: qui si cerca in tutte e si presenta l'elenco delle pagine
        // che contengono il termine.
        QLineEdit* searchEdit = new QLineEdit(docDialog);
        searchEdit->setPlaceholderText("Search the manual\xE2\x80\xA6");
        searchEdit->setClearButtonEnabled(true);
#ifdef Q_OS_IOS
        // Stesse guardie degli altri campi su iOS: senza, il long-press apre il
        // menu di modifica e poi blocca la digitazione.
        searchEdit->setInputMethodHints(searchEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        searchEdit->setProperty("noEditMenu", true);
#endif

        // I risultati stanno in un widget SEPARATO che si alterna al browser,
        // invece di essere disegnati dentro la pagina: cosi' la cronologia del
        // QTextBrowser (e quindi il tasto Back) non si sporca di pagine
        // sintetiche che non esistono nel manuale.
        // Pagina da cui e' partita la ricerca: uscendo dai risultati si torna
        // LI', non sulla pagina che un risultato aveva aperto. Senza, il
        // browser restava dov'era e serviva un secondo Back per l'indice --
        // che per l'utente e' un passo indietro di troppo, per giunta verso una
        // pagina appena abbandonata.
        // QString* e non QString: le lambda che lo usano sono copiate per
        // valore e devono condividere lo stesso stato.
        QString* originPage = new QString(QStringLiteral("qrc:/docs/documentation.html"));
        // Pagina aperta DA un risultato: serve a riconoscere se ci si trova
        // ancora li' o se da li' si e' seguito un altro link. Nel secondo caso
        // il passo indietro e' quel link, non l'elenco -- saltarlo perderebbe
        // un passo di navigazione che l'utente ha fatto.
        QString* resultPage = new QString();
        QObject::connect(docDialog, &QObject::destroyed, [originPage, resultPage]() {
            delete originPage; delete resultPage;
        });

        QListWidget* results = new QListWidget(docDialog);
        results->setWordWrap(true);
        // Una riga separa una voce dall'altra: ogni risultato occupa due righe
        // (titolo + contesto) e senza uno stacco visivo il contesto di una voce
        // e il titolo della successiva si leggono come un blocco solo.
        // Il bordo va sull'item, non su voci finte inserite nella lista: quelle
        // sarebbero navigabili con le frecce e romperebbero il conteggio.
        // NB: Qt ignora in silenzio i selettori :first / :last sugli item delle
        // viste (verificato: con tre voci disegna tre bordi), quindi l'ultima
        // riga chiude l'elenco. Cade al fondo dell'ultima voce, non nel vuoto
        // sotto, e fa da chiusura: nessun ritocco necessario.
        results->setStyleSheet(
            "QListWidget { border: none; }"
            " QListWidget::item {"
            "   border-bottom: 1px solid #3a3a3a;"
            "   padding: 10px 6px;"
            " }"
            " QListWidget::item:selected { background-color: #094771; color: white; }");
        results->hide();

        QPushButton* closeBtn = new QPushButton("Close", docDialog);

        // 1. Decidiamo cosa fa il click: chiude la finestra o torna indietro?
        connect(closeBtn, &QPushButton::clicked, docDialog,
                [browser, docDialog, closeBtn, results, searchEdit, originPage, resultPage]() {
            if (closeBtn->text() != "Back") {
                docDialog->accept();     // Chiude la finestra normalmente
                return;
            }
            // Elenco dei risultati a schermo: si torna al manuale svuotando la
            // ricerca. Si agisce SUBITO invece di aspettare che il textChanged
            // faccia scattare il debounce (200 ms): con lo svuotamento affidato
            // al timer il click restava senza effetto visibile per un attimo, e
            // un secondo click nel frattempo cadeva in un ramo sbagliato.
            if (results->isVisible()) {
                searchEdit->clear();
                results->hide();
                browser->show();
                // Si RITORNA alla pagina da cui la ricerca era partita. Senza,
                // il browser restava sulla pagina aperta da un risultato e
                // serviva un secondo Back per uscirne -- verso una pagina che
                // l'utente aveva appena lasciato.
                //
                // Con backward() e non setSource(): il primo CONSUMA la
                // cronologia, il secondo ci aggiunge una voce. Usando setSource
                // il Back seguente ripercorreva le pagine viste durante la
                // ricerca, riportando di nuovo su quella del risultato -- lo
                // stesso giro di troppo, spostato un passo piu' in la'.
                // Il ciclo ha un tetto: una cronologia inattesa non deve poter
                // bloccare il dialogo in un giro infinito.
                for (int guard = 0; guard < 32; ++guard) {
                    if (browser->source().toString() == *originPage) break;
                    if (!browser->isBackwardAvailable()) break;
                    browser->backward();
                }
                const bool isIndex = browser->source().toString()
                                        .endsWith("documentation.html", Qt::CaseInsensitive);
                closeBtn->setText(isIndex ? "Close" : "Back");
                return;
            }
            // SI E' ANCORA sulla pagina aperta dal risultato: il passo indietro
            // naturale e' l'elenco, non la pagina precedente della cronologia --
            // che qui sarebbe quella da cui si era partiti prima di cercare.
            // Il confronto con resultPage e' necessario: senza, bastava che una
            // ricerca fosse attiva e ogni Back saltava all'elenco anche dopo
            // aver seguito altri link da quella pagina, perdendoli.
            if (!searchEdit->text().trimmed().isEmpty() && results->count() > 0
                    && browser->source().toString() == *resultPage) {
                browser->hide();
                results->show();
                return;
            }
            browser->backward();         // Torna alla pagina precedente della cronologia
        });

        // 2. Cambiamo automaticamente il testo del bottone leggendo la pagina corrente.
        // La regola e' posizionale, non un elenco di pagine: l'INDICE
        // (documentation.html) e' l'unica pagina senza un "indietro" possibile,
        // quindi li' il tasto chiude; ovunque altro torna alla pagina precedente.
        // Con l'elenco per nome ogni capitolo aggiunto al manuale nasceva con
        // "Close" e usciva dalla finestra invece di tornare all'indice.
        connect(browser, &QTextBrowser::sourceChanged, docDialog,
                [closeBtn, searchEdit](const QUrl &src) {
            const QString page = src.toString();
            const bool isIndex = page.endsWith("documentation.html", Qt::CaseInsensitive);
            // Con una ricerca in corso l'indice NON e' un vicolo cieco: da li'
            // si torna comunque all'elenco dei risultati, quindi il tasto resta
            // "Back" anche sulla pagina che di norma chiude.
            const bool searching = !searchEdit->text().trimmed().isEmpty();
            closeBtn->setText((isIndex && !searching) ? "Close" : "Back");
        });

        // 3. La ricerca gira mentre si digita, ma con un debounce: la scansione
        // e' su ~128 KB di testo e a ogni carattere sarebbe lavoro sprecato.
        QTimer* searchDebounce = new QTimer(docDialog);
        searchDebounce->setSingleShot(true);
        searchDebounce->setInterval(200);

        auto runSearch = [this, searchEdit, results, browser, closeBtn, originPage]() {
            const QString term = searchEdit->text().trimmed();

            // Si registra la pagina di partenza al PRIMO carattere, cioe' quando
            // i risultati non sono ancora in vista: dopo, il browser mostra gia'
            // una pagina aperta da un risultato e registrarla la scambierebbe
            // per l'origine.
            if (!results->isVisible() && !browser->source().toString().isEmpty())
                *originPage = browser->source().toString();

            // Campo svuotato: si torna al manuale, esattamente dov'era.
            // Il tasto va RIALLINEATO alla pagina in vista: restando su "Back"
            // dopo che i risultati sono spariti finiva nel ramo della cronologia,
            // e sull'indice -- che una cronologia non ce l'ha -- non faceva
            // nulla. Il tasto sembrava morto.
            if (term.isEmpty()) {
                results->hide();
                browser->show();
                const bool isIndex = browser->source().toString()
                                        .endsWith("documentation.html", Qt::CaseInsensitive);
                closeBtn->setText(isIndex ? "Close" : "Back");
                return;
            }

            results->clear();
            const QVector<DocSearch::Hit> hits = DocSearch::search(term);

            if (hits.isEmpty()) {
                QListWidgetItem* none = new QListWidgetItem(
                    QString("No match for \u201C%1\u201D").arg(term), results);
                // Voce informativa, non cliccabile.
                none->setFlags(Qt::NoItemFlags);
            } else {
                for (const DocSearch::Hit &h : hits) {
                    QListWidgetItem* item = new QListWidgetItem(
                        QString("%1  (%2)\n%3").arg(h.title).arg(h.count).arg(h.context),
                        results);
                    item->setData(Qt::UserRole, h.file);
                }
            }

            browser->hide();
            results->show();
            // Con i risultati a schermo il tasto deve riportare al manuale, non
            // chiudere la finestra: la regola posizionale sul solo indice non
            // copre questo stato, che pagina del manuale non e'.
            closeBtn->setText("Back");
        };

        connect(searchDebounce, &QTimer::timeout, docDialog, runSearch);
        connect(searchEdit, &QLineEdit::textChanged, docDialog,
                [searchDebounce](const QString&) { searchDebounce->start(); });
        // Invio: cerca subito, senza aspettare il debounce.
        connect(searchEdit, &QLineEdit::returnPressed, docDialog, [searchDebounce, runSearch]() {
            searchDebounce->stop();
            runSearch();
        });

        // 4. Click su un risultato: apre la pagina ed EVIDENZIA il termine.
        // L'evidenziazione passa da ExtraSelection e non da tag iniettati nel
        // documento: il motore rich text di Qt ignora gran parte del CSS (per
        // questo il font e' forzato con setDefaultStyleSheet qui sopra) e
        // manipolare l'HTML sporcherebbe la pagina caricata dal .qrc.
        connect(results, &QListWidget::itemClicked, docDialog,
                [browser, results, searchEdit, resultPage](QListWidgetItem* item) {
            if (!item) return;
            const QString file = item->data(Qt::UserRole).toString();
            if (file.isEmpty()) return;   // riga "nessun risultato"

            results->hide();
            browser->show();
            browser->setSource(QUrl(file));
            *resultPage = file;   // da qui il Back torna all'elenco

            const QString term = searchEdit->text().trimmed();
            if (term.isEmpty()) return;

            QList<QTextEdit::ExtraSelection> sels;
            QTextCursor cur(browser->document());
            QTextCharFormat fmt;
            fmt.setBackground(QColor("#7a5c00"));   // ocra: leggibile sul tema scuro
            fmt.setForeground(QColor("#ffffff"));
            while (true) {
                cur = browser->document()->find(term, cur, QTextDocument::FindCaseSensitively
                                                            & QTextDocument::FindFlags());
                if (cur.isNull()) break;
                QTextEdit::ExtraSelection sel;
                sel.cursor = cur;
                sel.format = fmt;
                sels.append(sel);
            }
            browser->setExtraSelections(sels);

            // Porta la vista sulla prima occorrenza, o la pagina si aprirebbe
            // in cima lasciando all'utente il compito di cercarla a occhio.
            if (!sels.isEmpty()) {
                browser->setTextCursor(sels.first().cursor);
                browser->ensureCursorVisible();
            }
        });

        layout->addWidget(searchEdit);
        layout->addWidget(browser);
        layout->addWidget(results);
        layout->addWidget(closeBtn);

        // Deleghiamo tutta l'estetica a UiStyleManager!
        UiStyleManager::setupDocumentationDialog(docDialog, layout, browser, closeBtn, searchEdit);

        // 1. Trova il bottone PRIMA di aprire il dialog e salva la sua posizione reale
        QPoint originalPos;
        QPushButton* menuBtn = this->findChild<QPushButton*>("mobileMenuBtn");
        if (menuBtn) {
            originalPos = menuBtn->pos();
        }

        // 2. Apri la documentazione (il codice si ferma qui finché non chiudi la finestra)
        docDialog->exec();

        // 3. Ripristina il bottone
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Passiamo 'originalPos' dentro la lambda per ricordarci dove stava
        QTimer::singleShot(50, this, [this, originalPos]() {
            QPushButton* btn = this->findChild<QPushButton*>("mobileMenuBtn");
            if (btn) {
                btn->raise();
                // Proviamo a rimetterlo dove era "nativamente"
                if (!originalPos.isNull()) {
                    btn->move(originalPos);
                } else {
                    // Fallback unificato: ora che la barra è nascosta, 10,10 è perfetto per tutti
                    btn->move(10, 10);
                }
                btn->show();
            }
        });
#endif
    });
}

// Barra di stato e comandi in basso (master START/STOP). Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupStatusBar()
{
    // =========================================================================
    // 5. STATUS BAR & BOTTOM CONTROLS
    // =========================================================================
    m_btnStart = new QPushButton("START", this);
    m_btnStart->setFlat(true);
    QFont fontBold = m_btnStart->font();
    fontBold.setBold(true);
    m_btnStart->setFont(fontBold);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // NEW sta nella status bar, fra START e RESET: e' la sua posizione naturale,
    // accanto agli altri comandi che agiscono sulla scena intera, e non
    // appartiene a nessuna delle due modalita' - come la scena vuota che
    // produce. Ci era gia' stato in passato, ma con undici tasti in fila gli
    // ultimi (Library per ultimo) finivano FUORI SCHERMO sui telefoni, e da li'
    // era passato prima in fondo al dock Equations, poi sopra le linguette.
    // Ora la barra scorre col dito su mobile (StatusBarScroller, in fondo a
    // questa sezione), quindi il vincolo che lo aveva esiliato non c'e' piu'.
    m_btnNew = new QPushButton("NEW", this);
    m_btnNew->setFlat(true);
    m_btnNew->setFont(fontBold);
    connect(m_btnNew, &QPushButton::clicked, this, &MainWindow::onNewSceneClicked);

    m_btnResetView = new QPushButton("RESET", this);
    m_btnResetView->setFlat(true);
    m_btnResetView->setFont(fontBold);
    connect(m_btnResetView, &QPushButton::clicked, this, &MainWindow::onResetClicked);

    m_btnProjection = new QPushButton("Perspective", this);
    m_btnProjection->setFlat(true);
    m_btnProjection->setFont(fontBold);
    connect(m_btnProjection, &QPushButton::clicked, this, &MainWindow::toggleProjection);

    m_btnRec = new QPushButton("REC", this);
    m_btnRec->setFlat(true);
    m_btnRec->setFont(fontBold);
    UiStyleManager::applyRecordButtonStyle(m_btnRec);
    m_videoRecorder = new VideoRecorder(this, this);
    connect(m_btnRec, &QPushButton::clicked, m_videoRecorder, &VideoRecorder::toggleRecord);

    m_statusLabel = new QLabel("", this);
    m_renderProgress = new QProgressBar(this);
    m_renderProgress->setRange(0, 100);
    m_renderProgress->setValue(0);
    m_renderProgress->setTextVisible(true);
    m_renderProgress->setVisible(false);
    m_renderProgress->setFixedWidth(150);

    ui->statusbar->addWidget(m_btnStart);
    ui->statusbar->addWidget(m_btnNew);
    ui->statusbar->addWidget(m_btnResetView);
    ui->statusbar->addWidget(m_btnProjection);
    ui->statusbar->addWidget(m_btnRec);
    ui->statusbar->addWidget(m_renderProgress);
    // m_statusLabel SENZA fattore di stretch: con stretch 1 rivendicava tutto
    // lo spazio residuo della barra, e i tasti dock (permanent widget, che il
    // layout non comprime) venivano spinti fuori schermo sui telefoni. Il
    // label non mostra mai testo: e' solo il bersaglio di clear() e
    // setStyleSheet(), quindi non gli serve larghezza propria.
    ui->statusbar->addWidget(m_statusLabel);

    // Lambda helper apertura dock
    auto closeAllDocks = [this](){
        ui->dockEquations->close();
        ui->dockRenders->close();
        ui->dock3D->close();
        ui->dock4D->close();
        ui->dockScripts->close();
        ui->dockSurfaces->close();
    };

    auto safeOpenDock = [this, closeAllDocks](QDockWidget* dock) {

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        static qint64 lastToggleTime = 0;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastToggleTime < 400) return;
        lastToggleTime = currentTime;

        if (dock->isVisible()) {
            dock->close();
            return;
        }

        if (dock->isFloating()) dock->setFloating(false);

        // 1. CONGELIAMO LO SCHERMO (Elimina il flash visivo)
        this->setUpdatesEnabled(false);

        // 2. FORZIAMO LA LARGHEZZA (Evita il collasso a 0 pixel e il "millimetro" di splitter)
        dock->setFixedWidth(400);

        // 3. AGGANCIAMO E MOSTRIAMO
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
        dock->raise();

        // 4. CHIUDIAMO I VECCHI IN BACKGROUND
        if (dock != ui->dockEquations) ui->dockEquations->close();
        if (dock != ui->dockRenders) ui->dockRenders->close();
        if (dock != ui->dock3D) ui->dock3D->close();
        if (dock != ui->dock4D) ui->dock4D->close();
        if (dock != ui->dockScripts) ui->dockScripts->close();
        if (dock != ui->dockSurfaces) ui->dockSurfaces->close();

        // 5. SCONGELIAMO LO SCHERMO
        this->setUpdatesEnabled(true);

        QTimer::singleShot(100, dock, [this, dock]() {
            dock->setMinimumWidth(400);

            // dockEquations e dockSurfaces (Library) bloccati a 400: non serve
            // che si allarghino oltre. Cosi' i tab Library hanno larghezza nota.
            if (dock == ui->dockEquations || dock == ui->dockSurfaces) {
                dock->setMaximumWidth(400);
            } else {
                dock->setMaximumWidth(16777215);
            }

            // 6. SBLOCCHIAMO LA LARGHEZZA DOPO L'APERTURA
            QTimer::singleShot(100, dock, [this, dock]() {
                dock->setMinimumWidth(400);

                if (dock == ui->dockEquations || dock == ui->dockSurfaces) {
                    // Blocca l'espansione massima per Equations e Library
                    dock->setMaximumWidth(400);
                } else {
                    // Lascia gli altri liberi
                    dock->setMaximumWidth(16777215);
                }
            });
        });

#else
        // =========================================================
        // RAMO DESKTOP
        // =========================================================
        if (dock->isVisible()) {
            dock->close();
            return;
        }

        closeAllDocks();

        if (dock->isFloating()) dock->setFloating(false);
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
#endif
    };

    // Tasti Docks nella Statusbar
    QPushButton* btnEq = new QPushButton("Equations", this); btnEq->setFlat(true);
    connect(btnEq, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockEquations); });
    ui->statusbar->addPermanentWidget(btnEq);

    QPushButton* btnRen = new QPushButton("Renderer", this); btnRen->setFlat(true);
    connect(btnRen, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockRenders); });
    ui->statusbar->addPermanentWidget(btnRen);

    QPushButton* btn3D = new QPushButton("3D View", this); btn3D->setFlat(true);
    connect(btn3D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock3D); });
    ui->statusbar->addPermanentWidget(btn3D);

    QPushButton* btn4D = new QPushButton("4D View", this); btn4D->setObjectName("btnDock4D"); btn4D->setFlat(true);
    connect(btn4D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock4D); });
    ui->statusbar->addPermanentWidget(btn4D);

    QPushButton* btnScript = new QPushButton("Script", this); btnScript->setFlat(true);
    connect(btnScript, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockScripts); });
    ui->statusbar->addPermanentWidget(btnScript);

    QPushButton* btnEx = new QPushButton("Library", this); btnEx->setFlat(true);
    connect(btnEx, &QPushButton::clicked, this, [this, safeOpenDock](){
        QSettings settings;
        QString root = settings.value("libraryRootPath").toString();
        if (root.isEmpty() || !SecurityBookmark::isAccessible(root)) setupDefaultFolders();
        safeOpenDock(ui->dockSurfaces);
    });
    ui->statusbar->addPermanentWidget(btnEx);

    // MOBILE: da qui in poi la barra scorre col dito. Va chiamata DOPO l'ultimo
    // addPermanentWidget - riparenta i widget gia' presenti, quindi qualsiasi
    // tasto aggiunto piu' avanti finirebbe fuori dal nastro. btnEq segna dove
    // iniziano i tasti dock: li' va lo stacco che prima nasceva dalla differenza
    // fra addWidget e addPermanentWidget.
    // m_renderProgress e m_statusLabel sono passati come INDICATORI: entrano nel
    // nastro subito dopo REC, il tasto a cui appartengono. Tenuti fuori dal
    // nastro finivano in coda alla barra - visivamente all'estrema destra, dopo
    // Library - perche' il nastro ha stretch 1 e si prende tutto lo spazio
    // residuo. No-op su desktop.
    StatusBarScroller::install(ui->statusbar, btnEq,
                               { m_renderProgress, m_statusLabel });
}

// Ultimi allineamenti dell'avvio. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::finishStartup()
{
    // =========================================================================
    // 11. FINAL STARTUP CALLS
    // =========================================================================
    ui->chkBoxTexture->setChecked(false);
    applySurfaceTextureToEngine();   // intenzione iniziale: spenta
    updateTextureUIState(false);

    connectSidePanels();
    switchToMainMode();

    // ACCESSO ALLA LIBRERIA, UNA VOLTA PER TUTTA LA SESSIONE.
    // Non basta riaprirlo dentro refreshRepositories: la libreria contiene anche
    // FILE ESTERNI referenziati per percorso assoluto dai preset — i .wav/.mp3
    // dei suoni (//MUSIC:) e le immagini delle texture (//IMG:). Chi li carica
    // decide con QFile::exists() / QDirIterator, che sotto sandbox falliscono
    // esattamente come gli alberi vuoti: il record si apriva EVIDENZIANDO il
    // suono o l'immagine in libreria, ma senza attivarli (audio muto, texture
    // sostituita da quella di default). Aprendo lo scope qui, prima di ogni
    // caricamento, tutti quei punti trovano i file senza doverlo sapere.
    // No-op fuori dalla sandbox.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // RADICE DI SISTEMA SALVATA PER ERRORE: si BUTTA, non si corregge.
    //
    // Una versione precedente del dialogo di recupero salvava la cartella
    // scelta GREZZA, senza scendere in "presets": chi ha indicato ~/Documents
    // si e' ritrovato surfaces/textures/records/sounds sparsi direttamente li'.
    // Quel valore resta in QSettings e va tolto, altrimenti i quattro rami
    // vengono ricreati a ogni avvio.
    //
    // NON si prova a "riparare" la radice inventando <cartella>/presets: sotto
    // sandbox una cartella CREATA DAL CODICE non e' raggiungibile ai riavvii.
    // Un security-scoped bookmark autorizza solo cio' che l'UTENTE ha indicato
    // in un pannello di sistema (Powerbox); su una cartella mai scelta il
    // bookmark si salva ma non concede nulla, e all'avvio dopo compariva
    // "Your library folder can no longer be opened" -- un messaggio da
    // diagnostica, incomprensibile a chi installa l'app per la prima volta.
    // Peggio: ~/Documents sotto sandbox NON e' quella reale, e' la Documents
    // PRIVATA e vuota del container (Desktop e Downloads sono symlink,
    // Documents no), quindi quella radice non avrebbe mai mostrato nulla.
    //
    // Azzerando la chiave si torna esattamente al comportamento del DMG e del
    // primo avvio: nessun popup d'errore, e alla prima apertura del dock
    // Library parte setupDefaultFolders, che CHIEDE dove installare i preset
    // con un pannello di sistema -- l'unico gesto che sotto sandbox concede
    // davvero l'accesso, e che rende la cartella scelta valida per sempre.
    {
        QSettings settings;
        const QString cleanRoot = QDir::cleanPath(settings.value("libraryRootPath").toString());

        // Home REALE dell'utente, non quella del container: si ricava dal
        // percorso stesso (/Users/<nome>), perche' QStandardPaths sotto sandbox
        // risponde con i percorsi redirezionati e non darebbe mai riscontro.
        QStringList systemDirs;
        const QRegularExpression userRe(QStringLiteral("^(/Users/[^/]+)"));
        const auto m = userRe.match(cleanRoot);
        if (m.hasMatch()) {
            const QString home = m.captured(1);
            systemDirs << home
                       << home + "/Documents"
                       << home + "/Desktop"
                       << home + "/Downloads";
        }

        if (!cleanRoot.isEmpty() && systemDirs.contains(cleanRoot)) {
            qWarning() << "Libreria: radice di sistema" << cleanRoot
                       << "-- scartata, si tornera' a chiedere la cartella.";
            settings.remove("libraryRootPath");
            // Puntavano ai rami sparsi: se restassero, scavalcherebbero la
            // scelta successiva (sono lette con default rootPath+"/ramo").
            settings.remove("pathSurfaces");
            settings.remove("pathTextures");
            settings.remove("pathRecords");
            settings.remove("pathSounds");
        }
    }

    SecurityBookmark::restore(QSettings().value("libraryRootPath").toString());
#endif

    // LIBRERIA NON RAGGIUNGIBILE: dirlo, invece di mostrare quattro alberi vuoti.
    // E' il caso in cui la cartella e' stata scelta in passato (esiste un
    // bookmark) ma l'autorizzazione non si risolve piu'. Prima il fallimento era
    // MUTO: i gate QDir::exists() saltavano il caricamento in silenzio e il
    // sintomo ("libreria vuota") non suggeriva in alcun modo la causa. I file
    // NON sono persi: manca solo il permesso di raggiungerli.
    // needsAuthorization e' sempre false fuori dalla sandbox: su DMG, Windows e
    // Linux questo blocco non si attiva mai.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    {
        const QString libRoot = QSettings().value("libraryRootPath").toString();

        // SOLO il caso "autorizzazione persa" (sandbox / App Store): la cartella
        // c'e' e i preset sono al loro posto, manca il permesso di raggiungerli.
        // Senza questo dialogo la libreria resterebbe vuota senza spiegazione, e
        // nessun altro punto lo direbbe.
        //
        // NON si avvisa qui della cartella SPARITA (radice configurata, cartella
        // inesistente): sarebbe un popup di troppo, perche' subito dopo arriva
        // comunque quello di setupDefaultFolders che chiede dove installare la
        // libreria -- due finestre in fila per la stessa decisione. Il secondo
        // basta da solo.
        if (SecurityBookmark::needsAuthorization(libRoot)) {
            // Testo breve in setText, resto in informativeText: il testo
            // principale di QMessageBox e' un TITOLO e non va a capo, quindi il
            // box si allarga fino a contenere su UNA riga la piu' lunga di
            // quelle logiche. Attenzione: le righe che qui sotto sembrano corte
            // sono letterali C++ ADIACENTI, che il compilatore concatena in
            // un'unica riga senza \n -- misurata 823 pt, da cui un box di 983 pt.
            // L'informativeText invece manda a capo da solo.
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle("Library Not Accessible");
            box.setText("Your library folder can no longer be opened.");
            // Il testo diceva "Your presets are still there — only the permission
            // was lost": FALSO nel caso piu' comune che porta qui, la cartella
            // spostata o cancellata. needsAuthorization non distingue le due cose
            // (bookmark salvato + cartella irraggiungibile), quindi il messaggio
            // deve coprirle entrambe e dire cosa succede se la libreria non c'e'
            // piu'.
            box.setInformativeText(
                libRoot + "\n\n"
                "It may have been moved or deleted, or the permission to open it "
                "may have been lost after a system update.\n\n"
                "Do you want to select your library folder? If it no longer exists, "
                "select where to create a new one: the factory presets will be "
                "installed in a \"presets\" folder inside it.");
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box.setDefaultButton(QMessageBox::Yes);
            const auto answer = box.exec();

            if (answer == QMessageBox::Yes) {
                const QString picked = QFileDialog::getExistingDirectory(this,
                    "Select Your Library Folder", QDir::homePath());
                if (!picked.isEmpty()) {
                    const QString pickedClean = QDir::cleanPath(picked);

                    // DUE CASI, decisi da cio' che la cartella indicata CONTIENE.
                    // Dopo il pannello di sistema la cartella e' autorizzata, quindi
                    // qui il contenuto si legge davvero: non c'e' il rischio di
                    // scambiare una libreria irraggiungibile per una cartella vuota.
                    //
                    // RECUPERO: la cartella e' la libreria, o ne contiene una in
                    // "presets" (casi 1 e 2 di LibraryFolders::resolveRoot) -> si riprende
                    // quella, senza installare niente.
                    //
                    // NUOVA INSTALLAZIONE: nessuna libreria in vista -- la vecchia e'
                    // stata spostata o cancellata. Prima si prendeva la cartella
                    // COM'ERA come radice: libreria vuota, e Restore Factory Presets
                    // (che la trovava valida) rovesciava i quattro rami direttamente
                    // li' dentro. MISURATO il 27/9: cancellata ~/Projects/presets e
                    // indicata ~/Projects, rami sparsi in ~/Projects. Ora si fa come
                    // alla prima installazione: "<scelta>/presets" e preset di
                    // fabbrica. Eccezione: se la cartella indicata si chiama gia'
                    // "presets" si usa lei, per non creare presets/presets.
                    QString clean = LibraryFolders::resolveRoot(picked);
                    const bool newInstall = !LibraryFolders::isLibraryRoot(QDir(clean));
                    if (newInstall
                        && QFileInfo(pickedClean).fileName().compare(
                               QLatin1String("presets"), Qt::CaseInsensitive) == 0) {
                        clean = pickedClean;
                    }

                    QDir().mkpath(clean);
                    {   // sync() esplicito: vedi setupDefaultFolders (cfprefsd)
                        QSettings s;
                        s.setValue("libraryRootPath", clean);
                        s.sync();
                    }
                    // Come in setupDefaultFolders: bookmark ANCHE della cartella
                    // indicata nel pannello, da cui nasce il diritto sotto sandbox.
                    if (clean != pickedClean) SecurityBookmark::save(pickedClean);
                    SecurityBookmark::save(clean);

                    // La radice ora e' raggiungibile, quindi setupDefaultFolders
                    // salta la domanda e fa solo il resto: crea i quattro rami,
                    // installa i preset di fabbrica e aggiorna gli alberi.
                    if (newInstall) setupDefaultFolders();
                }
            }
        }
    }
#endif

    refreshRepositories();
    updateWatcherPaths();

#if defined(Q_OS_IOS)
    // SU IOS: showFullScreen() è obbligatorio per sbloccare la risoluzione nativa
    this->showFullScreen();
#elif defined(Q_OS_ANDROID)
    // SU ANDROID: Ripristiniamo showMaximized().
    // Evita l'Immersive Mode che spinge la UI a destra contro il Notch.
    this->showMaximized();
#else
    this->resize(1280, 720);
    this->showMaximized();
#endif

    QPushButton* mobileMenuBtn = nullptr;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    ui->menuBar->hide();

    // Creiamo il bottone, con parent 'this' per evitare problemi di touch col 3D
    mobileMenuBtn = new QPushButton(this);
    mobileMenuBtn->setObjectName("mobileMenuBtn");

    // Applichiamo tutto il disegno e lo stile tramite il manager!
    UiStyleManager::styleMobileMenuButton(mobileMenuBtn);

    mobileMenuBtn->move(10, 10);

    QMenu* overflowMenu = new QMenu(this);
    overflowMenu->addAction(ui->actionDocumentation);
    overflowMenu->addAction(ui->actionAbout);
    overflowMenu->addSeparator();
    overflowMenu->addAction(ui->actionQuit);

    // Applichiamo il foglio di stile CSS del menu
    UiStyleManager::styleMobileOverflowMenu(overflowMenu);

    // Connessione al tocco
    connect(mobileMenuBtn, &QPushButton::released, this, [mobileMenuBtn, overflowMenu]() {
        QPoint pos = mobileMenuBtn->mapToGlobal(QPoint(0, mobileMenuBtn->height()));
        overflowMenu->exec(pos);
    });

    mobileMenuBtn->raise();
    mobileMenuBtn->show();
#endif

#if defined (Q_OS_IOS)
    // Manteniamo il tuo HACK DI PRE-RISCALDAMENTO per l'iPad
    this->setUpdatesEnabled(false);

    ui->dockEquations->show(); ui->dockEquations->hide();
    ui->dockRenders->show();   ui->dockRenders->hide();
    ui->dock3D->show();        ui->dock3D->hide();
    ui->dock4D->show();        ui->dock4D->hide();
    ui->dockScripts->show();   ui->dockScripts->hide();
    ui->dockSurfaces->show();  ui->dockSurfaces->hide();

    ui->dock3D->blockSignals(false);
    ui->dock4D->blockSignals(false);

    this->setUpdatesEnabled(true);
#endif
}

// Ridimensionamento e filtri d'input desktop. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupDesktopFilters()
{
    // =========================================================================
    // 12. RESCALING & DESKTOP FILTERS
    // =========================================================================

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Applica il blocco dell'invio su Desktop per TUTTI i campi di testo
    DesktopInputFilter* desktopFilter = new DesktopInputFilter(this);

    // Per i campi multi-riga delle equazioni (QPlainTextEdit)
    for (auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        textEdit->installEventFilter(desktopFilter);
    }

    // Per i campi a riga singola di costanti, limiti e percorsi (QLineEdit)
    for (auto* lineEdit : this->findChildren<QLineEdit*>()) {
        lineEdit->installEventFilter(desktopFilter);
    }
#endif

    EnterApplyFilter* equationEnterFilter = new EnterApplyFilter(this);
    // L'origine marca QUESTA via d'ingresso (Invio su un campo equazione), che
    // senza non si distinguerebbe dalle chiamate programmatiche: entrambe
    // arrivano senza tasto. Serve a riarmare il clock della geometria come fa
    // il Run.
    equationEnterFilter->onEnter = [this]() { runScene(RunOrigin::EnterKey); };
    ui->lineEquation->installEventFilter(equationEnterFilter);
    ui->lineEquationCrossSection->installEventFilter(equationEnterFilter);

    // Run del dock Equations: applica le equazioni parametriche senza passare
    // dal tasto master START/STOP della status bar
    connect(ui->btnRunParametric, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // Run del dock Equations (modalità implicita / Ray Marching): stesso effetto
    // dell'invio nel campo equazione, agisce SOLO sul modulo equazioni.
    connect(ui->btnImplicit, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // All'avvio entrambe le superfici di default (toro parametrico e sfera
    // implicita) sono già renderizzate: i Run "one-shot" devono nascere
    // DISABILITATI. Riasseriamo i flag a true perché impostare le equazioni di
    // default in costruzione fa scattare i loro textChanged (es. lineEquation a
    // segnali NON bloccati) che li avrebbero rimessi a false. Poi forziamo
    // l'allineamento dei tasti: senza questa chiamata updateMasterButtonState non
    // gira mai in costruzione (m_btnStart non esisteva ancora ai primi trigger) e
    // i tasti resterebbero sull'enabled di default del .ui.
    m_parametricApplied = true;
    m_implicitApplied = true;
    m_rmTextureApplied = true;
    // All'avvio a schermo c'e' la superficie di default (toro/sfera): i campi
    // sono pieni ma non sono lavoro dell'utente, come dopo un reset.
    m_surfaceOrigin = OriginDefault;
    // Tracciamento della posa mossa dall'utente (mouse, touch, tasti di camera).
    // PRIMA di m_uiReady: i segnali emessi durante il setup trovano la guardia
    // di noteViewControlUsed ancora chiusa.
    wireViewControlsTracking();
    m_uiReady = true;   // sblocca updateMasterButtonState: la UI è completa
    updateMasterButtonState();
    // La scena di avvio (superficie di default) e' il primo momento pulito.
    markSceneClean();
    captureStartPose();
}

void MainWindow::connectSidePanels()
{
    // --- Navigazione Dock ---
    connect(ui->dock3D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo3DMode();
    });
    connect(ui->dock4D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo4DMode();
    });

    // --- CONTROLLI ROTAZIONE

    // Precessione
    setupSpeedControl(ui->btnPrecessionPlus, ui->btnPrecessionMinus,
                      [this]{ return ui->glWidget->getPrecessionSpeed(); },
                      [this](float v){ ui->glWidget->setPrecessionSpeed(v); });

    // Nutazione
    setupSpeedControl(ui->btnNutationPlus, ui->btnNutationMinus,
                      [this]{ return ui->glWidget->getNutationSpeed(); },
                      [this](float v){ ui->glWidget->setNutationSpeed(v); });

    // Spin
    setupSpeedControl(ui->btnSpinPlus, ui->btnSpinMinus,
                      [this]{ return ui->glWidget->getSpinSpeed(); },
                      [this](float v){ ui->glWidget->setSpinSpeed(v); });

    // Omega (4D)
    setupSpeedControl(ui->btnOmegaPlus, ui->btnOmegaMinus,
                      [this]{ return ui->glWidget->getOmegaSpeed(); },
                      [this](float v){
                          ui->glWidget->setOmegaSpeed(v);
                          update4DButtonState();
                      });

    // Phi (4D)
    setupSpeedControl(ui->btnPhiPlus, ui->btnPhiMinus,
                      [this]{ return ui->glWidget->getPhiSpeed(); },
                      [this](float v){
                          ui->glWidget->setPhiSpeed(v);
                          update4DButtonState();
                      });

    // Psi (4D)
    setupSpeedControl(ui->btnPsiPlus, ui->btnPsiMinus,
                      [this]{ return ui->glWidget->getPsiSpeed(); },
                      [this](float v){
                          ui->glWidget->setPsiSpeed(v);
                          update4DButtonState();
                      });

    // Tasto Stop/Go laterale
    connect(ui->btnStart_2, &QPushButton::clicked, this, &MainWindow::onStopClicked);
}

void MainWindow::connectNavButton(QPushButton *btn, int action)
{
    if (!btn) return;

    // Registriamo il pulsante per poterli disabilitare in blocco durante un path
    // (i tasti di spostamento a click dei dock 3D/4D non hanno senso mentre la
    // telecamera segue un percorso).
    m_navButtons.append(btn);

    // PRESSIONE: uno scatto subito, della sola azione di questo tasto. La
    // ripetizione continua parte solo se il tasto resta premuto oltre
    // m_navRepeatDelay. Prima navTimer partiva alla pressione e scattava ogni
    // 30 ms: un clic normale (100-150 ms) dava da 1 a 5 scatti a caso, e
    // contando i clic non si tornava al punto di partenza (segnalato
    // dall'utente su T3 Villarceau: Psi non tornava all'angolo esatto, con
    // il readout a due decimali che segnava comunque +0.00).
    connect(btn, &QPushButton::pressed, this, [this, action]() {
        // Primo tasto premuto: parte l'attesa della ripetizione.
        if (activeNavActions.isEmpty()) {
            m_navRepeatDelay->start();
        }
        activeNavActions.insert(action);
        stepNavActions({action});
    });

    // RILASCIO: nessun tasto premuto, si ferma tutto (attesa e ripetizione).
    connect(btn, &QPushButton::released, this, [this, action]() {
        activeNavActions.remove(action);
        if (activeNavActions.isEmpty()) {
            m_navRepeatDelay->stop();
            navTimer->stop();
        }
    });
}

void MainWindow::updateLayoutForMode(int mode)
{
    bool old3D = ui->dock3D->blockSignals(true);
    bool old4D = ui->dock4D->blockSignals(true);

    if (mode == 1) { // 3D Mode
        if (!ui->dock3D->isVisible()) ui->dock3D->show();
        if (ui->dock4D->isVisible())  ui->dock4D->close();
        ui->dock3D->raise(); // Porta in primo piano
    }
    else if (mode == 2) { // 4D Mode
        if (ui->dock3D->isVisible())  ui->dock3D->close();
        if (!ui->dock4D->isVisible()) ui->dock4D->show();
        ui->dock4D->raise(); // Porta in primo piano
    }

    ui->dock3D->blockSignals(old3D);
    ui->dock4D->blockSignals(old4D);
}


// ==========================================
// PROTECTED
// ==========================================


// Chiusura dell'applicazione. Era l'unico percorso distruttivo rimasto senza
// avviso: da qui la scena, la texture e il suono modificati sparivano senza
// domande, mentre load di un preset, NEW, reset e cambio di modalita' chiedono
// tutti conferma.
//
// Passa dalla stessa confirmDiscardUnsaved(ScopeScene) degli altri percorsi:
// il popup elenca cio' che e' sporco e, su "Save", apre il salvataggio dalla
// radice dell'albero. Nessun flag va riletto qui -- la decisione sta in
// hasUnsavedWork(): duplicarla farebbe divergere questo percorso dagli altri.
//
// Vale anche quando l'unica cosa fatta e' applicare una texture da libreria: la
// scena che la usa si perde comunque, e il salvataggio proposto -- dalla
// radice, ramo records/ -- la conserva per intero. Il ramo di destinazione non
// c'entra: e' il RECORD a rendere salvabile quel lavoro, non la superficie.
//
// Cancel (o la chiusura del popup) -> event->ignore(): la finestra resta
// aperta. Vale anche per il quit dal menu/Cmd-Q, che passa comunque da qui.
void MainWindow::closeEvent(QCloseEvent* event)
{
    // Registrazione in corso: chiudere adesso lascerebbe il file video
    // troncato e l'encoder a meta'. Si chiede prima di fermare REC.
    if (m_isRecording) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Recording in progress");
        box.setText("A video is being recorded.");
        box.setInformativeText("Stop the recording before quitting.");
        box.addButton("OK", QMessageBox::AcceptRole);
        box.exec();
        event->ignore();
        return;
    }

    if (!confirmDiscardUnsaved(ScopeScene)) {
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMinimized) {
            if (m_audioController && m_audioController->isPlaying()) {
                m_audioController->stopAll();
            }
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::hideEvent(QHideEvent* event)
{
    // Ferma l'audio
    if (m_audioController && m_audioController->isPlaying()) {
        m_audioController->stopAll();
    }

    // Ferma il timer del flusso geodetico e memorizza che era attivo,
    // così possiamo ripristinarlo al ritorno in foreground.
    // Necessario su iOS: al ritorno dal background il QRhi può essere
    // valido ma le risorse Metal non ancora ripristinate -> grid vuota
    // -> falso popup di singolarita'.
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
        m_geoAnimTimerWasRunning = true;
    }

    QMainWindow::hideEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // Riavvia il timer geodetico solo se era stato fermato da hideEvent.
    // Il delay (300 ms) serve a dare al backend grafico il tempo di
    // ricreare le risorse GPU su iOS/Android prima del primo tick.
    if (m_geoAnimTimerWasRunning) {
        m_geoAnimTimerWasRunning = false;

        QTimer::singleShot(300, this, [this]() {
            // Ricontrolla che l'utente non abbia fermato il flusso nel frattempo:
            // col master (m_masterStopped) o col tasto del dock Equations.
            // (Era il testo del master su "STOP", che ora vuol dire "tutti i
            // moduli accesi", non "qualcosa in moto".)
            if (m_masterStopped || m_userStoppedGeomClock) return;
            if (!isVisible()) return;
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            if (m_geoAnimTimer && !m_geoAnimTimer->isActive()) {
                m_geoAnimTimer->start();
            }
        });
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
