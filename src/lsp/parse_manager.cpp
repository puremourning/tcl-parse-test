#include "lsp/server.hpp"
#include "lsp/types.cpp"
#include <asio/awaitable.hpp>
#include <shared_mutex>

namespace lsp::parse_manager
{
  using server::Server;

  void Clear( Server& server, const std::string& /*uri TODO*/ )
  {
    std::unique_lock l(server.index_lock);
    // TODO(Ben) WAAAAHAHAHAAHAHA
    server.index = Index::make_index();
  }

  asio::awaitable<void> Reparse( Server& server, server::Document& doc )
  {
    // FIXME we don't do this because we're replacing the index below
    // Clear( server, doc.item.uri ); // TOOD(Ben): Bad, slow, lame.

    // TODO(Ben): this is pretty horrific. Parser::SourceFile duplicates the
    // contents and much other badness. this is just for exploration.
    auto context = Parser::ParseContext{
      .file = Parser::make_source_file( doc.item.uri, doc.item.text ),
      .cur_ns = "",
    };

    auto script = Parser::ParseScript( server.interp,
                                       context,
                                       context.file.contents );

    auto index = Index::make_index();
    Index::ScanContext scanContext{
      .nsPath = { index.global_namespace_id }
    };
    Index::Build( index, scanContext, script );

    {
      std::unique_lock write_index(server.index_lock);
      doc.context = std::move( context );
      doc.script = std::move( script );
      server.index = std::move( index );
    }

    co_return;
  }

  auto GetCursor( Server& server,
                  const types::TextDocumentPositionParams pos )
  {
    // TODO: Check the URI!
    auto& document = server.documents.at( pos.textDocument.uri );
    return Index::FindPositionInScript(
      document.script,
      { pos.position.line, pos.position.character } );
  }
}
