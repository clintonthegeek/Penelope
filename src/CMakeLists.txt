# Generate KConfig settings class from kcfg schema
kconfig_add_kcfg_files(PENELOPE_KCFG_SRCS app/penelopesettings.kcfgc GENERATE_MOC)

add_library(PenelopeCore STATIC
    ${PENELOPE_KCFG_SRCS}
    app/mainwindow.cpp
    app/mainwindow.h
    app/metadatastore.cpp
    app/metadatastore.h
    markdown/documentbuilder.cpp
    markdown/documentbuilder.h
    markdown/codeblockhighlighter.cpp
    markdown/codeblockhighlighter.h
    markdown/codespancollector.h
    markdown/markdownhighlighter.cpp
    markdown/markdownhighlighter.h
    markdown/footnoteparser.cpp
    markdown/footnoteparser.h
    canvas/pageitem.cpp
    canvas/pageitem.h
    canvas/documentview.cpp
    canvas/documentview.h
    canvas/pdfpageitem.cpp
    canvas/pdfpageitem.h
    canvas/rendercache.cpp
    canvas/rendercache.h
    canvas/qtboxrenderer.cpp
    canvas/qtboxrenderer.h
    canvas/webviewitem.cpp
    canvas/webviewitem.h
    print/headerfooterrenderer.cpp
    print/headerfooterrenderer.h
    print/printcontroller.cpp
    print/printcontroller.h
    style/pagelayout.cpp
    style/pagelayout.h
    style/masterpage.h
    style/pagetemplate.cpp
    style/pagetemplate.h
    style/pagetemplatemanager.cpp
    style/pagetemplatemanager.h
    style/fontfeatures.h
    style/paragraphstyle.cpp
    style/paragraphstyle.h
    style/characterstyle.cpp
    style/characterstyle.h
    style/tablestyle.cpp
    style/tablestyle.h
    style/footnotestyle.cpp
    style/footnotestyle.h
    style/stylemanager.cpp
    style/stylemanager.h
    style/styletreemodel.cpp
    style/styletreemodel.h
    style/colorpalette.cpp
    style/colorpalette.h
    style/fontdegradationmap.cpp
    style/fontdegradationmap.h
    style/thememanager.cpp
    style/thememanager.h
    style/themecomposer.cpp
    style/themecomposer.h
    style/resourcestore.h
    style/palettemanager.cpp
    style/palettemanager.h
    style/typeset.cpp
    style/typeset.h
    style/typesetmanager.cpp
    style/typesetmanager.h
    typography/hyphenator.cpp
    typography/hyphenator.h
    typography/shortwords.cpp
    typography/shortwords.h
    export/rtfexporter.cpp
    export/rtfexporter.h
    export/contentrtfexporter.cpp
    export/contentrtfexporter.h
    export/rtffilteroptions.h
    export/rtfutils.h
    # PDF rendering pipeline (Phase 4)
    model/contentbuilder.cpp
    model/contentbuilder.h
    model/contentmodel.h
    model/pdfexportoptions.h
    model/pagerangeparser.cpp
    model/pagerangeparser.h
    model/contentfilter.cpp
    model/contentfilter.h
    font/fontmanager.cpp
    font/fontmanager.h
    font/textshaper.cpp
    font/textshaper.h
    font/sfnt.cpp
    font/sfnt.h
    font/hersheyfont.cpp
    font/hersheyfont.h
    layout/layoutengine.cpp
    layout/layoutengine.h
    layout/linebreaker.cpp
    layout/linebreaker.h
    pdf/pdfwriter.cpp
    pdf/pdfwriter.h
    pdf/pdfgenerator.cpp
    pdf/pdfgenerator.h
    pdf/pdfboxrenderer.cpp
    pdf/pdfboxrenderer.h
    # Rendering base class
    render/boxtreerenderer.cpp
    render/boxtreerenderer.h
    # Widgets
    widgets/pagelayoutwidget.cpp
    widgets/pagelayoutwidget.h
    widgets/droptargetlineedit.cpp
    widgets/droptargetlineedit.h
    widgets/headerfooterdialog.cpp
    widgets/headerfooterdialog.h
    widgets/sidebar.cpp
    widgets/sidebar.h
    widgets/itemselectorbar.cpp
    widgets/itemselectorbar.h
    widgets/typedockwidget.cpp
    widgets/typedockwidget.h
    widgets/colordockwidget.cpp
    widgets/colordockwidget.h
    widgets/colorselectorwidget.cpp
    widgets/colorselectorwidget.h
    widgets/pagedockwidget.cpp
    widgets/pagedockwidget.h
    widgets/stylepropertieseditor.cpp
    widgets/stylepropertieseditor.h
    widgets/tablestylepropertieseditor.cpp
    widgets/tablestylepropertieseditor.h
    widgets/footnoteconfigwidget.cpp
    widgets/footnoteconfigwidget.h
    widgets/preferencesdialog.cpp
    widgets/preferencesdialog.h
    widgets/rtfcopyoptionsdialog.cpp
    widgets/rtfcopyoptionsdialog.h
    widgets/languagepickerdialog.cpp
    widgets/languagepickerdialog.h
    widgets/toolview.cpp
    widgets/toolview.h
    widgets/filebrowserdock.cpp
    widgets/filebrowserdock.h
    widgets/tocwidget.cpp
    widgets/tocwidget.h
    widgets/pdfexportdialog.cpp
    widgets/pdfexportdialog.h
    widgets/documenttab.cpp
    widgets/documenttab.h
    widgets/resourcepickerwidget.cpp
    widgets/resourcepickerwidget.h
    widgets/palettepickerwidget.cpp
    widgets/palettepickerwidget.h
    widgets/pagetemplatepickerwidget.cpp
    widgets/pagetemplatepickerwidget.h
    widgets/typesetpickerwidget.cpp
    widgets/typesetpickerwidget.h
    widgets/themepickerdock.cpp
    widgets/themepickerdock.h
)

