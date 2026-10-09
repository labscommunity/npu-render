cmake_minimum_required(VERSION 3.25)
project(npu_render_kernel LANGUAGES CXX)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The kernel laboratory requires x64 MSVC")
endif()
set(NR_WDK_ROOT "" CACHE PATH "Pinned WDK dependency root from prepare_wdk.py --kernel")
option(NR_KMD_LAB_SYNC_PROBE "Observe synchronization before rejecting lab adapter startup" OFF)
option(NR_KMD_LAB_WORKER_PROBE "Exercise an owned paging worker and deferred callback in the lab" OFF)
if(NOT EXISTS "${NR_WDK_ROOT}/lib/ntoskrnl.lib")
  message(FATAL_ERROR "Prepare WDK kernel headers and libraries first")
endif()
# This is a kernel image, not a Win32 application. No user-mode CRT/default libs.
set(CMAKE_CXX_STANDARD_LIBRARIES "")
set(CMAKE_CXX_FLAGS "/DWIN32 /D_WINDOWS /W4 /WX")
set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi")
set(CMAKE_EXE_LINKER_FLAGS "")
add_executable(nr_render_kmd driver.cpp)
set_target_properties(nr_render_kmd PROPERTIES SUFFIX ".sys")
target_compile_features(nr_render_kmd PRIVATE cxx_std_17)
target_compile_options(nr_render_kmd PRIVATE /kernel /GR- /EHs-c- /Zl /Z7)
target_include_directories(nr_render_kmd PRIVATE
  "${NR_WDK_ROOT}/include/km" "${NR_WDK_ROOT}/include/shared"
  "${NR_WDK_ROOT}/include/km/crt")
target_compile_definitions(nr_render_kmd PRIVATE
  NR_KMD_LAB_SYNC_PROBE=$<BOOL:${NR_KMD_LAB_SYNC_PROBE}>
  NR_KMD_LAB_WORKER_PROBE=$<BOOL:${NR_KMD_LAB_WORKER_PROBE}>
  _AMD64_ AMD64 _WIN64 NTDDI_VERSION=NTDDI_WIN10_VB
  DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM1_3)
target_link_options(nr_render_kmd PRIVATE /DRIVER /SUBSYSTEM:NATIVE
  /ENTRY:GsDriverEntry /NODEFAULTLIB /MANIFEST:NO /INTEGRITYCHECK /NXCOMPAT /DYNAMICBASE
  /DEBUG:FULL /OPT:REF /OPT:ICF)
target_link_libraries(nr_render_kmd PRIVATE
  "${NR_WDK_ROOT}/lib/ntoskrnl.lib" "${NR_WDK_ROOT}/lib/hal.lib"
  "${NR_WDK_ROOT}/lib/displib.lib" "${NR_WDK_ROOT}/lib/BufferOverflowK.lib")

add_executable(nr_kernel_host_test host_test.cpp host_main.cpp)
target_compile_features(nr_kernel_host_test PRIVATE cxx_std_17)
target_include_directories(nr_kernel_host_test PRIVATE
  "${NR_WDK_ROOT}/include/km" "${NR_WDK_ROOT}/include/shared")
target_compile_definitions(nr_kernel_host_test PRIVATE
  NR_KMD_LAB_SYNC_PROBE=$<BOOL:${NR_KMD_LAB_SYNC_PROBE}>
  NR_KMD_LAB_WORKER_PROBE=$<BOOL:${NR_KMD_LAB_WORKER_PROBE}>
  _AMD64_ AMD64 _WIN64 NTDDI_VERSION=NTDDI_WIN10_VB
  DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM1_3)
include(CTest)
add_test(NAME kernel_ddi_host_contract COMMAND nr_kernel_host_test)
