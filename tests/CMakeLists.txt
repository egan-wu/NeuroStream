function(ns_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE neurostream_core doctest::doctest)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ns_add_test(smoke_test)
ns_add_test(event_queue_test)
ns_add_test(config_test)
ns_add_test(trace_test)
ns_add_test(injector_test)
ns_add_test(scenario_test)
ns_add_test(scheduler_test)
ns_add_test(predictor_test)
ns_add_test(dma_test)
ns_add_test(intent_test)
ns_add_test(compression_test)
ns_add_test(npu_cache_test)
ns_add_test(phase8_integration_test)

target_compile_definitions(config_test PRIVATE
    NS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_compile_definitions(scenario_test PRIVATE
    NS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