target_include_directories(PenelopeCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${CMAKE_CURRENT_SOURCE_DIR}/markdown
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas
        ${CMAKE_CURRENT_SOURCE_DIR}/print
        ${CMAKE_CURRENT_SOURCE_DIR}/style
        ${CMAKE_CURRENT_SOURCE_DIR}/typography
        ${CMAKE_CURRENT_SOURCE_DIR}/export
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        # PDF pipeline directories (Phase 4)
        ${CMAKE_CURRENT_SOURCE_DIR}/model
        ${CMAKE_CURRENT_SOURCE_DIR}/font
        ${CMAKE_CURRENT_SOURCE_DIR}/layout
        ${CMAKE_CURRENT_SOURCE_DIR}/pdf
        ${CMAKE_CURRENT_SOURCE_DIR}/render
        ${CMAKE_CURRENT_BINARY_DIR}
)

# MD4C system library
find_library(MD4C_LIBRARY NAMES md4c REQUIRED)
find_path(MD4C_INCLUDE_DIR NAMES md4c.h REQUIRED)

# libhyphen for hyphenation
find_library(HYPHEN_LIBRARY NAMES hyphen REQUIRED)
find_path(HYPHEN_INCLUDE_DIR NAMES hyphen.h REQUIRED)

# Bundle theme JSON files as Qt resources
qt_add_resources(PenelopeCore "themes"
    PREFIX "/themes"
    BASE themes
    FILES
        themes/default.json
        themes/academic.json
        themes/hershey.json
)

# Bundle color palette JSON files as Qt resources
qt_add_resources(PenelopeCore "palettes"
    PREFIX "/palettes"
    BASE themes/palettes
    FILES
        themes/palettes/default-light.json
        themes/palettes/academic-light.json
        themes/palettes/hershey-ink.json
        themes/palettes/solarized-light.json
        themes/palettes/solarized-dark.json
        themes/palettes/dracula.json
        themes/palettes/nord.json
        themes/palettes/catppuccin-mocha.json
        themes/palettes/catppuccin-latte.json
        themes/palettes/gruvbox-dark.json
        themes/palettes/gruvbox-light.json
        themes/palettes/tokyo-night.json
        themes/palettes/rose-pine.json
        themes/palettes/rose-pine-dawn.json
        themes/palettes/one-dark.json
        themes/palettes/one-light.json
        themes/palettes/everforest-dark.json
        themes/palettes/everforest-light.json
        themes/palettes/flexoki-dark.json
        themes/palettes/flexoki-light.json
        themes/palettes/github-light.json
)

# Bundle page template JSON files as Qt resources
qt_add_resources(PenelopeCore "templates"
    PREFIX "/templates"
    BASE themes/templates
    FILES
        themes/templates/default.json
        themes/templates/academic.json
        themes/templates/minimal.json
)

# Bundle type set JSON files as Qt resources
qt_add_resources(PenelopeCore "typesets"
    PREFIX "/typesets"
    BASE themes/typesets
    FILES
        themes/typesets/default.json
        themes/typesets/academic.json
        themes/typesets/hershey.json
        themes/typesets/georgia-trebuchet-courier.json
        themes/typesets/georgia-inter-jetbrains.json
        themes/typesets/noto-source-family.json
        themes/typesets/ubuntu-family.json
        themes/typesets/fira-family.json
        themes/typesets/liberation-serif-sans-mono.json
        themes/typesets/dejavu-family.json
        themes/typesets/typewriter.json
        themes/typesets/sans-forward.json
        themes/typesets/serif-scholar.json
        themes/typesets/inverted-contrast.json
        themes/typesets/libertine-biolinum-hack.json
)

