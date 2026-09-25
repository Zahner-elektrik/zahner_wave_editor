# wavecore: data model, sampling engine, spectrum analysis, expression evaluation,
# .zwj, CSV and binary IO.
# GUI-free by design (Qt6::Core only) so everything in here is unit-testable.
add_library(wavecore STATIC
    version.cpp
    version.h
    segment.cpp
    segment.h
    wavelayer.cpp
    wavelayer.h
    wavedocument.cpp
    wavedocument.h
    documenttransform.cpp
    documenttransform.h
    sampling.cpp
    sampling.h
    spectrum.cpp
    spectrum.h
    segmentcontinuity.cpp
    segmentcontinuity.h
    expression.cpp
    expression.h
    pointinterpolation.cpp
    pointinterpolation.h
    zwjio.cpp
    zwjio.h
    csvio.cpp
    csvio.h
    binaryio.cpp
    binaryio.h
)

target_include_directories(wavecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# version.cpp reads the generated zwe_version.h. Private, because the generated
# defines are an implementation detail of version.h's API.
target_include_directories(wavecore PRIVATE ${ZWE_GENERATED_INCLUDE_DIR})
target_link_libraries(wavecore PUBLIC Qt6::Core PRIVATE muparser)
