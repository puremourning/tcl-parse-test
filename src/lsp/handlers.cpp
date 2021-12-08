#include <asio/awaitable.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <iostream>
#include <json/json.hpp>
#include <optional>

#include "comms.cpp"
#include "server.hpp"
#include "parse_manager.cpp"

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

  // Workspace {{{

  void on_workspace_didchangeconfiguration( stream&, const json& )
  {
  }

  // }}}

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
    auto [ pos, _ ] = server::server_.documents.emplace(
      std::move( uri ),
      lsp::server::Document{ .item = std::move( params.textDocument ) } );

    /* co_await */ parse_manager::Reparse( pos->second );
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
    if ( document.item.version < params.textDocument.version &&
         params.contentChanges.size() == 1 )
    {
      document.item.text = params.contentChanges[ 0 ].text;
      document.item.version = params.textDocument.version;

      lsp::parse_manager::Reparse( document );
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

  // Language Features {{{

  struct ReferenceContext
  {
    types::boolean includeDeclaration;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( ReferenceContext,
                                    includeDeclaration );
  };

  struct ReferencesParams : types::TextDocumentPositionParams
  {
    ReferenceContext context;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( ReferencesParams,
                                    TextDocumentPositionParams_Items,
                                    context );
  };

  asio::awaitable<void> on_textdocument_references( stream& out,
                                                    const json& message )
  {
    ReferencesParams params = message.at( "params" );
    auto cursor = parse_manager::GetCursor( params );

    if ( !cursor.call || !cursor.word )
    {
      co_await send_reject( out, message[ "id" ], {
        .code = 101,
        .message = "Invalid position"
      } );
      co_return;
    }

    // TODO: This duplicates a lookup from GetCursor
    auto& server = server::server_;
    auto response = json::array();

    switch ( cursor.word->type )
    {
      case Parser::Word::Type::ARRAY_ACCESS:
        // find the array?
        break;

      case Parser::Word::Type::TEXT:
        if ( cursor.argument == 0 )
        {
          // It's a call, find the references!
          auto qn = Parser::SplitName( cursor.call->ns );
          auto& ns = Index::ResolveNamespace(
            server.index,
            qn,
            server.index.namespaces.Get( server.index.global_namespace_id ) );

          auto* p = Index::FindProc( server.index,
                                     ns,
                                     cursor.word->text );

          if (!p)
          {
            break;
          }

          auto range = server.index.procs.refsByID.equal_range( p->id );
          for ( auto it = range.first; it != range.second; ++it )
          {
            auto& r = server.index.procs.references[ it->second ];
            response.push_back( types::Location{
              .uri = r->location.sourceFile->fileName,
              .range = {
                .start = {
                  .line = r->location.line,
                  .character = r->location.column
                },
                .end = {
                  .line = r->location.line,
                  .character = r->location.column,
                }
              }
            } );
          }
        }
        break;

      case Parser::Word::Type::VARIABLE:
        // return the variable refs
        break;
    }

    co_await send_reply( out, message[ "id" ], response );
  }

  // }}}
}

// vim: foldmethod=marker
