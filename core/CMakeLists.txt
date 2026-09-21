cmake_minimum_required(VERSION 3.20)

# Third party sources
set(MINIZ_SOURCES
    third_party/miniz/miniz.c
    third_party/miniz/miniz_zip.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
)

# Core sources
set(CORE_SOURCES
    src/bytecode/constant_pool.cpp
    src/bytecode/instructions.cpp
    src/bytecode/attributes.cpp
    src/bytecode/class_file.cpp
    src/jar/jar_archive.cpp
    src/config/obfuscator_config.cpp
    src/config/keep_rule.cpp
    src/reporter/ndjson_reporter.cpp
    src/reporter/console_reporter.cpp
    src/passes/pass.cpp
    src/passes/pass_pipeline.cpp
    src/passes/strip_debug_pass.cpp
    src/passes/rename_pass.cpp
    src/passes/string_encrypt_pass.cpp
    src/passes/control_flow_pass.cpp
    src/passes/indy_call_site_pass.cpp
    src/passes/reflection_proxy_pass.cpp
    src/passes/number_obfuscation_pass.cpp
    src/passes/access_reducer_pass.cpp
    src/passes/shrinker_pass.cpp
    src/passes/condy_pass.cpp
    src/passes/decompiler_deterrence_pass.cpp
    src/passes/class_encryption_pass.cpp
    src/passes/symbol_mapping.cpp
    src/engine/obfuscator_engine.cpp
    ${MINIZ_SOURCES}
)

# Core library
add_library(obfuscator_core STATIC
    ${CORE_SOURCES}
)

target_include_directories(obfuscator_core PUBLIC
    include
    third_party
    third_party/miniz
)

target_compile_features(obfuscator_core PUBLIC cxx_std_23)

# Standalone CLI executable
add_executable(obfuscator_cli
    src/main.cpp
)

target_link_libraries(obfuscator_cli PRIVATE
    obfuscator_core
)

target_compile_features(obfuscator_cli PRIVATE cxx_std_23)
