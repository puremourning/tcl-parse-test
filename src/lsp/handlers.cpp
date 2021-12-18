#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/use_awaitable.hpp>
#include <iostream>
#include <json/json.hpp>
#include <optional>

#include "comms.cpp"
#include "lsp/types.cpp"
#include "server.hpp"
#include "parse_manager.cpp"

#include <analyzer/source_location.cpp>
#include <analyzer/script.cpp>

namespace lsp::handlers
{
  using stream = asio::posix::stream_descriptor;
  using Server = lsp::server::Server;

  // General Messages {{{
  asio::awaitable<void> handle_initialize( Server& server,
                                           stream& out,
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
        },
        { "definitionProvider", true },
      } );

    const auto& params = message.value( "params", json::object() );

    server.options = params.value( "initializationOptions", json::object() );
    server.rootUri = params.value( "rootUri", "" );

    co_await send_reply( out, message[ "id" ], response );
  }

  // }}}

  // Workspace {{{

  void on_workspace_didchangeconfiguration( Server&,
                                            stream&,
                                            const json& )
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

  asio::awaitable<void> on_textdocument_didopen( Server& server,
                                                 stream&,
                                                 const json& message )
  {
    DidOpenTextDocumentParams params = message.at( "params" );

    auto [ pos, _ ] = server.documents.emplace(
      params.textDocument.uri,
      lsp::server::Document{ .item = params.textDocument } );

    co_await asio::co_spawn( server.index_queue,
                             lsp::parse_manager::Reparse( server, pos->second ),
                             asio::use_awaitable );
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

  asio::awaitable<void> on_textdocument_didchange( Server& server,
                                                   stream&,
                                                   const json& message )
  {
    DidChnageTextDocumentParams params = message.at( "params" );

    auto& document = server.documents.at( params.textDocument.uri );
    if ( document.item.version < params.textDocument.version &&
         params.contentChanges.size() == 1 )
    {
      document.item.text = params.contentChanges[ 0 ].text;
      document.item.version = params.textDocument.version;

      co_await asio::co_spawn( server.index_queue,
                               lsp::parse_manager::Reparse( server, document ),
                               asio::use_awaitable );
    }
    // else protocol error!
  }

  struct DidCloseTextDocumentParams
  {
    types::TextDocumentIdentifier textDocument;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( DidCloseTextDocumentParams,
                                    textDocument );
  };

  asio::awaitable<void> on_textdocument_didclose( Server& server,
                                                  stream&,
                                                  const json& message )
  {
    DidCloseTextDocumentParams params = message.at( "params" );

    auto coro = [&]() -> asio::awaitable<void> {
      server.documents.erase( params.textDocument.uri );
      co_return;
    };

    co_await asio::co_spawn( server.index_queue,
                             coro,
                             asio::use_awaitable );
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

  asio::awaitable<void> on_textdocument_references( Server& server,
                                                    stream& out,
                                                    const json& message )
  {
    ReferencesParams params = message.at( "params" );
    auto cursor = parse_manager::GetCursor( server, params );

    if ( !cursor.call || !cursor.word )
    {
      co_await send_reject( out, message[ "id" ], {
        .code = 101,
        .message = "Invalid position"
      } );
      co_return;
    }

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
          auto* ns = Index::FindNamespace( server.index, cursor.call->ns );
          if ( !ns )
          {
            break;
          }

          auto procs = Index::FindProc( server.index,
                                        *ns,
                                        cursor.word->text );
          auto* p = Index::BestFitProcToCall( procs, *cursor.call );

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

  using DefinitionParams = types::TextDocumentPositionParams;

  asio::awaitable<void> on_textdocument_definition( Server& server,
                                                    stream& out,
                                                    const json& message )
  {
    DefinitionParams params = message.at( "params" );
    auto cursor = parse_manager::GetCursor( server, params );

    if ( !cursor.call || !cursor.word )
    {
      co_await send_reject( out, message[ "id" ], {
        .code = 101,
        .message = "Invalid position"
      } );
      co_return;
    }

    auto response = json::array();

    // TODO/FIXME: Copy pasta above
    switch ( cursor.word->type )
    {
      case Parser::Word::Type::ARRAY_ACCESS:
        // find the array?
        break;

      case Parser::Word::Type::TEXT:
        if ( cursor.argument == 0 )
        {
          // It's a call, find the references!
          auto* ns = Index::FindNamespace( server.index, cursor.call->ns );
          if ( !ns )
          {
            break;
          }

          auto procs = Index::FindProc( server.index,
                                        *ns,
                                        cursor.word->text );
          auto* p = Index::BestFitProcToCall( procs, *cursor.call );

          if (!p)
          {
            break;
          }

          auto range = server.index.procs.refsByID.equal_range( p->id );
          for ( auto it = range.first; it != range.second; ++it )
          {
            auto& r = server.index.procs.references[ it->second ];
            if ( r->type != Index::ReferenceType::DEFINITION )
            {
              continue;
            }

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
