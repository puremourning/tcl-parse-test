#pragma once

#include <unordered_map>
#include <map>
#include <deque>

namespace DB
{
  // FIXME: Using heap allocation here is just laziness. It allows me to extract
  // pointers from the Db and use them while manipulating the tables. it isn't
  // actually needed, and it's basically a massive performance drain to avoid
  // typing. Solution might be so just make a macro that gets one of these
  // things and always use that. But for now we hammer malloc until it goes blue
  // in the face.
  template< typename T >
  using Table = std::vector< std::unique_ptr< T > >;

  // FIXME: Lazy standard library containers
  template< typename TKey, typename TValue >
  using HashKey = std::unordered_map< TKey, TValue >;

  // FIXME: Lazy standard library containers
  template< typename TKey, typename TValue >
  using SortKey = std::map< TKey, TValue >;

  // FIXME: Lazy standard library containers
  template< typename T>
  using FreeList = std::deque<T>;

  template< typename TRow >
  struct Record
  {
    using Table = Table< TRow >;
    using Row = TRow;

    Table table;
    SortKey< decltype( TRow::name ), size_t > byName;

    template< typename... Args >
    Row& Insert( Args&&... args )
    {
      const auto id = table.size();
      auto& row = table.emplace_back( std::forward<Args...>( args )... );
      row->id = id;
      byName.emplace( row->name, id );
      return *row;
    }

    Row& Get( typename TRow::ID id )
    {
      if constexpr ( std::is_signed< typename TRow::ID >::value )
      {
        if ( id < 0 )
        {
          assert( false && "Invalid id must be 0 or greater" );
          abort();
        }
      }

      if ( id >= table.size() )
      {
        assert( false && "Invalid id" );
        abort();
      }

      // FIXME: If anything is ever removed from the table, boom
      return *table.at( id ).get();
    }
    // FreeList<size_t> free_;
  };
}
