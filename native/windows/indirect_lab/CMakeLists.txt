cmake_minimum_required(VERSION 3.25)
project(nr_indirect_lab LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT DEFINED NR_WDK_ROOT)
  message(FATAL_ERROR "Windows, MSVC and pinned UMDF dependencies required")
endif()
add_library(nr_indirect_lab SHARED driver.cpp)
target_compile_features(nr_indirect_lab PRIVATE cxx_std_17)
target_compile_options(nr_indirect_lab PRIVATE /W4 /WX)
target_include_directories(nr_indirect_lab PRIVATE
  "${NR_WDK_ROOT}/include/um" "${NR_WDK_ROOT}/include/shared"
  "${NR_WDK_ROOT}/umdf/include" "${NR_WDK_ROOT}/include/um/iddcx/1.4")
target_compile_definitions(nr_indirect_lab PRIVATE UNICODE _UNICODE UMDF_DRIVER UMDF_USING_NTSTATUS
  UMDF_VERSION_MAJOR=2 UMDF_VERSION_MINOR=25 IDDCX_VERSION_MAJOR=1 IDDCX_VERSION_MINOR=4)
target_link_libraries(nr_indirect_lab PRIVATE
  "${NR_WDK_ROOT}/umdf/lib/WdfDriverStubUm.lib" "${NR_WDK_ROOT}/iddcx/lib/iddcxstub.lib"
  ntdll dxgi d3d11 gdi32 ole32)
set_property(TARGET nr_indirect_lab PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_options(nr_indirect_lab PRIVATE /EXPORT:FxDriverEntryUm)
