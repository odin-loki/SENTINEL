cmake_minimum_required(VERSION 3.27)

find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)

# ─── Helper: add a test executable that links all sentinel source libs ───────
macro(sentinel_test TARGET)
    qt_add_executable(${TARGET} ${ARGN})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${TARGET} PRIVATE
        sentinel_core
        Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    set_tests_properties(${TARGET} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
    )
endmacro()

sentinel_test(test_core_nlp     test_core_nlp.cpp)
sentinel_test(test_models       test_models.cpp)
sentinel_test(test_inference    test_inference.cpp)
sentinel_test(test_integration  test_integration.cpp)
sentinel_test(test_quality_and_data test_quality_and_data.cpp)

# Copy test data to build dir so tests can locate it via applicationDirPath()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/london_crimes_2024.csv
               ${CMAKE_CURRENT_BINARY_DIR}/data/london_crimes_2024.csv
               COPYONLY)
sentinel_test(test_provenance   test_provenance.cpp)

# UI automation test — needs QApplication and the full UI source tree
qt_add_executable(test_ui_automation
    test_ui_automation.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AuditLogWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DashboardWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DebugConsoleWidget.cpp
    ${CMAKE_SOURCE_DIR}/resources/sentinel.qrc
)
target_include_directories(test_ui_automation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_ui_automation PRIVATE
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network
)
add_test(NAME test_ui_automation COMMAND test_ui_automation -platform offscreen)
set_tests_properties(test_ui_automation PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
    LABELS "ui"
)
sentinel_test(test_benchmark         test_benchmark.cpp)
sentinel_test(test_network_ensemble  test_network_ensemble.cpp)
sentinel_test(test_end_to_end        test_end_to_end.cpp)
sentinel_test(test_calibration       test_calibration.cpp)
sentinel_test(test_edge_cases        test_edge_cases.cpp)
qt_add_executable(test_performance test_performance.cpp)
target_include_directories(test_performance PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_performance PRIVATE
    sentinel_core Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets)
add_test(NAME test_performance COMMAND test_performance)
set_tests_properties(test_performance PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_kde_hotspot       test_kde_hotspot.cpp)
sentinel_test(test_risk_forecaster   test_risk_forecaster.cpp)
sentinel_test(test_data_exporter       test_data_exporter.cpp)
sentinel_test(test_weather_and_spec   test_weather_and_spec.cpp)
sentinel_test(test_series_detector    test_series_detector.cpp)
sentinel_test(test_gp_regression      test_gp_regression.cpp)
qt_add_executable(test_network_stress test_network_stress.cpp)
target_include_directories(test_network_stress PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_network_stress PRIVATE
    sentinel_core Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets)
