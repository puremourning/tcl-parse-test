#pragma once

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <tcl.h>
#include <thread>
#include <unordered_map>
#include <shared_mutex>

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
    enum class State { OPEN, CLOSED } state = State::OPEN;
  };

  struct Server final
  {
    WorkspaceOptions options;
    std::unordered_map< std::string, Document > documents;

    std::shared_mutex index_lock;
    Index::Index index = Index::make_index();

    std::string rootUri;
    ClientCapabilities clientCapabilities;

    size_t next_id{0};

    Tcl_Interp* interp{nullptr};

    std::vector<std::thread> background_threads;
    asio::io_context background;
    asio::strand<asio::io_context::executor_type> index_queue =
      asio::make_strand(background.get_executor());

    Server( char** argv )
    {
      Tcl_FindExecutable( argv[ 0 ] );
      interp = Tcl_CreateInterp();

      for ( int i = 0; i < 4; ++i )
      {
        background_threads.emplace_back( [this]() {
          // prevent run from returning
          auto guard = asio::make_work_guard(background.get_executor());
          background.run();
        } );
      }
    }

    ~Server()
    {
      Tcl_DeleteInterp( interp );

      background.stop();

      for ( auto& t: background_threads )
      {
        if ( t.joinable() )
        {
          t.join();
        }
      }
    }
  };
}
