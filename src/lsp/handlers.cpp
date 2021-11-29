#include <asio/awaitable.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <json/json.hpp>

#include "comms.cpp"

namespace lsp::handlers
{
  asio::awaitable<void> on_initialize( asio::posix::stream_descriptor& out,
                                       json message )
  {
    json capabilities( json::value_t::object );
    capabilities[ "referencesProvider" ] = true;

    json response( json::value_t::object );
    response[ "capabilities" ] = capabilities;

    co_await send_reply( out, message[ "id" ], response );
  }
}