add_test(NAME test_network_stress COMMAND test_network_stress)
set_tests_properties(test_network_stress PROPERTIES
    TIMEOUT 240
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_gp_ensemble_integration test_gp_ensemble_integration.cpp)
sentinel_test(test_nlp_pipeline       test_nlp_pipeline.cpp)
sentinel_test(test_bayesian_hierarchical test_bayesian_hierarchical.cpp)
sentinel_test(test_full_pipeline         test_full_pipeline.cpp)
sentinel_test(test_anomaly_geo          test_anomaly_geo.cpp)
sentinel_test(database_stress test_database_stress.cpp)
set_tests_properties(database_stress PROPERTIES TIMEOUT 60)
sentinel_test(test_mo_export_e2e       test_mo_export_e2e.cpp)
sentinel_test(test_csv_temporal_hint    test_csv_temporal_hint.cpp)
sentinel_test(test_data_ingestion       test_data_ingestion.cpp)
sentinel_test(test_cooffending_deep     test_cooffending_deep.cpp)
sentinel_test(test_gp_regression_enh    test_gp_regression_enhanced.cpp)
sentinel_test(test_mo_analyser_deep       test_mo_analyser_deep.cpp)
sentinel_test(test_export_roundtrip       test_export_roundtrip.cpp)
sentinel_test(test_evidence_calibration   test_evidence_calibration.cpp)
qt_add_executable(test_performance_suite test_performance_suite.cpp)
target_include_directories(test_performance_suite PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_performance_suite PRIVATE
    sentinel_core
    Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets
)
add_test(NAME test_performance_suite COMMAND test_performance_suite)
set_tests_properties(test_performance_suite PROPERTIES
    TIMEOUT 600
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_app_config              test_app_config.cpp)
sentinel_test(test_settings_persistence    test_settings_persistence.cpp)
set_tests_properties(test_settings_persistence PROPERTIES TIMEOUT 30)
sentinel_test(test_anomaly_detector_deep   test_anomaly_detector_deep.cpp)
sentinel_test(test_risk_forecaster_deep   test_risk_forecaster_deep.cpp)
sentinel_test(test_benchmark_deep         test_benchmark_deep.cpp)
sentinel_test(test_nlp_edge_cases         test_nlp_edge_cases.cpp)
sentinel_test(test_ensemble_calibration   test_ensemble_calibration.cpp)
sentinel_test(test_export_completeness    test_export_completeness.cpp)
sentinel_test(test_ensemble_ci            test_ensemble_ci.cpp)
sentinel_test(test_e2e_integration        test_e2e_integration.cpp)
sentinel_test(test_nlp_adversarial        test_nlp_adversarial.cpp)
sentinel_test(test_lead_report_generator  test_lead_report_generator.cpp)
sentinel_test(test_hawkes_convergence     test_hawkes_convergence.cpp)
sentinel_test(test_ensemble_ece          test_ensemble_ece.cpp)
sentinel_test(test_cooffending_network    test_cooffending_network.cpp)
sentinel_test(test_gp_regression_advanced    test_gp_regression_advanced.cpp)
sentinel_test(test_series_linkage_calibration test_series_linkage_calibration.cpp)
sentinel_test(test_benchmark_metrics_full test_benchmark_metrics_full.cpp)
sentinel_test(test_csv_edge_cases      test_csv_edge_cases.cpp)
sentinel_test(test_appconfig_roundtrip  test_appconfig_roundtrip.cpp)
sentinel_test(test_kde_geo_integration  test_kde_geo_integration.cpp)
sentinel_test(test_full_system_integration test_full_system_integration.cpp)
set_tests_properties(test_full_system_integration PROPERTIES TIMEOUT 180)

sentinel_test(full_pipeline_integration test_full_pipeline_integration.cpp)
set_tests_properties(full_pipeline_integration PROPERTIES TIMEOUT 60)
sentinel_test(test_data_exporter_roundtrip       test_data_exporter_roundtrip.cpp)
sentinel_test(test_bayesian_hierarchical_advanced test_bayesian_hierarchical_advanced.cpp)
sentinel_test(test_kde_hotspot_advanced           test_kde_hotspot_advanced.cpp)
sentinel_test(test_evidence_scorer_comprehensive test_evidence_scorer_comprehensive.cpp)
sentinel_test(test_anomaly_accuracy             test_anomaly_accuracy.cpp)
sentinel_test(test_missing_coverage             test_missing_coverage.cpp)
sentinel_test(kde_temporal_cooffending          test_kde_temporal_cooffending.cpp)

sentinel_test(test_database_wal             test_database_wal.cpp)
set_tests_properties(test_database_wal PROPERTIES TIMEOUT 30)

sentinel_test(test_csv_importer_roundtrip   test_csv_importer_roundtrip.cpp)
set_tests_properties(test_csv_importer_roundtrip PROPERTIES TIMEOUT 60)

sentinel_test(test_data_exporter_html test_data_exporter_html.cpp)
set_tests_properties(test_data_exporter_html PROPERTIES TIMEOUT 30)

sentinel_test(test_nlp_murder_sexual            test_nlp_murder_sexual.cpp)
sentinel_test(nlp_comprehensive                test_nlp_comprehensive.cpp)
set_tests_properties(nlp_comprehensive PROPERTIES TIMEOUT 30)
sentinel_test(test_mo_analyser_accuracy         test_mo_analyser_accuracy.cpp)
sentinel_test(test_geographic_profiler_accuracy test_geographic_profiler_accuracy.cpp)
set_tests_properties(test_geographic_profiler_accuracy PROPERTIES TIMEOUT 30)

