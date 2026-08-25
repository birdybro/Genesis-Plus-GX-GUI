function(genplusgx_set_project_warnings target)
  if(MSVC)
    target_compile_options(${target} INTERFACE /W4 /permissive- /Zc:__cplusplus)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      ${target}
      INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
        $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic>
    )
  endif()
endfunction()
