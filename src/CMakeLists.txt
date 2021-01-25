add_executable( analyzer )

target_sources( analyzer
  PRIVATE
    analyzer.cpp
    script.cpp
    source_location.cpp
)

target_include_directories( analyzer
  PUBLIC
    ${TCL_INCLUDE_PATH}
)

target_link_libraries( analyzer
  PUBLIC
    ${TCL_LIBRARY}
)

target_compile_options( analyzer
  PRIVATE
    -Wall -Wextra -Werror
)

set_source_files_properties(
  script.cpp
  source_location.cpp
  PROPERTIES
    HEADER_FILE_ONLY ON
)