sentinel_test(test_cooffending_pagerank         test_cooffending_pagerank.cpp)
set_tests_properties(test_cooffending_pagerank PROPERTIES TIMEOUT 30)

sentinel_test(test_hint_engine_dedup            test_hint_engine_dedup.cpp)
set_tests_properties(test_hint_engine_dedup PROPERTIES TIMEOUT 30)

sentinel_test(test_risk_forecaster_horizon      test_risk_forecaster_horizon.cpp)
set_tests_properties(test_risk_forecaster_horizon PROPERTIES TIMEOUT 60)

sentinel_test(test_series_detector_accuracy     test_series_detector_accuracy.cpp)
set_tests_properties(test_series_detector_accuracy PROPERTIES TIMEOUT 30)

sentinel_test(test_lead_report_quality                    test_lead_report_quality.cpp)
set_tests_properties(test_lead_report_quality PROPERTIES TIMEOUT 30)

sentinel_test(test_bayesian_hierarchical_convergence      test_bayesian_hierarchical_convergence.cpp)
set_tests_properties(test_bayesian_hierarchical_convergence PROPERTIES TIMEOUT 30)

sentinel_test(test_benchmark_pai_pei                      test_benchmark_pai_pei.cpp)
set_tests_properties(test_benchmark_pai_pei PROPERTIES TIMEOUT 30)

sentinel_test(test_bias_auditor_comprehensive              test_bias_auditor_comprehensive.cpp)
set_tests_properties(test_bias_auditor_comprehensive PROPERTIES TIMEOUT 30)

sentinel_test(test_nlp_pipeline_integration               test_nlp_pipeline_integration.cpp)
set_tests_properties(test_nlp_pipeline_integration PROPERTIES TIMEOUT 30)

sentinel_test(test_kde_hotspot_accuracy                   test_kde_hotspot_accuracy.cpp)
set_tests_properties(test_kde_hotspot_accuracy PROPERTIES TIMEOUT 30)

sentinel_test(test_calibration_ece                        test_calibration_ece.cpp)
set_tests_properties(test_calibration_ece PROPERTIES TIMEOUT 30)

sentinel_test(test_gp_regression_intervals                test_gp_regression_intervals.cpp)
set_tests_properties(test_gp_regression_intervals PROPERTIES TIMEOUT 60)

sentinel_test(test_hawkes_temporal_decay                  test_hawkes_temporal_decay.cpp)
set_tests_properties(test_hawkes_temporal_decay PROPERTIES TIMEOUT 60)

sentinel_test(test_poisson_baseline_deep                  test_poisson_baseline_deep.cpp)
set_tests_properties(test_poisson_baseline_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_evidence_scorer_deep                   test_evidence_scorer_deep.cpp)
set_tests_properties(test_evidence_scorer_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_provenance_chain                       test_provenance_chain.cpp)
set_tests_properties(test_provenance_chain PROPERTIES TIMEOUT 30)

sentinel_test(test_anomaly_detector_accuracy              test_anomaly_detector_accuracy.cpp)
set_tests_properties(test_anomaly_detector_accuracy PROPERTIES TIMEOUT 30)

sentinel_test(test_csv_importer_deep                      test_csv_importer_deep.cpp)
set_tests_properties(test_csv_importer_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_temporal_features_deep                 test_temporal_features_deep.cpp)
set_tests_properties(test_temporal_features_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_ensemble_weights                       test_ensemble_weights.cpp)
set_tests_properties(test_ensemble_weights PROPERTIES TIMEOUT 30)

sentinel_test(test_weather_source_logic                   test_weather_source_logic.cpp)
set_tests_properties(test_weather_source_logic PROPERTIES TIMEOUT 30)

sentinel_test(test_uk_police_source_parsing               test_uk_police_source_parsing.cpp)
set_tests_properties(test_uk_police_source_parsing PROPERTIES TIMEOUT 30)

