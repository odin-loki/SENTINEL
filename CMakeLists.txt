cmake_minimum_required(VERSION 3.27)
project(Sentinel VERSION 1.0.0 LANGUAGES CXX)

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
    src/core/Database.cpp
    src/core/SentinelLogger.cpp
    src/core/DataExporter.cpp
    src/audit/ProvenanceLog.cpp
    src/ingest/UKPoliceSource.cpp
    src/ingest/WeatherSource.cpp
    src/ingest/CsvImporter.cpp
    src/ingest/DataQualityScorer.cpp
    src/nlp/MOExtractor.cpp
    src/nlp/CrimeClassifier.cpp
    src/models/PoissonBaseline.cpp
    src/models/HawkesProcess.cpp
    src/models/SeriesDetector.cpp
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
)

set(CORE_HEADERS
    src/core/CrimeEvent.h
    src/core/AppConfig.h
    src/core/Database.h
    src/core/SentinelLogger.h
    src/core/DataExporter.h
    src/audit/ProvenanceLog.h
    src/ingest/DataSource.h
    src/ingest/UKPoliceSource.h
    src/ingest/WeatherSource.h
    src/ingest/CsvImporter.h
    src/ingest/DataQualityScorer.h
    src/ingest/DataQualityScorer.h
    src/nlp/MOExtractor.h
    src/nlp/CrimeClassifier.h
    src/models/PoissonBaseline.h
    src/models/HawkesProcess.h
    src/models/SeriesDetector.h
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
)

add_library(sentinel_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(sentinel_core PUBLIC src)
target_link_libraries(sentinel_core PUBLIC
    Qt6::Core Qt6::Network Qt6::Sql Qt6::Charts Qt6::Widgets
)

# ─────────────────────────────────────────────────────────────────────────────
# UI sources (depend on sentinel_core + Qt6::Widgets)
# ─────────────────────────────────────────────────────────────────────────────
set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/AuditLogWidget.cpp
    src/ui/DashboardWidget.cpp
    src/ui/MapWidget.cpp
    src/ui/EventsTableWidget.cpp
    src/ui/LeadsWidget.cpp
    src/ui/AnalyticsWidget.cpp
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
    src/ui/LeadsWidget.h
    src/ui/AnalyticsWidget.h
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
target_link_libraries(sentinel PRIVATE
    mingw32          # must precede Qt on MinGW to resolve __imp___argc
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::Charts Qt6::Sql
)

if(WIN32)
    set_target_properties(sentinel PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS sentinel RUNTIME DESTINATION bin BUNDLE DESTINATION .)

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
