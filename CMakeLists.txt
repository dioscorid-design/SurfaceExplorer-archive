cmake_minimum_required(VERSION 3.16)
# La versione marketing sta QUI e in nessun altro punto del C++: da project()
# viene propagata al bundle (sotto) e al codice come define APP_VERSION, che il
# dialogo About stampa. Prima il numero era scritto a mano anche in
# mainwindow.cpp e restava indietro a ogni bump (bump-version.sh non poteva
# raggiungerlo e si limitava a stampare un promemoria).
# Info.plist resta un file a parte, aggiornato da ExC/Mac/bump-version.sh.
project(SurfaceExplorer VERSION 1.4 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

# 🔹 Trova i moduli necessari di Qt
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    GuiPrivate
    Widgets
    Concurrent
    Network
    Multimedia
    ShaderTools
    ShaderToolsPrivate
)
# 🔹 Includi la cartella sorgente locale
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

qt_standard_project_setup()

# GUARDIA ANTI-REGRESSIONE: script_guide.html è già sparito più volte da
# resources.qrc (rimozioni accidentali durante l'editing delle risorse), e il
# link "GLSL Script Writing Guide" nella documentazione muore in SILENZIO.
# Meglio un errore di build chiaro. CONFIGURE_DEPENDS fa rieseguire la
# configure (e quindi il controllo) a ogni modifica del .qrc.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS resources.qrc)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc" _qrc_content)
if(NOT _qrc_content MATCHES "script_guide\\.html")
    message(FATAL_ERROR
        "resources.qrc non contiene <file>docs/script_guide.html</file>: il link "
        "'GLSL Script Writing Guide' nella documentazione sarebbe morto. "
        "Rigenera il .qrc con update_resources.py (STATIC_FILES).")
endif()

# Stessa guardia per i capitoli del manuale: documentation.html è ormai solo
# l'INDICE, e ogni voce punta a un doc_*.html. Se uno di questi non finisce nel
# .qrc la sua voce di indice apre una pagina vuota, senza alcun errore visibile.
# La lista sta in update_resources.py (DOC_CHAPTERS): tenerle allineate.
set(_doc_chapters
    intro quickstart interface interaction projection dimensions equations
    geodesic raymarching rendering audio animation scripting
    library-management modular-controls acknowledgments)
foreach(_chap IN LISTS _doc_chapters)
    if(NOT _qrc_content MATCHES "doc_${_chap}\\.html")
        message(FATAL_ERROR
            "resources.qrc non contiene <file>doc_${_chap}.html</file>: la voce "
            "corrispondente nell'indice della documentazione aprirebbe una pagina "
            "vuota. Rigenera il .qrc con update_resources.py.")
    endif()
endforeach()
unset(_doc_chapters)
unset(_qrc_content)

