# Vendored muParser 2.3.5 (BSD-2-Clause, see LICENSE), library sources only.
# Upstream's samples/, test/, muParserInt (integer-only parser variant) and
# muParserDLL (C DLL API) are not vendored, they are unused here.
add_library(muparser STATIC
    include/muParser.h
    include/muParserBase.h
    include/muParserBytecode.h
    include/muParserCallback.h
    include/muParserDef.h
    include/muParserError.h
    include/muParserFixes.h
    include/muParserTemplateMagic.h
    include/muParserToken.h
    include/muParserTokenReader.h
    src/muParser.cpp
    src/muParserBase.cpp
    src/muParserBytecode.cpp
    src/muParserCallback.cpp
    src/muParserError.cpp
    src/muParserTokenReader.cpp
)

target_include_directories(muparser SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Static build: plain symbols, no dllimport/dllexport decoration, see
# include/muParserFixes.h. MUP_USE_OPENMP is left undefined (upstream default)
# so no OpenMP dependency is pulled in.
target_compile_definitions(muparser
    PUBLIC
        MUPARSER_STATIC
        $<$<PLATFORM_ID:Windows>:UNICODE>
        $<$<PLATFORM_ID:Windows>:_UNICODE>
)

# Vendored third-party code: not a Qt target, skip Qt's AUTOMOC/AUTOUIC/AUTORCC.
set_target_properties(muparser PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
