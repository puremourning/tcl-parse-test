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

    if ( message.contains( "initializationOptions" ) )
    {
      server::server_.options = message[ "initializationOptions" ];
    }

    std::cerr << "Options: " << server::server_.options.auto_path.size();

    co_await send_reply( out, message[ "id" ], response );
  }
}
