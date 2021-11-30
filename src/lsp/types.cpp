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

  // }}}
}

// vim: foldmethod=marker
