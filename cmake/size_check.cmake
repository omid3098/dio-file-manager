# Fails a Release or MinSizeRel build whose exe is over MAX_BYTES.
if(CONFIG STREQUAL "Release" OR CONFIG STREQUAL "MinSizeRel")
  file(SIZE "${EXE}" size)
  message(STATUS "dio-file-manager.exe is ${size} bytes (budget ${MAX_BYTES})")
  if(size GREATER MAX_BYTES)
    message(FATAL_ERROR "dio-file-manager.exe is ${size} bytes, over the budget of ${MAX_BYTES} bytes")
  endif()
endif()
