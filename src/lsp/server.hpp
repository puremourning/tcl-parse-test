#pragma once

#include "types.cpp"
#include "lsp_serialization.cpp"
#include <unordered_map>

namespace lsp::server
{
  namespace types = lsp::types;
  using json = nlohmann::json;

  struct WorkspaceOptions
  {
    std::vector< std::string > auto_path;

    friend void from_json( const json& j, WorkspaceOptions& o )
    {
      LSP_FROM_JSON_OPTIONAL(j, o, auto_path);
    }
  };

  struct ClientCapabilities
  {
  };

  struct Server
  {
    WorkspaceOptions options;
    std::unordered_map< std::string, types::TextDocumentItem > documents;

    std::string rootUri;
    ClientCapabilities clientCapabilities;

    size_t next_id;
  } server_; // TODO( just one for now )
}