sentinel_test(test_sentinel_logger_deep                   test_sentinel_logger_deep.cpp)
set_tests_properties(test_sentinel_logger_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_cooffending_analyser                   test_cooffending_analyser.cpp)
set_tests_properties(test_cooffending_analyser PROPERTIES TIMEOUT 30)

sentinel_test(test_database_deep                          test_database_deep.cpp)
set_tests_properties(test_database_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_crime_classifier_deep                  test_crime_classifier_deep.cpp)
set_tests_properties(test_crime_classifier_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_mo_extractor_deep                      test_mo_extractor_deep.cpp)
set_tests_properties(test_mo_extractor_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_near_repeat_victimisation              test_near_repeat_victimisation.cpp)
set_tests_properties(test_near_repeat_victimisation PROPERTIES TIMEOUT 30)

sentinel_test(series_near_repeat test_series_near_repeat.cpp)
set_tests_properties(series_near_repeat PROPERTIES TIMEOUT 30)

sentinel_test(test_bias_auditor_advanced                  test_bias_auditor_advanced.cpp)
set_tests_properties(test_bias_auditor_advanced PROPERTIES TIMEOUT 30)

sentinel_test(test_appconfig_edge_cases                   test_appconfig_edge_cases.cpp)
set_tests_properties(test_appconfig_edge_cases PROPERTIES TIMEOUT 30)

sentinel_test(test_benchmark_full_report                  test_benchmark_full_report.cpp)
set_tests_properties(test_benchmark_full_report PROPERTIES TIMEOUT 30)

sentinel_test(test_crime_event_serialization              test_crime_event_serialization.cpp)
set_tests_properties(test_crime_event_serialization PROPERTIES TIMEOUT 30)

sentinel_test(test_hawkes_advanced                        test_hawkes_advanced.cpp)
set_tests_properties(test_hawkes_advanced PROPERTIES TIMEOUT 30)

sentinel_test(test_provenance_log_deep                    test_provenance_log_deep.cpp)
set_tests_properties(test_provenance_log_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_anomaly_detector_advanced              test_anomaly_detector_advanced.cpp)
set_tests_properties(test_anomaly_detector_advanced PROPERTIES TIMEOUT 30)

sentinel_test(test_mo_analyser_tfidf_deep                 test_mo_analyser_tfidf_deep.cpp)
set_tests_properties(test_mo_analyser_tfidf_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_data_exporter_formats                  test_data_exporter_formats.cpp)
set_tests_properties(test_data_exporter_formats PROPERTIES TIMEOUT 30)

sentinel_test(test_calibration_analyser_deep              test_calibration_analyser_deep.cpp)
set_tests_properties(test_calibration_analyser_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_poisson_statistical                    test_poisson_statistical.cpp)
set_tests_properties(test_poisson_statistical PROPERTIES TIMEOUT 30)

sentinel_test(test_hint_engine_advanced                   test_hint_engine_advanced.cpp)
set_tests_properties(test_hint_engine_advanced PROPERTIES TIMEOUT 30)

sentinel_test(test_data_source_interface                  test_data_source_interface.cpp)
set_tests_properties(test_data_source_interface PROPERTIES TIMEOUT 30)

sentinel_test(test_series_detector_linkage                test_series_detector_linkage.cpp)
set_tests_properties(test_series_detector_linkage PROPERTIES TIMEOUT 30)

sentinel_test(test_bayesian_posterior                     test_bayesian_posterior.cpp)
set_tests_properties(test_bayesian_posterior PROPERTIES TIMEOUT 30)

sentinel_test(test_csv_importer_edge                      test_csv_importer_edge.cpp)
set_tests_properties(test_csv_importer_edge PROPERTIES TIMEOUT 30)

sentinel_test(test_gp_regression_grid                     test_gp_regression_grid.cpp)
set_tests_properties(test_gp_regression_grid PROPERTIES TIMEOUT 30)

sentinel_test(test_temporal_features_edge                 test_temporal_features_edge.cpp)
set_tests_properties(test_temporal_features_edge PROPERTIES TIMEOUT 30)

