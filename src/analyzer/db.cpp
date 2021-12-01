#pragma once

#include "source_location.cpp"
#include <tuple>
#include <unordered_map>
#include <map>
#include <deque>
#include <utility>

namespace DB
{
  // FIXME: Using heap allocation here is just laziness. It allows me to extract
  // pointers from the Db and use them while manipulating the tables. it isn't
  // actually needed, and it's basically a massive performance drain to avoid
  // typing. Solution might be so just make a macro that gets one of these
  // things and always use that. But for now we hammer malloc until it goes blue
  // in the face.
  //
  // FIXME: lazy non-serialisable vector of pointers
  template< typename T >
  using Storage = std::vector< std::unique_ptr< T > >;

  // TODO: we want:
  //  - to be able to specify arbitrary keys for a type (specialise?)

#if 0
  // FIXME: Lazy non-serialisable standard library containers
  template< typename TKey, typename... TValues >
  using HashUniqueKey = std::unordered_map< TKey, std::tuple< TValues... > >;

  // FIXME: Lazy non-serialisable standard library containers
  template< typename TKey, typename... TValues >
  using SortUniqueKey = std::map< TKey, std::tuple< TValues... > >;

  // FIXME: Lazy non-serialisable standard library containers
  template< typename TKey, typename... TValues >
  using HashIndex = std::unordered_multimap< TKey, std::tuple< TValues... > >;
#endif

  // FIXME: Lazy non-serialisable standard library containers
  template< typename TKey, typename TValue, typename... TRest >
  using SortIndex =
    std::multimap< TKey,
                   std::conditional_t< sizeof...( TRest ) == 0,
                                       TValue,
                                       std::tuple< TValue, TRest... > > >;

  // FIXME: Lazy non-serialisable standard library containers
  template< typename T >
  using FreeList = std::deque< T >;

  template< typename TRecord, typename TRow >
  struct Record
  {
    using Table = Storage< TRow >;
    using Row = TRow;

    Table table;

    template< typename... Args >
    Row& Insert( Args&&... args )
    {
      const auto id = table.size() + 1;
      auto& row = table.emplace_back( std::forward< Args >( args )... );
      row->id = id;
      static_cast< TRecord* >( this )->UpdateKeys( *row );
      return *row;
    }

    Row& Get( typename TRow::ID id ) const
    {
      if ( id < 1 )
      {
        assert( false && "Invalid id must be 1 or greater" );
        abort();
      }

      if ( id > table.size() )
      {
        assert( false && "Invalid id" );
        abort();
      }

      // FIXME: If anything is ever removed from the table, boom
      return *table.at( id - 1 ).get();
    }
    // FreeList<size_t> free_;
  };

  // OK, we're going all in. CRTP because why the hell not.
  template< typename TRow >
  struct NamedRecord : Record< NamedRecord< TRow >, TRow >
  {
    SortIndex< decltype( TRow::name ), typename TRow::ID > byName;

    void UpdateKeys( const TRow& row )
    {
      byName.emplace( row.name, row.id );
    }
  };

  template< typename TRow >
  struct RefRecord : NamedRecord< TRow >
  {
    using Reference = typename TRow::Reference;
    using ID = typename TRow::ID;

    Storage< Reference > references;

    Reference& AddReference( Reference&& r )
    {
      auto pos = references.size();
      auto& ref = references.emplace_back(
        new Reference( std::forward< Reference >( r ) ) );
      refsByID.emplace( r.id, pos );
      return *ref;
    }

    SortIndex< typename TRow::ID, size_t > refsByID;
  };


}  // namespace DB
