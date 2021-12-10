#include "lsp/server.hpp"
#include "lsp/types.cpp"

namespace lsp::parse_manager
{
  void Clear( const std::string& /*uri TODO*/ )
  {
    auto& server = server::server_;

    // TODO(Ben) WAAAAHAHAHAAHAHA
    server.index = Index::make_index();
  }

  void Reparse( server::Document& doc )
  {
    auto& server = server::server_;

    Clear( doc.item.uri ); // TOOD(Ben): Bad, slow, lame.

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
  }

  auto GetCursor( const types::TextDocumentPositionParams pos )
  {
    auto& server = server::server_;

    // TODO: Check the URI!
    auto& document = server.documents.at( pos.textDocument.uri );
    return Index::FindPositionInScript(
      document.script,
      { pos.position.line, pos.position.character } );
  }
}
