#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/buffers_iterator.hpp>
#include <asio/co_spawn.hpp>
#include <asio/completion_condition.hpp>
#include <asio/coroutine.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/use_coro.hpp>
#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/streambuf.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/as_tuple.hpp>
#include <asio/experimental/coro.hpp>
#include <asio.hpp>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <tclInt.h>
#include <unistd.h>
#include <unordered_map>
#include <algorithm>
#include <span>
#include <json/json.hpp>

#include "lsp/comms.cpp"
#include "lsp/handlers.cpp"

namespace lsp::server
{
  asio::awaitable<void> handle_test( asio::posix::stream_descriptor& out,
                                     json message )
  {
    json response;
    response[ "method" ] = "reply";
    response[ "value" ] = message[ "input" ].get<double>() * 100;

    co_await lsp::send_message( out, response );
  }

  asio::awaitable<void> dispatch_messages()
  {
    std::cerr << "dispatch_messages starting up" << std::endl;

    asio::posix::stream_descriptor in(co_await asio::this_coro::executor,
                                      ::dup(STDIN_FILENO));
    asio::posix::stream_descriptor out(co_await asio::this_coro::executor,
                                      ::dup(STDOUT_FILENO));

    auto r = lsp::read_message( std::move(in) );
    for( ;; )
    {
      auto [ ec, message_ ] = co_await r.async_resume(
        use_nothrow_awaitable );

      if ( ec || !message_ )
      {
        break;
      }

      auto& message = *message_;

      // spawn a new handler for this message
      if ( !message.contains("method") )
      {
        continue;
      }

      auto method = message[ "method" ];

      std::cerr << "RX: " << message.dump( 2 ) << std::endl;

      if( method == "test" )
      {
        // TODO(Ben): what if we return from this function (and thus `out` goes
        // out of scope) before this coroutine completes. Does it get cancelled?
        // we could perhaps solve that by making 'out' a member of main(), but i
        // would rather understand the actual behaviour/lifetime here, and what
        // you're supposed to do (like should we have an equivalent of go's wait
        // group or whatver it's called)
        asio::co_spawn( co_await asio::this_coro::executor,
                        handle_test(out, std::move( message ) ),
                        asio::detached );
      }
      else if ( method == "initialize" )
      {
        asio::co_spawn( co_await asio::this_coro::executor,
                        lsp::handlers::on_initialize(out,
                                                     std::move( message ) ),
                        asio::detached );
      }
      else if ( method == "initialized" )
      {
         // Not sure we need to do anything here
      }
      else if ( method == "shutdown" )
      {
        co_await send_reply( out, message[ "id" ], {} );
      }
      else if ( method == "exit" )
      {
        break;
      }
      else
      {
        std::cerr << "Unknown message: " << method << std::endl;
      }
    }
  }
}

int main( int , char** )
{
  asio::io_context ctx;

  asio::co_spawn( ctx, lsp::server::dispatch_messages(), asio::detached );

  ctx.run();
}
