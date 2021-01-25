add_executable( analyzer )

target_sources( analyzer
  PRIVATE
    analyzer.cpp
    script.cpp
    source_location.cpp
)

# This enables unity build (ish), by pretending that these source files are
# header files
set_source_files_properties(
  script.cpp
  source_location.cpp
  PROPERTIES
    HEADER_FILE_ONLY ON
)

if ( MSVC )
  target_compile_options( analyzer
    PRIVATE
      /W4 /WX
  )
else()
  target_compile_options( analyzer
    PRIVATE
      -Wall -Wextra -Werror
  )
endif()

target_include_directories( analyzer
  PUBLIC
    ${TCL_INCLUDE_PATH}
)
target_link_libraries( analyzer
  PUBLIC
    ${TCL_LIBRARY}
)
