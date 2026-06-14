cmake_minimum_required(VERSION 3.27)
if(NOT DEFINED SENTINEL_VERSION)
    set(SENTINEL_VERSION "1.0.0")
endif()
project(Sentinel VERSION ${SENTINEL_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ─── Qt 6 ────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Network Charts Sql Test
)

enable_testing()

# ─────────────────────────────────────────────────────────────────────────────
# sentinel_core: all non-UI, non-main sources compiled as a static library.
# Both the application and unit tests link against this.
# ─────────────────────────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/AppConfig.cpp
    src/core/Database.cpp
    src/core/SentinelLogger.cpp
    src/core/DataExporter.cpp
    src/core/UpdateChecker.cpp
    src/audit/ProvenanceLog.cpp
    src/ingest/UKPoliceSource.cpp
    src/ingest/WeatherSource.cpp
    src/ingest/CsvImporter.cpp
    src/ingest/DataQualityScorer.cpp
    src/ingest/IngestEnricher.cpp
    src/nlp/MOExtractor.cpp
    src/nlp/CrimeClassifier.cpp
    src/models/PoissonBaseline.cpp
    src/models/HawkesProcess.cpp
    src/models/SeriesDetector.cpp
    src/models/NearRepeatVictimisation.cpp
    src/models/TemporalFeatures.cpp
    src/inference/GeographicProfiler.cpp
    src/inference/MOAnalyser.cpp
    src/inference/EvidenceScorer.cpp
    src/inference/HintEngine.cpp
    src/inference/AnomalyDetector.cpp
    src/inference/CoOffendingAnalyser.cpp
    src/inference/LeadReportGenerator.cpp
    src/models/EnsemblePredictor.cpp
    src/models/KDEHotspot.cpp
    src/models/RiskForecaster.cpp
    src/models/GPRegression.cpp
    src/models/BayesianHierarchical.cpp
    src/benchmark/BenchmarkMetrics.cpp
    src/benchmark/BiasAuditor.cpp
    src/benchmark/CalibrationAnalyser.cpp
    src/api/LocalApiServer.cpp
)

set(CORE_HEADERS
    src/core/CrimeEvent.h
    src/core/AppConfig.h
    src/core/Database.h
    src/core/SentinelLogger.h
    src/core/DataExporter.h
    src/core/UpdateChecker.h
    src/audit/ProvenanceLog.h
    src/ingest/DataSource.h
    src/ingest/UKPoliceSource.h
    src/ingest/WeatherSource.h
    src/ingest/CsvImporter.h
    src/ingest/DataQualityScorer.h
    src/ingest/IngestEnricher.h
    src/nlp/MOExtractor.h
    src/nlp/CrimeClassifier.h
    src/models/PoissonBaseline.h
    src/models/HawkesProcess.h
    src/models/SeriesDetector.h
    src/models/NearRepeatVictimisation.h
    src/models/TemporalFeatures.h
    src/inference/GeographicProfiler.h
    src/inference/MOAnalyser.h
    src/inference/EvidenceScorer.h
    src/inference/HintEngine.h
    src/inference/AnomalyDetector.h
    src/inference/CoOffendingAnalyser.h
    src/inference/LeadReportGenerator.h
    src/models/EnsemblePredictor.h
    src/models/KDEHotspot.h
    src/models/RiskForecaster.h
    src/models/GPRegression.h
    src/benchmark/BenchmarkMetrics.h
    src/benchmark/BiasAuditor.h
    src/benchmark/CalibrationAnalyser.h
    src/api/LocalApiServer.h
)

