#include "lsp/server.hpp"
#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/buffers_iterator.hpp>
#include <asio/co_spawn.hpp>
#include <asio/completion_condition.hpp>
#include <asio/coroutine.hpp>
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
#include <exception>
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

#include <lsp/comms.cpp>
#include <lsp/handlers.cpp>

namespace lsp::server
{
  template< typename... Ts >
  void handle_unexpected_exception( std::exception_ptr ep, Ts&&... )
  {
    if ( ep )
    {
      try
      {
        std::rethrow_exception( ep );
      }
      catch ( const std::exception& e )
      {
        std::cerr << "Unhandled exception! " << e.what() << std::endl;
      }
    }
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
      try
      {
        auto message_ = co_await r.async_resume(
          asio::use_awaitable );

        if ( !message_ )
        {
          std::cerr << "Empty message!" << std::endl;
          break;
        }

        const auto& message = *message_;

        // spawn a new handler for this message
        if ( !message.contains("method") )
        {
          continue;
        }

        const auto& method = message[ "method" ];

        std::cerr << "RX: " << message.dump( 2 ) << std::endl;

        if ( method == "initialize" )
        {
          asio::co_spawn( co_await asio::this_coro::executor,
                          lsp::handlers::handle_initialize(out, message ),
                          handle_unexpected_exception<> );
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
        else if ( method == "workspace/didChangeConfiguration" )
        {
            lsp::handlers::on_workspace_didchangeconfiguration(out, message );
        }
        else if ( method == "textDocument/didOpen" )
        {
          lsp::handlers::on_textdocument_didopen(out, message );
        }
        else if ( method == "textDocument/didChange" )
        {
          lsp::handlers::on_textdocument_didchange(out, message );
        }
        else if ( method == "textDocument/didClose" )
        {
          lsp::handlers::on_textdocument_didclose(out, message );
        }
        else if ( method == "textDocument/references" )
        {
          co_await lsp::handlers::on_textdocument_references( out, message );
        }
        else
        {
          std::cerr << "Unknown message: " << method << std::endl;
        }
      }
      catch ( const std::exception& ex )
      {
        std::cerr << "exception handling message: "
                  << ex.what()
                  << std::endl;
        break;
      }
    }
  }

}

int main( int , char** argv )
{
  asio::io_context ctx;
  lsp::server::initialise_server( argv );

  asio::co_spawn( ctx,
                  lsp::server::dispatch_messages(),
                  lsp::server::handle_unexpected_exception<> );

  ctx.run();

  lsp::server::cleanup_server();
}
