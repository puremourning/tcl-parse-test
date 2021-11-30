#pragma once

#include "types.cpp"
#include "lsp_serialization.cpp"

namespace lsp::server
{
  namespace types = lsp::types;
  using json = nlohmann::json;

  struct WorkspaceOptions
  {
    std::vector< std::string > auto_path;
  };

  void from_json( const json& j, WorkspaceOptions& o )
  {
    LSP_FROM_JSON_OPTIONAL(j, o, auto_path);
  }

  struct Server
  {
    WorkspaceOptions options;
    size_t next_id;
  } server_; // TODO( just one for now )
}
