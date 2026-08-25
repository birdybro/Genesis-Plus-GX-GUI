function(genplusgx_enable_sanitizers target)
  if(MSVC)
    message(WARNING "The sanitizer preset is not supported by this MSVC configuration")
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      ${target}
      INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(${target} INTERFACE -fsanitize=address,undefined)
  else()
    message(WARNING "Sanitizers are not configured for ${CMAKE_CXX_COMPILER_ID}")
  endif()
endfunction()
