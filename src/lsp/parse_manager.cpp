#include "lsp/server.hpp"
#include "lsp/types.cpp"
#include <asio/awaitable.hpp>
#include <shared_mutex>

namespace lsp::parse_manager
{
  using server::Server;

  asio::awaitable<void> Reparse( Server& server, server::Document& doc )
  {
    // TODO(Ben): this is pretty horrific. Parser::SourceFile duplicates the
    // contents and much other badness. this is just for exploration.
    auto context = Parser::ParseContext{
      .file = Parser::make_source_file( doc.item.uri, doc.item.text ),
      .cur_ns = "",
    };

    auto script = Parser::ParseScript( server.interp,
                                       context,
                                       context.file.contents );

    // TODO: Index::make_temp_index( server.index ) (with read lock)
    //  that can then be merged with the main index via something like
    //  Index::merge( server.index, temp_index ) under the write lock
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
    std::shared_lock l(server.index_lock);
    auto& document = server.documents.at( pos.textDocument.uri );
    return Index::FindPositionInScript(
      document.script,
      { pos.position.line, pos.position.character } );
  }
}