sentinel_test(test_benchmark_metrics_edge                 test_benchmark_metrics_edge.cpp)
set_tests_properties(test_benchmark_metrics_edge PROPERTIES TIMEOUT 30)

sentinel_test(test_weather_source_advanced                test_weather_source_advanced.cpp)
set_tests_properties(test_weather_source_advanced PROPERTIES TIMEOUT 30)

sentinel_test(test_data_quality_boundary                  test_data_quality_boundary.cpp)
set_tests_properties(test_data_quality_boundary PROPERTIES TIMEOUT 30)

sentinel_test(test_sentinel_logger_filter                 test_sentinel_logger_filter.cpp)
set_tests_properties(test_sentinel_logger_filter PROPERTIES TIMEOUT 30)

sentinel_test(test_risk_forecaster_zones                  test_risk_forecaster_zones.cpp)
set_tests_properties(test_risk_forecaster_zones PROPERTIES TIMEOUT 30)

sentinel_test(test_mo_extractor_complex                   test_mo_extractor_complex.cpp)
set_tests_properties(test_mo_extractor_complex PROPERTIES TIMEOUT 30)

sentinel_test(test_database_migration_deep                test_database_migration_deep.cpp)
set_tests_properties(test_database_migration_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_geographic_profiler_multisite          test_geographic_profiler_multisite.cpp)
set_tests_properties(test_geographic_profiler_multisite PROPERTIES TIMEOUT 30)

sentinel_test(test_crime_classifier_multilabel            test_crime_classifier_multilabel.cpp)
set_tests_properties(test_crime_classifier_multilabel PROPERTIES TIMEOUT 30)

sentinel_test(test_uk_police_source_deep                  test_uk_police_source_deep.cpp)
set_tests_properties(test_uk_police_source_deep PROPERTIES TIMEOUT 30)

sentinel_test(ingest_comprehensive                        test_ingest_comprehensive.cpp)
set_tests_properties(ingest_comprehensive PROPERTIES TIMEOUT 30)

sentinel_test(test_anomaly_edge_cases                     test_anomaly_edge_cases.cpp)
set_tests_properties(test_anomaly_edge_cases PROPERTIES TIMEOUT 30)

sentinel_test(inference_comprehensive                     test_inference_comprehensive.cpp)
set_tests_properties(inference_comprehensive PROPERTIES TIMEOUT 30)


sentinel_test(test_csv_real_data  test_csv_real_data.cpp)
set_tests_properties(test_csv_real_data PROPERTIES TIMEOUT 60)

# ── Provenance audit focused tests ───────────────────────────────────────────
sentinel_test(test_provenance_audit  test_provenance_audit.cpp)

# ── LeadsWidget UI interaction tests — needs LeadsWidget source directly ─────
qt_add_executable(test_leads_widget_interaction
    test_leads_widget_interaction.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
)
target_include_directories(test_leads_widget_interaction PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_leads_widget_interaction PRIVATE
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network
)
add_test(NAME test_leads_widget_interaction COMMAND test_leads_widget_interaction)
set_tests_properties(test_leads_widget_interaction PROPERTIES
    TIMEOUT 120
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_all_models_perf     test_all_models_perf.cpp)
set_tests_properties(test_all_models_perf PROPERTIES TIMEOUT 120)
sentinel_test(test_database_migration  test_database_migration.cpp)
sentinel_test(test_network_failures    test_network_failures.cpp)

# AuditLogWidget UI test — needs QApplication and the AuditLogWidget source
qt_add_executable(test_audit_log_widget
    test_audit_log_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AuditLogWidget.cpp
)
target_include_directories(test_audit_log_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_audit_log_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_audit_log_widget COMMAND test_audit_log_widget)
set_tests_properties(test_audit_log_widget PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_database_integration  test_database_integration.cpp)
sentinel_test(test_hint_quality_mrr      test_hint_quality_mrr.cpp)
sentinel_test(test_source_parsing        test_source_parsing.cpp)
sentinel_test(test_csv_importer          test_csv_importer.cpp)
sentinel_test(test_temporal_features     test_temporal_features.cpp)
sentinel_test(test_series_detector_deep   test_series_detector_deep.cpp)
sentinel_test(test_poisson_overdispersion test_poisson_overdispersion.cpp)
sentinel_test(test_temporal_features_advanced test_temporal_features_advanced.cpp)
sentinel_test(test_sentinel_logger_advanced   test_sentinel_logger_advanced.cpp)