add_library(sentinel_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(sentinel_core PUBLIC src)
target_compile_definitions(sentinel_core PRIVATE SENTINEL_TESTING)
target_link_libraries(sentinel_core PUBLIC
    Qt6::Core Qt6::Network Qt6::Sql Qt6::Charts Qt6::Widgets
)

find_package(Qt6 QUIET COMPONENTS HttpServer)
if(TARGET Qt6::HttpServer)
    target_compile_definitions(sentinel_core PUBLIC SENTINEL_HAS_QT_HTTPSERVER)
    target_link_libraries(sentinel_core PUBLIC Qt6::HttpServer)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# UI sources (depend on sentinel_core + Qt6::Widgets)
# ─────────────────────────────────────────────────────────────────────────────
set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/AuditLogWidget.cpp
    src/ui/DashboardWidget.cpp
    src/ui/MapWidget.cpp
    src/ui/EventsTableWidget.cpp
    src/ui/CaseWorkspaceWidget.cpp
    src/ui/GeoProfileHeatmapWidget.cpp
    src/ui/CoOffendingGraphWidget.cpp
    src/ui/LeadsWidget.cpp
    src/ui/AnalyticsWidget.cpp
    src/ui/CalibrationDashboardWidget.cpp
    src/ui/TemporalHeatmapWidget.cpp
    src/ui/SettingsWidget.cpp
    src/ui/DebugConsoleWidget.cpp
)

set(UI_HEADERS
    src/ui/MainWindow.h
    src/ui/AuditLogWidget.h
    src/ui/DashboardWidget.h
    src/ui/MapWidget.h
    src/ui/EventsTableWidget.h
    src/ui/CaseWorkspaceWidget.h
    src/ui/GeoProfileHeatmapWidget.h
    src/ui/CoOffendingGraphWidget.h
    src/ui/LeadsWidget.h
    src/ui/AnalyticsWidget.h
    src/ui/CalibrationDashboardWidget.h
    src/ui/TemporalHeatmapWidget.h
    src/ui/SettingsWidget.h
    src/ui/DebugConsoleWidget.h
)

# ─────────────────────────────────────────────────────────────────────────────
# Main executable
# ─────────────────────────────────────────────────────────────────────────────
qt_add_executable(sentinel
    src/main.cpp
    ${UI_SOURCES}
    ${UI_HEADERS}
    resources/sentinel.qrc
)

target_include_directories(sentinel PRIVATE src)
target_compile_definitions(sentinel PRIVATE
    SENTINEL_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(sentinel PRIVATE
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::Charts Qt6::Sql
)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW only: must precede Qt to resolve __imp___argc
    target_link_libraries(sentinel PRIVATE mingw32)
endif()

if(MSVC)
    target_compile_options(sentinel_core PRIVATE /Zc:__cplusplus /permissive- /utf-8)
    target_compile_options(sentinel PRIVATE /Zc:__cplusplus /permissive- /utf-8)
endif()

if(WIN32)
    set_target_properties(sentinel PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install layout (used by CPack and packaging scripts)
#   Windows: <prefix>/sentinel.exe + Qt DLLs (windeployqt) + data/
#   Linux:   <prefix>/bin/sentinel + share/sentinel/data/
# ─────────────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS sentinel
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sentinel/data
    PATTERN "bulk" EXCLUDE
    PATTERN "scripts" EXCLUDE
    PATTERN ".gitignore" EXCLUDE
)

install(FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/docs/ANALYST_GUIDE.md
    ${CMAKE_SOURCE_DIR}/docs/REAL_DATA_EVALUATION.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

if(WIN32)
    get_filename_component(_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${_QT_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            set(_sentinel_exe \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/sentinel.exe\")
            if(NOT EXISTS \"\${_sentinel_exe}\")
                set(_sentinel_exe \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/sentinel.exe\")
            endif()
            if(NOT EXISTS \"\${_sentinel_exe}\")
                message(FATAL_ERROR \"sentinel.exe not found for windeployqt under \${CMAKE_INSTALL_PREFIX}\")
            endif()
            message(STATUS \"Running windeployqt on \${_sentinel_exe}\")
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                    --release --no-translations --compiler-runtime
                    \"\${_sentinel_exe}\"
                RESULT_VARIABLE _windeploy_result
            )
            if(NOT _windeploy_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed with exit code \${_windeploy_result}\")
            endif()
        ")
    else()
        message(WARNING \"windeployqt not found; CPack/NSIS packages may miss Qt DLLs\")
    endif()
endif()

if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/sentinel.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    if(EXISTS ${CMAKE_SOURCE_DIR}/packaging/icons/sentinel.svg)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/icons/sentinel.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# CPack (NSIS/ZIP on Windows, DEB/TGZ on Linux)
# ─────────────────────────────────────────────────────────────────────────────
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/CPackConfig.cmake")
include(CPack)

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
