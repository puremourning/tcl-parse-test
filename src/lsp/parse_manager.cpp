#include "lsp/server.hpp"
#include "lsp/types.cpp"
#include <asio/awaitable.hpp>

namespace lsp::parse_manager
{
  using server::Server;

  void Clear( Server& server, const std::string& /*uri TODO*/ )
  {
    // TODO(Ben) WAAAAHAHAHAAHAHA
    server.index = Index::make_index();
  }

  asio::awaitable<void> Reparse( Server& server, server::Document& doc )
  {
    Clear( server, doc.item.uri ); // TOOD(Ben): Bad, slow, lame.

    // TODO(Ben): this is pretty horrific. Parser::SourceFile duplicates the
    // contents and much other badness. this is just for exploration.
    doc.context = Parser::ParseContext{
      .file = Parser::make_source_file( doc.item.uri, doc.item.text ),
      .cur_ns = "",
    };

    doc.script = Parser::ParseScript( server.interp,
                                      doc.context,
                                      doc.context.file.contents );

    auto& index = server.index;
    Index::ScanContext scanContext{
      .nsPath = { index.global_namespace_id }
    };
    Index::Build( index, scanContext, doc.script );
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
