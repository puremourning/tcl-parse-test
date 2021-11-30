#pragma once

#include "types.cpp"

namespace lsp::server
{
  namespace types = lsp::types;

  struct WorkspaceOptions
  {
    std::vector< std::string > auto_path;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( WorkspaceOptions,
                                    auto_path );
  };

  struct Server
  {
    WorkspaceOptions options;
    size_t next_id;
  } server_; // TODO( just one for now )
}
