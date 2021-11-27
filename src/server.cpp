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


namespace
{
  using json = nlohmann::json;

  constexpr auto use_nothrow_coro = asio::experimental::as_tuple(
    asio::experimental::use_coro );
  constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(
    asio::use_awaitable );

  asio::experimental::coro<json> read_message(
    asio::posix::stream_descriptor str )
  {
    asio::streambuf buf;
    buf.prepare(2048);

    for(;;)
    {
      // Read the content-length header (as it is the only one that matters)
      size_t content_length = 0;
      for( ;; )
      {
        auto [ ec, bytes_read ] = co_await asio::async_read_until(
          str,
          buf,
          "\n",
          use_nothrow_coro );

        if ( ec )
        {
          co_return;
        }

        // -1 because we don't care about the \n
        std::string line{ asio::buffers_begin( buf.data() ),
                          asio::buffers_begin( buf.data() ) + bytes_read - 1 };

        buf.consume( bytes_read );

        // string any \r. The spec says all lines are terminated with \r\n but it
        // makes sense to just handle \n as well.
        while ( line.length() > 1 && line[ line.length() - 1 ] == '\r' )
          line.resize( line.length() - 1 );

        if ( line.empty() )
        {
          // We reached the end of headers
          break;
        }

        auto colon = line.find( ':' );
        if ( colon != std::string::npos )
        {
          std::string header( line.data(), colon );
          std::transform( header.begin(),
                          header.end(),
                          header.begin(),
                          []( auto c ){ return std::tolower(c); } );

          if ( header != "content-length" )
          {
            // this header is not interesting
            continue;
          }

          // skip whitespace
          colon++;
          while( colon < line.length() && std::isspace( line[ colon ] ) )
            ++colon;

          if ( colon >= line.length() )
            break;

          std::string_view value{ line.data() + colon, line.length() - colon };

          {
            auto [ _, ec ] = std::from_chars( value.data(),
                                              value.data() + value.length(),
                                              content_length );

            if ( ec != std::errc() )
            {
              break;
            }
          }

        }
      }

      if ( content_length == 0 )
      {
        // probably an error, try and listen for the next recognisable message.
        // This also helps from testing because we always get a \n after a full
        // message
        continue;
      }

      buf.prepare( content_length );
      {
        auto [ ec, content_read ] = co_await asio::async_read(
          str,
          buf,
          asio::transfer_exactly( content_length ),
          use_nothrow_coro );

        if ( ec )
        {
          co_return;
        }

        std::string message{
          asio::buffers_begin( buf.data() ),
          asio::buffers_begin( buf.data() ) + content_length };

        buf.consume( content_length );

        co_yield json::parse( message );
      }
    }
  }

  asio::awaitable<void> send_message( asio::posix::stream_descriptor& out,
                                      json response )
  {
    static asio::streambuf buf;

    std::string data = response.dump();
    buf.prepare( data.length() + 50 );

    std::ostream os(&buf);
    os << "Content-Length: "
       << data.length()
       << "\r\n\r\n"
       << data;

    co_await asio::async_write( out, buf, use_nothrow_awaitable );
  }

  asio::awaitable<void> handle_test( asio::posix::stream_descriptor& out,
                                     json message )
  {
    json response;
    response[ "method" ] = "reply";
    response[ "value" ] = message[ "input" ].get<double>() * 100;

    co_await send_message( out, response );
  }

  asio::awaitable<void> dispatch_messages()
  {
    asio::posix::stream_descriptor in(co_await asio::this_coro::executor,
                                      ::dup(STDIN_FILENO));
    asio::posix::stream_descriptor out(co_await asio::this_coro::executor,
                                      ::dup(STDOUT_FILENO));

    auto r = read_message( std::move(in) );
    for( ;; )
    {
      auto [ ec, message_ ] = co_await r.async_resume(
        use_nothrow_awaitable );

      if ( ec || !message_ )
      {
        break;
      }

      auto& message = *message_;

      std::cout << "The message: " << message.dump( 2 ) << "\n";

      // spawn a new handler for this message
      if ( !message.contains("method") )
      {
        continue;
      }

      auto method = message[ "method" ];

      if( method == "test" )
      {
        asio::co_spawn( co_await asio::this_coro::executor,
                        handle_test(out, std::move( message ) ),
                        asio::detached );
      }
    }
  }
}

int main( int , char** )
{
  asio::io_context ctx;

  asio::co_spawn( ctx, dispatch_messages(), asio::detached );

  ctx.run();
}