sentinel_test(test_advanced_pipeline_integration test_advanced_pipeline_integration.cpp)
set_tests_properties(test_advanced_pipeline_integration PROPERTIES TIMEOUT 30)
target_link_libraries(test_advanced_pipeline_integration PRIVATE Qt6::Concurrent)
sentinel_test(test_hint_engine_lead_quality test_hint_engine_lead_quality.cpp)

# Deep UI widget tests — needs QApplication and individual widget sources
qt_add_executable(test_ui_widgets_deep
    test_ui_widgets_deep.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
)
target_include_directories(test_ui_widgets_deep PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ui_widgets_deep PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_ui_widgets_deep COMMAND test_ui_widgets_deep)
set_tests_properties(test_ui_widgets_deep PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
sentinel_test(test_bias_auditor_deep      test_bias_auditor_deep.cpp)

sentinel_test(test_mo_analyser_tfidf             test_mo_analyser_tfidf.cpp)
set_tests_properties(test_mo_analyser_tfidf PROPERTIES TIMEOUT 30)

sentinel_test(test_pipeline_integration_deep  test_pipeline_integration_deep.cpp)
set_tests_properties(test_pipeline_integration_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_data_quality_deep      test_data_quality_deep.cpp)
sentinel_test(test_kde_hotspot_deep       test_kde_hotspot_deep.cpp)
sentinel_test(test_sentinel_logger        test_sentinel_logger.cpp)
qt_add_executable(test_pipeline_stress test_pipeline_stress.cpp)
target_include_directories(test_pipeline_stress PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_pipeline_stress PRIVATE
    sentinel_core Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets)
add_test(NAME test_pipeline_stress COMMAND test_pipeline_stress)
set_tests_properties(test_pipeline_stress PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
qt_add_executable(test_heatmap_widget
    test_heatmap_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
)
target_include_directories(test_heatmap_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_heatmap_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_heatmap_widget COMMAND test_heatmap_widget)
set_tests_properties(test_heatmap_widget PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)
# Map and Analytics deep UI tests
qt_add_executable(test_ui_map_analytics
    test_ui_map_analytics.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
)
target_include_directories(test_ui_map_analytics PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ui_map_analytics PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_ui_map_analytics COMMAND test_ui_map_analytics)
set_tests_properties(test_ui_map_analytics PROPERTIES
    TIMEOUT 120
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

sentinel_test(test_gp_regression_full    test_gp_regression_full.cpp)
sentinel_test(test_mo_performance        test_mo_performance.cpp)
sentinel_test(test_co_offending_analyser  test_co_offending_analyser.cpp)
sentinel_test(test_evidence_scorer        test_evidence_scorer.cpp)
sentinel_test(test_hint_engine_deep       test_hint_engine_deep.cpp)
sentinel_test(hint_anomaly_evidence       test_hint_anomaly_evidence.cpp)
sentinel_test(test_risk_forecaster_bounds  test_risk_forecaster_bounds.cpp)
sentinel_test(test_database_concurrent     test_database_concurrent.cpp)
sentinel_test(test_geographic_profiler_deep test_geographic_profiler_deep.cpp)
sentinel_test(geographic_mo_evidence test_geographic_mo_evidence.cpp)
qt_add_executable(test_performance_benchmark test_performance_benchmark.cpp)
target_include_directories(test_performance_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_performance_benchmark PRIVATE
    sentinel_core Qt6::Core Qt6::Test Qt6::Sql Qt6::Network Qt6::Charts Qt6::Widgets)
add_test(NAME test_performance_benchmark COMMAND test_performance_benchmark)
set_tests_properties(test_performance_benchmark PROPERTIES TIMEOUT 120
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}")

# ── RiskForecaster alert & horizon tests ─────────────────────────────────────
sentinel_test(test_risk_forecaster_alerts  test_risk_forecaster_alerts.cpp)
set_tests_properties(test_risk_forecaster_alerts PROPERTIES TIMEOUT 60)

sentinel_test(test_statistical_pipeline   test_statistical_pipeline.cpp)
set_tests_properties(test_statistical_pipeline PROPERTIES TIMEOUT 60)

sentinel_test(test_provenance_stress      test_provenance_stress.cpp)
set_tests_properties(test_provenance_stress PROPERTIES TIMEOUT 30)

sentinel_test(test_model_accuracy_validation test_model_accuracy_validation.cpp)
sentinel_test(sla_accuracy                   test_sla_accuracy.cpp)
set_tests_properties(sla_accuracy PROPERTIES TIMEOUT 120)
sentinel_test(test_forecaster_accuracy       test_forecaster_accuracy.cpp)

sentinel_test(test_calibration_bias_improvements test_calibration_bias_improvements.cpp)
set_tests_properties(test_calibration_bias_improvements PROPERTIES TIMEOUT 30)

sentinel_test(benchmark_calibration_deep test_benchmark_calibration_deep.cpp)
set_tests_properties(benchmark_calibration_deep PROPERTIES TIMEOUT 30)

sentinel_test(test_ensemble_predictor_deep  test_ensemble_predictor_deep.cpp)
set_tests_properties(test_ensemble_predictor_deep PROPERTIES TIMEOUT 60)

sentinel_test(models_edge_cases test_models_edge_cases.cpp)
set_tests_properties(models_edge_cases PROPERTIES TIMEOUT 60)

sentinel_test(hawkes_ensemble_deep test_hawkes_ensemble_deep.cpp)
set_tests_properties(hawkes_ensemble_deep PROPERTIES TIMEOUT 60)

sentinel_test(test_gp_regression_deep2      test_gp_regression_deep2.cpp)
set_tests_properties(test_gp_regression_deep2 PROPERTIES TIMEOUT 60)

sentinel_test(gp_bayesian_deep              test_gp_bayesian_deep.cpp)
set_tests_properties(gp_bayesian_deep PROPERTIES TIMEOUT 60)

# ── TemporalHeatmapWidget data-model tests ────────────────────────────────────
qt_add_executable(test_temporal_heatmap_data
    test_temporal_heatmap_data.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
)
target_include_directories(test_temporal_heatmap_data PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_temporal_heatmap_data PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_temporal_heatmap_data
    COMMAND test_temporal_heatmap_data -platform offscreen)
set_tests_properties(test_temporal_heatmap_data PROPERTIES
    TIMEOUT 60
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── AnalyticsWidget data-model tests ──────────────────────────────────────────
qt_add_executable(test_analytics_widget_data
    test_analytics_widget_data.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
)
target_include_directories(test_analytics_widget_data PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_analytics_widget_data PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_analytics_widget_data
    COMMAND test_analytics_widget_data -platform offscreen)
set_tests_properties(test_analytics_widget_data PROPERTIES
    TIMEOUT 60
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── MainWindow integration test — needs QApplication + full UI source tree ───
qt_add_executable(test_main_window_integration
    test_main_window_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AuditLogWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DashboardWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DebugConsoleWidget.cpp
    ${CMAKE_SOURCE_DIR}/resources/sentinel.qrc
)
target_include_directories(test_main_window_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_main_window_integration PRIVATE
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network
)
add_test(NAME test_main_window_integration
    COMMAND test_main_window_integration -platform offscreen)
set_tests_properties(test_main_window_integration PROPERTIES
    TIMEOUT 120
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── EventsTableWidget focused tests ──────────────────────────────────────────
qt_add_executable(test_events_table_widget
    test_events_table_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
)
target_include_directories(test_events_table_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_events_table_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_events_table_widget COMMAND test_events_table_widget)
set_tests_properties(test_events_table_widget PROPERTIES
    TIMEOUT 60 LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── DebugConsoleWidget focused tests ─────────────────────────────────────────
qt_add_executable(test_debug_console_widget
    test_debug_console_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DebugConsoleWidget.cpp
)
target_include_directories(test_debug_console_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_debug_console_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_debug_console_widget COMMAND test_debug_console_widget)
set_tests_properties(test_debug_console_widget PROPERTIES
    TIMEOUT 60 LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── MapWidget focused tests ───────────────────────────────────────────────────
qt_add_executable(test_map_widget
    test_map_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
)
target_include_directories(test_map_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_map_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_map_widget COMMAND test_map_widget)
set_tests_properties(test_map_widget PROPERTIES
    TIMEOUT 60 LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── DashboardWidget focused tests ─────────────────────────────────────────────
qt_add_executable(test_dashboard_widget
    test_dashboard_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DashboardWidget.cpp
)
target_include_directories(test_dashboard_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dashboard_widget PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_dashboard_widget COMMAND test_dashboard_widget)
set_tests_properties(test_dashboard_widget PROPERTIES
    TIMEOUT 60 LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── SettingsWidget headless tests ─────────────────────────────────────────────
qt_add_executable(test_settings_widget_headless
    test_settings_widget_headless.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsWidget.cpp
)
target_include_directories(test_settings_widget_headless PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_settings_widget_headless PRIVATE
    sentinel_core Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network)
add_test(NAME test_settings_widget_headless COMMAND test_settings_widget_headless)
set_tests_properties(test_settings_widget_headless PROPERTIES
    TIMEOUT 60 LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

sentinel_test(test_pipeline_e2e_stress      test_pipeline_e2e_stress.cpp)
set_tests_properties(test_pipeline_e2e_stress PROPERTIES TIMEOUT 120)

sentinel_test(database_performance test_database_performance.cpp)
set_tests_properties(database_performance PROPERTIES TIMEOUT 120)

sentinel_test(appconfig_database_deep test_appconfig_database_deep.cpp)
target_sources(appconfig_database_deep PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsWidget.cpp
)
set_tests_properties(appconfig_database_deep PROPERTIES
    TIMEOUT 60
    LABELS "ui"
    ENVIRONMENT "SENTINEL_HEADLESS_TEST=1;PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── MainWindow completeness test — needs QApplication + full UI source tree ──
qt_add_executable(main_window_completeness
    test_main_window_completeness.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AuditLogWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DashboardWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DebugConsoleWidget.cpp
    ${CMAKE_SOURCE_DIR}/resources/sentinel.qrc
)
target_include_directories(main_window_completeness PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(main_window_completeness PRIVATE
    sentinel_core
    Qt6::Core Qt6::Widgets Qt6::Test Qt6::Charts Qt6::Sql Qt6::Network
)
add_test(NAME main_window_completeness
    COMMAND main_window_completeness -platform offscreen)
set_tests_properties(main_window_completeness PROPERTIES
    TIMEOUT 120
    LABELS "ui"
    ENVIRONMENT "PATH=C:/Qt/6.11.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
)

# ── Comprehensive UI widget tests (iteration 4) ───────────────────────────────
sentinel_test(ui_comprehensive
    test_ui_comprehensive.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DashboardWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalyticsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EventsTableWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LeadsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DebugConsoleWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
)
set_tests_properties(ui_comprehensive PROPERTIES LABELS "ui")

# ── Map / Audit / Heatmap widget audit tests (iteration 5) ─────────────────
sentinel_test(ui_map_audit_heatmap
    test_ui_map_audit_heatmap.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AuditLogWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TemporalHeatmapWidget.cpp
)
set_tests_properties(ui_map_audit_heatmap PROPERTIES LABELS "ui")

sentinel_test(nearrepeat_csv_mo test_nearrepeat_csv_mo.cpp)
set_tests_properties(nearrepeat_csv_mo PROPERTIES TIMEOUT 30)

sentinel_test(provenance_quality_nlp test_provenance_quality_nlp.cpp)
set_tests_properties(provenance_quality_nlp PROPERTIES TIMEOUT 30)
