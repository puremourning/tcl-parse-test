#pragma once

#include <asio/completion_condition.hpp>
#include <asio/experimental/as_tuple.hpp>
#include <asio/experimental/use_coro.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/read_until.hpp>
#include <asio.hpp>
#include <charconv>
#include <json/json.hpp>

#include "types.cpp"

namespace lsp
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
        std::cerr << "Reading header line... "
                  << std::endl;

        // NOTE(Ben): This may make 0 read calls if the get buffer already
        // contains the delimiter.
        auto [ ec, bytes_read ] = co_await asio::async_read_until(
          str,
          buf,
          "\n",
          use_nothrow_coro );

        std::cerr << "Read "
                  << bytes_read
                  << " bytes from the stream"
                  << std::endl;

        if ( ec )
        {
          co_return;
        }

        // -1 because we don't care about the \n
        std::string line{ asio::buffers_begin( buf.data() ),
                          asio::buffers_begin( buf.data() ) + bytes_read - 1 };

        std::cerr << "Header line..."
                  << line
                  << std::endl;

        buf.consume( bytes_read );

        // string any \r. The spec says all lines are terminated with \r\n but it
        // makes sense to just handle \n as well.
        while ( line.length() > 0 && line[ line.length() - 1 ] == '\r' )
          line.resize( line.length() - 1 );

        if ( line.empty() )
        {
          // We reached the end of headers
          std::cerr << "End of headers!" << std::endl;
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

          std::cerr << "Header..."
                    << header
                    << std::endl;

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

          std::cerr << "Value..."
                    << value
                    << std::endl;

          {
            auto [ _, ec ] = std::from_chars( value.data(),
                                              value.data() + value.length(),
                                              content_length );

            if ( ec != std::errc() )
            {
              break;
            }

            std::cerr << "ContentLength..."
                      << content_length
                      << std::endl;

          }
        }
      }

      std::cerr << "About to read "
                << content_length
                << " bytes of message data with buffer size "
                << buf.size()
                << std::endl;

      if ( content_length == 0 )
      {
        // probably an error, try and listen for the next recognisable message.
        // This also helps from testing because we always get a \n after a full
        // message
        continue;
      }

      buf.prepare( content_length );
      while (buf.size() < content_length )
      {
        auto [ ec, content_read ] = co_await asio::async_read(
          str,
          buf,
          asio::transfer_exactly(content_length - buf.size()),
          use_nothrow_coro );

        if ( ec )
        {
          co_return;
        }
      }

      std::string message{
        asio::buffers_begin( buf.data() ),
        asio::buffers_begin( buf.data() ) + content_length };

      std::cerr << "Read a message (buffer size="
                << buf.size()
                << ", actual msg length="
                << message.length()
                << "): "
                << message
                << std::endl;

      buf.consume( content_length );

      co_yield json::parse( message );
    }
  }

  asio::awaitable<void> send_message( asio::posix::stream_descriptor& out,
                                      json message )
  {
    static asio::streambuf buf;

    std::string data = message.dump();
    buf.prepare( data.length() + 50 );

    std::ostream os(&buf);
    os << "Content-Length: "
       << data.length()
       << "\r\n\r\n"
       << data;

    co_await asio::async_write( out, buf, use_nothrow_awaitable );
  }

  template< typename id_t >
  asio::awaitable<void> send_reply( asio::posix::stream_descriptor& out,
                                    id_t reply_to,
                                    json result )
  {
    json message( json::value_t::object );
    message[ "jsonrpc" ] = "2.0";
    message[ "id" ] = reply_to;
    message[ "result" ] = std::move( result );

    co_await send_message( out, std::move( message ) );
  }

  template< typename id_t >
  asio::awaitable<void> send_reject( asio::posix::stream_descriptor& out,
                                     id_t reply_to,
                                     const ResponseError& result )
  {
    json message( json::value_t::object );
    message[ "jsonrpc" ] = "2.0";
    message[ "id" ] = reply_to;
    message[ "result" ] = result;

    co_await send_message( out, std::move( message ) );
  }

  uint64_t next_id = 0;

  asio::awaitable<void> send_notification( asio::posix::stream_descriptor& out,
                                           std::string method,
                                           std::optional< json > params )
  {
    json message( json::value_t::object );
    message[ "jsonrpc" ] = "2.0";
    message[ "id" ] = next_id++;
    message[ "method" ] = std::move( method );
    if ( params )
    {
      message[ "params" ] = std::move( *params );
    }

    co_await send_message( out, std::move( message ) );
  }
}