qt_add_executable(SurfaceExplorer
    resources.qrc

    # --- MAIN & UI ---
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    # MainWindow divisa per argomento: stessa classe, un file per area
    # (intestazione privata comune mainwindow_p.h).
    mainwindow_p.h
    scenestate.h
    mainwindow_scene.cpp
    mainwindow_state.cpp
    mainwindow_equations.cpp
    mainwindow_run.cpp
    mainwindow_motion.cpp
    mainwindow_texture.cpp
    mainwindow_render.cpp
    mainwindow_script.cpp
    mainwindow_library.cpp
    mainwindow_hint.cpp
    # Classi estratte da MainWindow (stato e interfaccia propri).
    scenehintoverlay.cpp
    scenehintoverlay.h
    docsearch.cpp
    docsearch.h
    camerapaths.cpp
    camerapaths.h
    texturecode.cpp
    texturecode.h
    texthelpers.h
    libraryfolders.cpp
    libraryfolders.h
    librarytreefocus.cpp
    librarytreefocus.h
    librarytreewidget.cpp
    librarytreewidget.h

    # --- ENGINE & PARSING ---
    surfaceengine.cpp
    surfaceengine.h
    expressionparser.h

    # --- RENDERING & GL ---
    glwidget.cpp
    glwidget.h
    geometrybuilder.cpp
    geometrybuilder.h



    # --- UTILITIES & INPUT ---
    uistylemanager.cpp
    uistylemanager.h
    inputhandler.cpp
    inputhandler.h

    # --- NUOVO MANAGER UNIFICATO ---
    librarymanager.cpp
    librarymanager.h
    shaders/common.glsl


    shaders/surface.frag
    shaders/surface.vert

    docs/documentation.html
    synthesizer.h
    synthesizer.cpp
    update_resources.py
    glsltranslator.h
    glsltranslator.cpp
    videorecorder.h
    videorecorder.cpp
    librarymenucontroller.h
    librarymenucontroller.cpp
    presetserializer.h
    presetserializer.cpp
    libraryfileoperations.h
    libraryfileoperations.cpp
    librarydragdrophandler.h
    librarydragdrophandler.cpp
    audiocontroller.h
    audiocontroller.cpp
    statusbarscroller.h
    statusbarscroller.cpp
    icon.icns
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/app.rc>
    $<$<PLATFORM_ID:iOS>:${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets>
    shaders/raymarch_template.txt
    geodesiccalculator.h
    geodesiccalculator.cpp
    tubecurvesampler.h
    tubecurvesampler.cpp
    inputvalidator.h
    inputvalidator.cpp
    shaders/geodesic.glsl

)

set_source_files_properties(docs/documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# 🔹 Collega le librerie Qt
target_link_libraries(SurfaceExplorer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Multimedia
    Qt6::ShaderTools
    Qt6::ShaderToolsPrivate
    Qt6::Concurrent
)

# Versione marketing disponibile al C++ (la stampa il dialogo About). Definita
# qui, FUORI dai rami di piattaforma, cosi' vale anche su Windows/Linux/Android
# e non solo nel ramo APPLE dove stanno le proprieta' del bundle.
target_compile_definitions(SurfaceExplorer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

# =========================================================
# CONFIGURAZIONE PIATTAFORMA (iOS Device vs macOS)
# =========================================================

if (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS Device...")
    set_target_properties(SurfaceExplorer PROPERTIES QT_PLUGINS "darwinmediaplugin")
    qt_import_plugins(SurfaceExplorer
        INCLUDE Qt6::QDarwinMediaPlugin
        EXCLUDE Qt6::QFFmpegMediaPlugin
    )
    # 1. FORZATURA GLOBALE DELLA FIRMA (Ferma il bulldozer di CMake)
    # TEAM: deve essere quello dei PROFILI DI PROVISIONING installati, non
    # quello del certificato "Apple Development". Sono due team diversi:
    #   BAPKX72394 = Apple Distribution GAETANO MOSCHETTI -> e' il team dei
    #                profili (compreso "iOS Team Store Provisioning Profile"),
    #                l'unico con cui si puo' archiviare per TestFlight;
    #   AJ655XKJR8 = Apple Development adenio@libero.it -> team SENZA alcun
    #                profilo per questo bundle id.
    # Con AJ655XKJR8 l'archive fallisce con "No Account for Team" +
    # "No profiles for 'com.dioscorid.surfaceexplorer' were found".
    set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "BAPKX72394")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
    set(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.dioscorid.surfaceexplorer")
    # 2. Architettura e Versione
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
    # 3. Proprietà dell'App
    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dioscorid.surfaceexplorer"
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "62"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "62"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
    )

    # --- App icon via Asset Catalog ---
    # Copia Assets.xcassets nelle Resources del bundle; Xcode lo compila in
    # Assets.car automaticamente. ASSETCATALOG_COMPILER_APPICON_NAME dice a
    # iOS quale icon-set usare come app icon.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(SurfaceExplorer PROPERTIES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    )

    # 4. Framework di Sistema iOS
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework Foundation"
        "-framework UIKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics"
    )

elseif (APPLE)
    # BUNDLE ID DIVERSO PER I DUE CANALI MACOS.
    #
    # DMG (Developer ID) e App Store escono dallo STESSO target e finivano con lo
    # stesso identificatore: per launchd, il Dock e TCC erano LA STESSA
    # applicazione, pur essendo due bundle distinti in /Applications.
    # Conseguenze MISURATE con le due installate insieme:
    #  - condividevano il file di preferenze, quindi NON potevano puntare a
    #    librerie diverse: l'ultima che scriveva libraryRootPath vinceva, e
    #    l'altra si ritrovava la libreria cambiata sotto i piedi;
    #  - il Dock non riusciva a portare avanti la seconda: cliccando l'icona il
    #    sistema considerava l'app gia' in esecuzione e non attivava nulla --
    #    sembrava che "non si aprisse", mentre il processo era vivo e sano
    #    (sample: fermo in mach_msg dentro il run loop, cioe' inattivo).
    #
    # SI RINOMINA IL CANALE DMG, non l'altro: l'id dell'App Store e' l'identita'
    # registrata su App Store Connect e legata alle build gia' su TestFlight;
    # cambiarlo richiederebbe un nuovo record e un nuovo provisioning. Il DMG lo
    # distribuisce l'utente per conto proprio, quindi un id nuovo non rompe
    # nulla a valle (chi aggiorna ritrova le preferenze azzerate: la libreria si
    # ripunta da "Change Library Folder...").
    #
    # IL DISCRIMINANTE E' IL GENERATORE, che e' gia' cio' che separa i due
    # canali: il Mac App Store passa da `xcodebuild` col generatore Xcode
    # (release_testflight_mac.sh), il DMG dalla build normale Ninja/Makefile
    # (release_macos.sh, che firma build/Desktop_Qt_6_10-Release). Non si e'
    # usata una option() perche' andrebbe ricordata a ogni configure manuale: e'
    # esattamente il tipo di passo che si dimentica, e il sintomo tornerebbe
    # silenzioso.
    # ECCEZIONE: SE_APPSTORE_ID forza l'identita' App Store anche senza Xcode.
    # Serve a test_sandbox_local.sh, che compila con Ninja/Makefile ma firma con
    # gli entitlements dell'App Store per provare la sandbox in locale: quella
    # build DEVE avere l'id App Store, altrimenti simula il canale sbagliato.
    # MISURATO senza questa eccezione: l'app di test nasceva con l'id "...dmg",
    # quindi --reset azzerava il container e il dominio dell'App Store mentre
    # l'app leggeva e scriveva quelli del DMG -- il reset sembrava non
    # funzionare e la libreria si riapriva sul percorso precedente. In piu' i
    # due bundle con lo stesso id (test e /Applications) si contendevano
    # l'attivazione, da cui l'icona nel Dock sostituita da un'altra copia.
    if (CMAKE_GENERATOR STREQUAL "Xcode" OR SE_APPSTORE_ID)
        set(SE_MACOS_BUNDLE_ID "com.dioscorid.surfaceexplorer")        # App Store
    else()
        set(SE_MACOS_BUNDLE_ID "com.dioscorid.surfaceexplorer.dmg")    # Developer ID
    endif()

    # --- CONFIGURAZIONE NOME APP MACOS ---
    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE

        # 0. PLIST DEDICATO AL MAC — non Info.plist, che e' quello iOS.
        # Quello dichiara LSRequiresIPhoneOS=true e porta chiavi UIKit
        # (UILaunchScreen, orientamenti, status bar) prive di senso qui; in piu'
        # gli manca LSApplicationCategoryType, obbligatoria sul Mac App Store:
        # senza, Distribute App fallisce con "The product archive is invalid".
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-macos.plist"

        # 1. Nome nella BARRA DEI MENU (Accanto alla Mela )
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"

        # 2. Nome dell'icona nel Launchpad/Dock
        MACOSX_BUNDLE_DISPLAY_NAME "Surface Explorer"

        # 3. Nome del file fisico (.app)
        OUTPUT_NAME "SurfaceExplorer"

        # 4. Identificativo univoco (diverso fra DMG e App Store: vedi sopra)
        MACOSX_BUNDLE_GUI_IDENTIFIER "${SE_MACOS_BUNDLE_ID}"

        # 5. Icona (TUTTO MINUSCOLO!)
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
    )

# Copia documentation.html dentro l'app (risorsa)
    set_source_files_properties(docs/documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # --- CANALE MAC APP STORE (xcodebuild archive) ---
    # Serve SOLO al generatore Xcode, usato da release_testflight_mac.sh: la
    # build normale (Ninja/Makefile, DMG Developer ID) ignora questi attributi
    # e non cambia comportamento.
    #
    # Gli stessi valori esistono nel ramo iOS qui sopra, ma erano CHIUSI dentro
    # if(IOS): senza duplicarli qui, l'archive macOS non troverebbe team ne'
    # bundle version e fallirebbe la firma automatica.
    # Stesso team del ramo iOS: quello dei profili installati (vedi il commento
    # esteso li' sopra). Se cambia, va cambiato in ENTRAMBI i punti.
    set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "BAPKX72394")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
    # Dalla stessa variabile del bundle id qui sopra, non da una seconda
    # costante: sotto Xcode vale "com.dioscorid.surfaceexplorer" (l'identita'
    # registrata su App Store Connect), e i due punti non possono divergere.
    set(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${SE_MACOS_BUNDLE_ID}")

    set_target_properties(SurfaceExplorer PROPERTIES
        # Build number e versione marketing: stessi campi che il bump di
        # release_testflight_mac.sh tiene allineati con Info.plist.
        MACOSX_BUNDLE_BUNDLE_VERSION "62"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "62"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"

        # Sandbox: obbligatoria sull'App Store. Xcode firma con questi
        # entitlements durante l'archive, senza passare da codesign a mano.
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_CURRENT_SOURCE_DIR}/ExC/Mac/macos_appstore.entitlements"
    )

elseif (UNIX AND NOT ANDROID)
    # Impostiamo il nome dell'eseguibile finale (tutto minuscolo per standard Linux)
    set_target_properties(SurfaceExplorer PROPERTIES OUTPUT_NAME "surface-explorer")

    # Salva i percorsi delle librerie Qt dentro l'eseguibile per farlo avviare dal menu
        set_target_properties(SurfaceExplorer PROPERTIES
            INSTALL_RPATH "${CMAKE_PREFIX_PATH}/lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )

    # 1. Installa l'eseguibile in /usr/local/bin
    install(TARGETS SurfaceExplorer DESTINATION bin)

    # 2. Installa il file .desktop per il menu
    install(FILES SurfaceExplorer.desktop DESTINATION share/applications)

    # 3. Installa l'icona in tutte le dimensioni standard hicolor
    # (Icon=surface-explorer nel file .desktop cerca surface-explorer.png
    # nella cartella corrispondente alla risoluzione richiesta dal DE)
    foreach(_size 16 22 24 32 48 64 128 256 512)
        install(FILES linux-icons/icon_${_size}.png
            DESTINATION share/icons/hicolor/${_size}x${_size}/apps
            RENAME surface-explorer.png)
    endforeach()
endif()

# =========================================================
# CONFIGURAZIONE ORIENTAMENTO E FIRMA (FIX DEFINITIVO)
# =========================================================

if(ANDROID)
    # Imposta la cartella sorgente per il pacchetto Android (dove sta il Manifest)
    set_property(TARGET SurfaceExplorer PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")

    # Configurazioni specifiche per l'APK
    set_target_properties(SurfaceExplorer PROPERTIES
        QT_ANDROID_VERSION_NAME "1.0"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_MIN_SDK_VERSION 26
    )
endif()

# --- CONFIGURAZIONE NATIVA PER REGISTRAZIONE VIDEO SU iOS ---
if(IOS)
    # Aggiunge i file Objective-C++ alla compilazione
    target_sources(SurfaceExplorer PRIVATE
        nativevideoencoder.h
        nativevideoencoder_ios.mm
        ioseditmenu.h
        ioseditmenu.mm
    )

    # 1. Framework Apple e librerie di compressione necessarie a FFmpeg
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework AVFoundation"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework UIKit"
        "-framework Foundation"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework Security"
        "-lbz2"
        "-lz"
        "-liconv"
    )

    # 2. Troviamo dinamicamente la cartella "lib" di Qt 6.10
    get_target_property(QT_CORE_LOC Qt6::Core LOCATION)
    get_filename_component(QT_FRAMEWORK_DIR "${QT_CORE_LOC}" DIRECTORY)
    get_filename_component(QT_LIB_DIR "${QT_FRAMEWORK_DIR}" DIRECTORY)
endif()

# --- CONFIGURAZIONE NATIVA PER REGISTRAZIONE VIDEO SU macOS ---
# L'encoder AVFoundation sostituisce ffmpeg su TUTTO macOS (non solo nella build
# App Store): sotto sandbox non si possono lanciare eseguibili esterni al bundle,
# e ffmpeg-kit-full-gpl non e' incorporabile (GPL incompatibile con l'App Store).
if(APPLE AND NOT IOS)
    target_sources(SurfaceExplorer PRIVATE
        nativevideoencoder.h
        nativevideoencoder_mac.mm
    )

    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework AVFoundation"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework Foundation"
        "-framework VideoToolbox"
    )
endif()

# --- TEST INTERNI (round-trip dei preset, orologi, scenari d'uso) ---
# Solo nelle build Debug desktop: sono la rete di sicurezza di chi sviluppa
# (ExC/Mac/Test/*.sh li lanciano sulla build Debug) e nei binari distribuiti
# sarebbero solo peso, piu' tre opzioni da riga di comando che nessun utente
# deve avere. Le build di rilascio restano fuori da sole: Release di Qt Creator
# e, per l'App Store, il generatore Xcode (multi-config: CMAKE_BUILD_TYPE e'
# vuoto). Su iOS/Android main.cpp non li ha mai chiamati.
# SE_SELF_TESTS e' cio' che main.cpp guarda per agganciarli.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ANDROID AND NOT IOS)
    target_sources(SurfaceExplorer PRIVATE
        presetroundtrip.h
        presetroundtrip.cpp
        clocktest.h
        clocktest.cpp
        scenariotest.h
        scenariotest.cpp
        shadercompilelog.h
    )
    target_compile_definitions(SurfaceExplorer PRIVATE SE_SELF_TESTS)
endif()

# --- SECURITY-SCOPED BOOKMARK (solo canale App Store, ma compilato ovunque) ---
# Il .mm ha una sezione macOS e un fallback no-op per le altre piattaforme,
# cosi' i punti di chiamata non hanno #ifdef. Fuori da Apple va forzato a C++,
# altrimenti il compilatore lo tratta come Objective-C++ e non compila.
target_sources(SurfaceExplorer PRIVATE
    securitybookmark.h
    securitybookmark.mm
)
if(NOT APPLE)
    set_source_files_properties(securitybookmark.mm PROPERTIES LANGUAGE CXX)
endif()
