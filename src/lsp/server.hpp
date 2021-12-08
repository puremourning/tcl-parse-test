#pragma once

#include <tcl.h>
#include <unordered_map>

#include <analyzer/index.cpp>

#include "types.cpp"


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

  struct Document
  {
    types::TextDocumentItem item;
    Parser::ParseContext context;
    Parser::Script script;
  };

  struct Server
  {
    WorkspaceOptions options;
    std::unordered_map< std::string, Document > documents;

    Index::Index index;

    std::string rootUri;
    ClientCapabilities clientCapabilities;

    size_t next_id;

    Tcl_Interp* interp;
  } server_; // TODO( just one for now )

  void initialise_server( char ** argv )
  {
    server_.index = Index::make_index();

    Tcl_FindExecutable( argv[ 0 ] );
    server_.interp = Tcl_CreateInterp();
  }

  void cleanup_server()
  {
    Tcl_DeleteInterp( server_.interp );
  }
}
