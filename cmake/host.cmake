# Reuse the vendored runtime and CMSIS-NN scalar kernels, replacing only
# hardware-specific logging, timing and LED output.
get_target_property(tflm_sources pico-tflmicro SOURCES)
list(REMOVE_ITEM tflm_sources
  "${PROJECT_SOURCE_DIR}/src/tensorflow/lite/micro/system_setup.cpp"
  "${PROJECT_SOURCE_DIR}/src/tensorflow/lite/micro/micro_time.cpp")
set_property(TARGET pico-tflmicro PROPERTY SOURCES "${tflm_sources}")
target_sources(pico-tflmicro PRIVATE
  "${PROJECT_SOURCE_DIR}/examples/hello_world/host/platform.cpp")

set(hello_world_dir "${PROJECT_SOURCE_DIR}/examples/hello_world")
add_executable(hello_world
  "${hello_world_dir}/constants.cpp"
  "${hello_world_dir}/hello_world_int8_model_data.cpp"
  "${hello_world_dir}/main_functions.cpp"
  "${hello_world_dir}/host/main.cpp"
  "${hello_world_dir}/host/output_handler.cpp")
target_include_directories(hello_world PRIVATE "${hello_world_dir}")
target_link_libraries(hello_world PRIVATE pico-tflmicro)

enable_testing()
add_executable(hello_world_test
  "${hello_world_dir}/hello_world_float_model_data.cpp"
  "${hello_world_dir}/hello_world_int8_model_data.cpp"
  "${hello_world_dir}/hello_world_test.cpp")
target_include_directories(hello_world_test PRIVATE "${hello_world_dir}")
target_link_libraries(hello_world_test PRIVATE pico-tflmicro)
add_test(NAME hello_world_models COMMAND hello_world_test)
add_test(NAME hello_world_demo COMMAND hello_world)

# Accepts a trained EMG .tflite and its verification.csv at runtime, so no
# generated weights or Python dependencies are needed to build this tool.
add_executable(emg_model_check
  "${PROJECT_SOURCE_DIR}/examples/emg_classifier/model_check.cpp")
target_link_libraries(emg_model_check PRIVATE pico-tflmicro)
