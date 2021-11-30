#include <asio/awaitable.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <iostream>
#include <json/json.hpp>

#include "comms.cpp"
#include "server.hpp"

namespace lsp::handlers
{
  asio::awaitable<void> on_initialize( asio::posix::stream_descriptor& out,
                                       const json& message )
  {
    auto response = json::object();
    response[ "capabilities" ] = json::object( {
      { "referencesProvider", true },
    } );

    const auto& params = message.value( "params", json::object() );
    server::server_.options = params.value( "initializationOptions",
                                            json::object() );

    std::cerr << "Options: "
              << server::server_.options.auto_path.size()
              << std::endl;

    co_await send_reply( out, message[ "id" ], response );
  }
}
