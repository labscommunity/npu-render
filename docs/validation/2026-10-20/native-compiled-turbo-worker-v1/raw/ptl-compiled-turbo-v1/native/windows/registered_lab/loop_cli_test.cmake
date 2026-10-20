if(NOT DEFINED PROGRAM OR NOT EXISTS "${PROGRAM}")
  message(FATAL_ERROR "Loop test executable is required")
endif()

function(expect_rejected label)
  execute_process(COMMAND "${PROGRAM}" ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
  if(NOT "${result}" STREQUAL "1" OR NOT "${output}" STREQUAL ""
      OR NOT "${error}" MATCHES "^loop test: invalid loop test ")
    message(FATAL_ERROR "${label}: expected explicit rejection, got ${result}: ${output}${error}")
  endif()
endfunction()

# The flag-shaped invocation previously returned success without running a case.
expect_rejected(flag_backend --backend NPU)
expect_rejected(unknown_backend ZPU loops)
expect_rejected(unknown_section CPU unknown)
expect_rejected(extra_argument CPU all unexpected)
expect_rejected(unknown_option CPU raster --output unused.bin)
expect_rejected(unused_packet_output CPU arithmetic --emit-packet unused.bin)
