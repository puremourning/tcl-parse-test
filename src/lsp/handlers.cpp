#include <asio/awaitable.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <iostream>
#include <json/json.hpp>
#include <optional>

#include "comms.cpp"
#include "server.hpp"

#include <analyzer/source_location.cpp>
#include <analyzer/script.cpp>

namespace lsp::handlers
{
  using stream = asio::posix::stream_descriptor;
  // General Messages {{{
  asio::awaitable<void> handle_initialize( stream& out,
                                           const json& message )
  {
    auto response = json::object();
    response[ "capabilities" ] = json::object(
      {
        { "referencesProvider", true },
        { "textDocumentSync", {
            { "openClose", true },
            { "change", types::TextDocumentSyncKind::Full },
          }
        }
      } );

    const auto& params = message.value( "params", json::object() );

    server::server_.options = params.value( "initializationOptions",
                                            json::object() );
    server::server_.rootUri = params.value( "rootUri", "" );

    co_await send_reply( out, message[ "id" ], response );
  }

  // }}}

  void on_workspace_didchangeconfiguration( stream&, const json& )
  {
  }

  // Text Synchronization {{{

  struct DidOpenTextDocumentParams
  {
    types::TextDocumentItem textDocument;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( DidOpenTextDocumentParams,
                                    textDocument );
  };

  void on_textdocument_didopen( stream&, const json& message )
  {
    DidOpenTextDocumentParams params = message.at( "params" );

    auto& server = server::server_;

    auto uri = params.textDocument.uri;

    // TODO(Ben): this is pretty horrific. Parser::SourceFile duplicates the
    // contents and much other badness. this is just for exploration.
    Parser::ParseContext context{
      Parser::make_source_file(
        uri,
        params.textDocument.text ) };

    auto script = Parser::ParseScript( server.interp,
                                       context,
                                       context.file.contents );

    auto& index = server.index;
    Index::ScanContext scanContext{
      .nsPath = { index.global_namespace_id }
    };
    Index::Build( index, scanContext, script );

    for ( auto& kv : index.namespaces.byName )
    {
      std::cerr << "Namespace: "
                << Index::GetPrintName( index,
                                        index.namespaces.Get( kv.second ) )
                << '\n';
    }

    for ( auto& kv : index.procs.byName )
    {
      std::cerr << "Proc: "
                << Index::GetPrintName( index, index.procs.Get( kv.second ) )
                << '\n';

      auto range = index.procs.refsByID.equal_range( kv.second );
      for ( auto it = range.first; it != range.second; ++it )
      {
        auto& r = index.procs.references[ it->second ];
        std::cerr << "  Ref: "
                  << Index::GetPrintName( index, index.procs.Get( r->id ) )
                  << " at " << r->location
                  << '\n';
      }
    }

    server::server_.documents.emplace( std::move( uri ),
                                       std::move( params.textDocument ) );
  }

  struct TextDocumentContentChangeEvent
  {
    std::optional< types::Range > range;
    types::string text;

    friend void from_json( const json& j, TextDocumentContentChangeEvent& o )
    {
      LSP_FROM_JSON_STDOPTIONAL(j, o, range);
      LSP_FROM_JSON(j, o, text);
    }
    // Not defined, but required for the std::vector<> imoplementation
    friend void to_json( json& j, const TextDocumentContentChangeEvent& o );
  };

  struct DidChnageTextDocumentParams
  {
    types::VersionedTextDocumentIdentifier textDocument;
    std::vector<TextDocumentContentChangeEvent> contentChanges;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( DidChnageTextDocumentParams,
                                    textDocument,
                                    contentChanges );
  };

  void on_textdocument_didchange( stream&, const json& message )
  {
    DidChnageTextDocumentParams params = message.at( "params" );

    auto& document = server::server_.documents.at( params.textDocument.uri );
    if ( document.version < params.textDocument.version &&
         params.contentChanges.size() == 1 )
    {
      document.text = params.contentChanges[ 0 ].text;
      document.version = params.textDocument.version;
    }
    // else protocol error!
  }

  struct DidCloseTextDocumentParams
  {
    types::TextDocumentIdentifier textDocument;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( DidCloseTextDocumentParams,
                                    textDocument );
  };

  void on_textdocument_didclose( stream&, const json& message )
  {
    DidCloseTextDocumentParams params = message.at( "params" );
    server::server_.documents.erase( params.textDocument.uri );
  }
  // }}}
}

// vim: foldmethod=marker
