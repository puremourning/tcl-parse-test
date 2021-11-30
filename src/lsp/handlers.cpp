#include <asio/awaitable.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <iostream>
#include <json/json.hpp>
#include <optional>

#include "comms.cpp"
#include "server.hpp"

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

    auto uri = params.textDocument.uri;
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
