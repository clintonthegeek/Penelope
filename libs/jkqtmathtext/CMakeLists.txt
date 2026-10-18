cmake_minimum_required(VERSION 3.19)
project(jkqtmathtext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

# --- JKQTCommon sources (subset needed by JKQTMathText) ---
set(JKQTCOMMON_SOURCES
    jkqtcommon/jkqtpbasicimagetools.cpp
    jkqtcommon/jkqtpcachingtools.cpp
    jkqtcommon/jkqtpcodestructuring.cpp
    jkqtcommon/jkqtpconcurrencytools.cpp
    jkqtcommon/jkqtpcsstools.cpp
    jkqtcommon/jkqtpdebuggingtools.cpp
    jkqtcommon/jkqtpdrawingtools.cpp
    jkqtcommon/jkqtpenhancedpainter.cpp
    jkqtcommon/jkqtpexpected.cpp
    jkqtcommon/jkqtpgeometrytools.cpp
    jkqtcommon/jkqtphighrestimer.cpp
    jkqtcommon/jkqtpicons.cpp
    jkqtcommon/jkqtpmathtools.cpp
    jkqtcommon/jkqtpstringtools.cpp
    jkqtcommon/jkqttools.cpp
)

# --- JKQTMathText sources ---
set(JKQTMATHTEXT_SOURCES
    jkqtmathtext.cpp
    jkqtmathtexttools.cpp
    jkqtmathtextlabel.cpp
    nodes/jkqtmathtextboxinstructionnode.cpp
    nodes/jkqtmathtextbracenode.cpp
    nodes/jkqtmathtextdecoratednode.cpp
    nodes/jkqtmathtextfracnode.cpp
    nodes/jkqtmathtexthorizontallistnode.cpp
    nodes/jkqtmathtextinstructionnode.cpp
    nodes/jkqtmathtextmatrixnode.cpp
    nodes/jkqtmathtextmodifyenvironmentnode.cpp
    nodes/jkqtmathtextnode.cpp
    nodes/jkqtmathtextnodetools.cpp
    nodes/jkqtmathtextnoopnode.cpp
    nodes/jkqtmathtextsqrtnode.cpp
    nodes/jkqtmathtextsubsupernode.cpp
    nodes/jkqtmathtextsymbolnode.cpp
    nodes/jkqtmathtexttextnode.cpp
    nodes/jkqtmathtextverbatimnode.cpp
    nodes/jkqtmathtextverticallistnode.cpp
    nodes/jkqtmathtextwhitespacenode.cpp
    parsers/jkqtmathtextlatexparser.cpp
    parsers/jkqtmathtextparser.cpp
)

# --- Font resources ---
qt_add_resources(FONT_RESOURCES resources/xits.qrc resources/firamath.qrc)

# --- Static library target ---
add_library(jkqtmathtext STATIC
    ${JKQTCOMMON_SOURCES}
    ${JKQTMATHTEXT_SOURCES}
    ${FONT_RESOURCES}
)

set_target_properties(jkqtmathtext PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(jkqtmathtext PUBLIC
    JKQTMATHTEXT_COMPILED_WITH_XITS
    JKQTMATHTEXT_COMPILED_WITH_FIRAMATH
)

# Include paths:
#   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}       -> resolves "jkqtcommon/..."
#   PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/..     -> resolves "jkqtmathtext/..." for consumers
target_include_directories(jkqtmathtext
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jkqtmathtext
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
)

# Vendored: built into whichever target links it; not installed separately.
