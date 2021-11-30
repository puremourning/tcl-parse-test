#pragma once

#include <optional>
#include <variant>
#include <string>
#include <vector>
#include <json/json.hpp>

namespace lsp::types
{
  // LSP 3.16.0

  using json = nlohmann::json;

  // Type defines {{{
  using integer = int64_t;
  using uinteger = uint64_t;
  using decimal = double;
  using number = double;
  using boolean = bool;
  using string = std::string;
  using object = nlohmann::json;
  using any = nlohmann::json;

  template<typename T=object> using array = std::vector<T>;

  using null = std::monostate;

  // common types that are repated
  using DocumentURI = string;
  using URI = string;

  template< typename... Ts > using one_of = std::variant< Ts... >;
  template< typename T > using nullable = one_of< null, T >;
  template< typename... Ts > using optional = std::optional< Ts... >;

  // }}}

  // Basic Structures {{{

  struct ResponseError
  {
    integer code;
    string message;
    json data;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( ResponseError,
                                    code,
                                    message,
                                    data );
  };

  // Text Document {{{

  struct Position
  {
    uinteger line;
    uinteger character;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( Position,
                                    line,
                                    character );
  };

  struct Range
  {
    Position start;
    Position end;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( Range,
                                    start,
                                    end );
  };

  struct Location
  {
    DocumentURI uri;
    Range range;
  };

  struct TextDocumentIdentifier
  {
    DocumentURI uri;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( TextDocumentIdentifier,
                                    uri );
  };

  struct TextDocumentItem
  {
    DocumentURI uri;
    string languageId;
    integer version;
    string text;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( TextDocumentItem,
                                    uri,
                                    languageId,
                                    version,
                                    text );
  };

  struct VersionedTextDocumentIdentifier : TextDocumentIdentifier
  {
    integer version;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( VersionedTextDocumentIdentifier,
                                    uri, // TODO(Ben) no inheritance here?
                                    version );
  };

  struct TextDocumentPositionParams
  {
    TextDocumentIdentifier textDocument;
    Position position;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( TextDocumentPositionParams,
                                    textDocument,
                                    position );
  };

  enum class TextDocumentSyncKind
  {
    None = 0,
    Full = 1,
    Incremental = 2,
  };

  // }}} Text Document Synchronization

  // }}} Basic Structures

}

#define LSP_FROM_JSON_OPTIONAL( j, o, v ) do {  \
    if ( j.contains( #v ) )                     \
    {                                           \
      j.at( #v ).get_to(o.v);                   \
    }                                           \
  } while( 0 )

#define LSP_FROM_JSON_STDOPTIONAL( j, o, v ) do {               \
    if ( j.contains( #v ) )                                     \
    {                                                           \
      o.v = j.at( #v ).get<decltype(o.v)::value_type>();        \
    }                                                           \
  } while( 0 )

#define LSP_FROM_JSON( j, o, v ) do {           \
    j.at( #v ).get_to( o.v );                   \
  } while ( 0 )

// vim: foldmethod=marker