# Bundle hyphenation dictionaries as Qt resources
qt_add_resources(PenelopeCore "dicts"
    PREFIX "/dicts"
    BASE dicts
    FILES
        dicts/hyph_en_US.dic
        dicts/hyph_en_GB.dic
)

# Bundle fallback symbol font as Qt resource
qt_add_resources(PenelopeCore "fonts"
    PREFIX "/fonts"
    BASE dicts
    FILES
        dicts/PrettySymbolsFallback.ttf
)

# Bundle glyph artwork (checkbox SVGs) as Qt resources
qt_add_resources(PenelopeCore "glyphs"
    PREFIX "/glyphs"
    BASE glyphs
    FILES
        glyphs/checkbox-unchecked.svg
        glyphs/checkbox-checked.svg
)

# Bundle Hershey vector font data as Qt resources
qt_add_resources(PenelopeCore "hershey"
    PREFIX "/hershey"
    BASE hershey
    FILES
        hershey/astrology.jhf
        hershey/cursive.jhf
        hershey/cyrilc_1.jhf
        hershey/cyrillic.jhf
        hershey/futural.jhf
        hershey/futuram.jhf
        hershey/gothgbt.jhf
        hershey/gothgrt.jhf
        hershey/gothiceng.jhf
        hershey/gothicger.jhf
        hershey/gothicita.jhf
        hershey/gothitt.jhf
        hershey/greek.jhf
        hershey/greekc.jhf
        hershey/greeks.jhf
        hershey/japanese.jhf
        hershey/markers.jhf
        hershey/mathlow.jhf
        hershey/mathupp.jhf
        hershey/meteorology.jhf
        hershey/music.jhf
        hershey/rowmand.jhf
        hershey/rowmans.jhf
        hershey/rowmant.jhf
        hershey/scriptc.jhf
        hershey/scripts.jhf
        hershey/symbolic.jhf
        hershey/timesg.jhf
        hershey/timesi.jhf
        hershey/timesib.jhf
        hershey/timesr.jhf
        hershey/timesrb.jhf
)

target_include_directories(PenelopeCore
    PRIVATE
        ${MD4C_INCLUDE_DIR}
        ${HYPHEN_INCLUDE_DIR}
        ${FONTCONFIG_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS}
        ${HARFBUZZ_SUBSET_INCLUDE_DIRS}
)

target_link_libraries(PenelopeCore
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::PrintSupport
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::IconThemes
        KF6::ConfigCore
        KF6::ConfigGui
        KF6::ConfigWidgets
        KF6::KIOFileWidgets
        KF6::SyntaxHighlighting
        ${MD4C_LIBRARY}
        ${HYPHEN_LIBRARY}
        # PDF pipeline dependencies (Phase 4)
        Poppler::Qt6
        Freetype::Freetype
        ICU::uc
        ICU::i18n
        ZLIB::ZLIB
        ${FONTCONFIG_LIBRARIES}
        ${HARFBUZZ_LIBRARIES}
        ${HARFBUZZ_ICU_LIBRARIES}
        ${HARFBUZZ_SUBSET_LIBRARIES}
)

qt_add_executable(Penelope
    WIN32 MACOSX_BUNDLE
    app/main.cpp
)
set_target_properties(Penelope PROPERTIES OUTPUT_NAME penelope)

if(NOT KF6DBusAddons_FOUND)
    target_sources(Penelope PRIVATE
        app/singleinstanceguard.h
        app/singleinstanceguard.cpp
    )
    target_link_libraries(Penelope PRIVATE Qt6::Network)
endif()

# Embed XMLGUI resource file
qt_add_resources(Penelope "xmlgui"
    PREFIX "/kxmlgui5/penelope"
    BASE app
    FILES app/penelopeui.rc
)

target_link_libraries(Penelope
    PRIVATE
        PenelopeCore
)

if(KF6DBusAddons_FOUND)
    target_link_libraries(Penelope PRIVATE KF6::DBusAddons)
endif()

include(GNUInstallDirs)

install(TARGETS Penelope
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES app/penelopeui.rc
    DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/penelope
)

install(FILES ../org.penelope.Penelope.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES ../icons/penelope.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)

# Third-party license notices (bundled in .deb under /usr/share/doc/penelope/)
install(FILES ../LICENSES/DejaVu-License.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/penelope
    RENAME copyright-dejavu
)
