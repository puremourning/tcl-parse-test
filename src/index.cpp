#include "script.cpp"
#include "source_location.cpp"
#include "tclDecls.h"
#include "tclInt.h"
#include <_types/_uint64_t.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
#include <string>
#include <unordered_set>

namespace Index
{
  struct Namespace;

  struct QualifiedName
  {
    std::optional< std::string > ns;
    std::string name;

    bool IsAbs() const
    {
      if ( !ns || ns.value().length() < 2 )
      {
        return false;
      }

      if( ns.value().substr(0, 2) == "::" )
      {
        return true;
      }

      return false;
    }

    QualifiedName AbsPath( std::string_view current_path ) const
    {
      if ( IsAbs() )
      {
        return *this;
      }

      return QualifiedName{
        .ns = std::string( current_path )
            + ( ns.has_value() ? ns.value() : "" ),
        .name = name
      };
    }

    std::string Path()
    {
      if ( ns.has_value() )
      {
        return ns.value() + "::" + name;
      }
      return name;
    }
  };

  QualifiedName SplitName( std::string_view name )
  {
    // If name starts with :: it's absolute and `ns` is ignored
    // Otherwise we concatenate anything up to the last :: to `ns`
    // name is always everything after the last ::

    QualifiedName qn;
    auto pos = name.rfind( "::" );
    if ( pos == std::string_view::npos )
    {
      qn.name = name;
    }
    else if ( name.length() > 2 && name.substr( 0, 2 ) == "::" )
    {
      qn.name = name.substr( pos + 2 );
      qn.ns = name.substr( 0, pos );
    }
    else
    {
      qn.name = name.substr( pos + 2 );
      qn.ns = name.substr( 0, pos );
    }

    return qn;
  }

  std::vector<std::string_view> SplitPath( std::string_view path )
  {
    std::vector<std::string_view> vec;
    std::string_view::size_type start = 0;
    for( std::string_view::size_type cur = 0;
         cur < path.length();
         ++cur )
    {
      if ( path[ cur ] == ':' && cur+1 < path.length() && path[ cur+1 ] == ':' )
      {
        vec.push_back( path.substr( start, cur - start ) );
        cur ++;
        start = cur + 1;
      }
    }

    vec.push_back( path.substr( start ) );

    return vec;
  }

  struct Proc;
  struct Namespace;
  struct Variable;
  struct Scope;

  using ID = size_t;
  using VariableID = ID;
  using ScopeID = ID;
  using ProcID = ID;
  using NamespaceID = ID;

  struct Variable
  {
    using ID = VariableID;

    VariableID id;
    std::string name;

    struct Reference
    {
      Parser::SourceLocation location;
      VariableID variable;
    };

    inline static ID next_id = 1;
  };

  struct Scope
  {
    std::vector< VariableID > variables;
    std::vector< ProcID > procs;
    std::vector< VariableID > imported;
  };

  struct Proc
  {
    using ID = ProcID;
    ProcID id;
    std::string name;
    std::vector< VariableID > arguments;

    Scope scope;
    NamespaceID parent_namespace;

    struct Reference
    {
      Parser::SourceLocation location;
      ProcID proc;
    };

    inline static ID next_id = 1;
  };

  struct Namespace
  {
    using ID = NamespaceID;
    NamespaceID id;
    std::string name;
    Scope scope;

    std::vector< NamespaceID > child_namespaces;
    std::optional< NamespaceID > parent_namespace;

    struct Reference
    {
      Parser::SourceLocation location;
      NamespaceID ns;
    };

    inline static ID next_id = 1;
  };

  // FIXME: Using heap allocation here is just laziness. It allows me to extract
  // pointers from the Db and use them while manipulating the tables. it isn't
  // actually needed, and it's basically a massive performance drain to avoid
  // typing. Solution might be so just make a macro that gets one of these
  // things and always use that. But for now we hammer malloc until it goes blue
  // in the face.
  template< typename T >
  using Table = std::vector< std::unique_ptr< T > >;

  struct Index
  {
    Table< Namespace > namespaces;
    Table< Proc > procs;
    Table< Variable > variables;

    Table< Namespace::Reference > nsrefs;
    Table< Variable::Reference > vrefs;
    Table< Proc::Reference > prefs;

    NamespaceID global_namespace_id;
  } index;

  template<typename Entity>
  auto AllocateID() -> typename Entity::ID
  {
    return Entity::next_id++;
  }

  Index make_index()
  {
    Index index{};

    //
    // FIXME: Eveerything blows up if these vectors ever resize. Switch to
    // something else, list a vector of heap-allocated things, use a slab
    // allocator? or maybe a absl::node_hash_map.
    //
    index.namespaces.reserve( 20 );
    index.procs.reserve( 1024 );
    index.variables.reserve( 1024 * 1024 );

    auto& global_namespace = index.namespaces.emplace_back( new Namespace{
      .id = AllocateID<Namespace>(),
      .name = "<global>",
    } );

    index.global_namespace_id = global_namespace->id;

    return index;
  }

  struct ScanContext
  {
    std::vector< NamespaceID > nsPath;
  };

  void ScanScript( Index& index,
                   ScanContext& context,
                   const Parser::Script& script );

  void ScanWord ( Index& index,
                  ScanContext& context,
                  const Parser::Word& word )
  {
    using Word = Parser::Word;
    switch ( word.type )
    {
      case Word::Type::EXPAND:
      case Word::Type::TOKEN_LIST:
      {
        for( auto& subWord : std::get< Word::WordVec >( word.data ) )
        {
          ScanWord( index, context, subWord );
        }
        break;
      }
      case Word::Type::SCRIPT:
      {
        ScanScript( index,
                    context,
                    *std::get< Word::ScriptPtr >( word.data ) );
        break;
      }

      default: // ignore
        break;
    }
  }

  template< typename Entity >
  Entity* Get( Table< Entity >& db, typename Entity::ID id )
  {
    if ( id < 1 || id > db.size() )
    {
      return nullptr;
    }

    return db.at( id - 1 ).get();
  }

  template< typename Entity >
  std::string GetPrintName( Index& index, const Entity& e )
  {
    constexpr bool is_opt =
      std::is_same< decltype(e.parent_namespace),
                    std::optional<typename Entity::ID>>();

    if constexpr ( is_opt )
    {
      if ( !e.parent_namespace )
      {
        return e.name;
      }
    }

    std::vector<std::string_view> parts;
    parts.push_back( e.name );
    std::optional<NamespaceID> curr_id = e.parent_namespace;
    while ( curr_id )
    {
      Namespace* curr = Get( index.namespaces, *curr_id );
      parts.push_back( curr->name );
      curr_id = curr->parent_namespace;
    }

    std::ostringstream o;
    for( auto i = parts.rbegin(); i != parts.rend(); ++i )
    {
      if ( i != parts.rbegin() )
      {
        o << "::";
      }
      o << *i;
    }

    return o.str();

  }

  Namespace& ResolveNamespace( Index& index,
                               const QualifiedName& qn,
                               Namespace& ns )
  {
    auto cur_id = ns.id;
    std::vector<std::string_view> parts;
    if ( qn.IsAbs() )
    {
      cur_id = index.global_namespace_id;
      parts = SplitPath( std::string_view( *qn.ns  ).substr( 2 ) );
    }
    else
    {
      parts = SplitPath( *qn.ns );
    }

    for ( auto part : parts )
    {
      auto* current = Get( index.namespaces, cur_id );
      auto& children = current->child_namespaces;
      auto child_pos = std::find_if(
        children.begin(),
        children.end(),
        [&]( auto child_id ) {
          return Get( index.namespaces, child_id )->name == part;
        } );

      if ( child_pos == children.end() )
      {
        auto& child = *index.namespaces.emplace_back( new Namespace{
          .id = AllocateID<Namespace>(),
          .name{ part },
          .parent_namespace = current->id,
        } );
        children.push_back( child.id );
        cur_id = child.id;
      }
      else
      {
        cur_id = *child_pos;
      }
    }

    return *Get( index.namespaces, cur_id );
  }

  template< typename WordVec >
  void AddProcToIndex( Index& index,
                       Namespace& ns,
                       const WordVec& words )
  {
    using Word = Parser::Word;
    if ( words.size() == 4 &&
         words[ 1 ].type == Word::Type::TEXT )
    {
      // proc name { arg|{ arg default } ... } { body }
      std::vector< VariableID > args;
      if ( words[ 2 ].type == Word::Type::LIST )
      {
        auto& vec = std::get< Word::WordVec >( words[ 2 ].data );
        args.reserve( vec.size() );
        for( auto& arg : vec )
        {
          std::string argName;
          if ( arg.type == Word::Type::TEXT )
          {
            argName = arg.text;
          }
          else
          {
            argName = std::get< Word::WordVec >( arg.data )[ 0 ].text;
          }

          auto& v = *index.variables.emplace_back( new Variable{
            .id = AllocateID<Variable>(),
            .name = std::move( argName ),
          } );
          args.push_back( v.id );
        }
      }
      auto qn = SplitName( words[ 1 ].text );
      auto& proc = *index.procs.emplace_back( new Proc{
        .id = AllocateID<Proc>(),
        .name = qn.name,
        .arguments{ std::move( args ) },
      } );

      if ( qn.IsAbs() || qn.ns )
      {
        auto& resolved = ResolveNamespace( index, qn, ns );
        resolved.scope.procs.push_back( proc.id );
        proc.parent_namespace = resolved.id;
      }
      else
      {
        ns.scope.procs.push_back( proc.id );
        proc.parent_namespace = ns.id;
      }
    }
  }

  void ScanScript( Index& index,
                   ScanContext& context,
                   const Parser::Script& script )
  {
    using Word = Parser::Word;
    // Find namespace, proc and variable declarations
    for ( auto& call : script.commands )
    {
      auto& ns = *Get( index.namespaces, context.nsPath.back() );

      auto scanned = false;

      if ( call.words[ 0 ].type == Word::Type::TEXT )
      {
        auto& cmdName = call.words[ 0 ].text;

        if ( cmdName == "proc" )
        {
          AddProcToIndex( index, ns, call.words );
        }
        else if ( cmdName == "namespace" )
        {
          if ( call.words.size() == 4 &&
               call.words[ 1 ].type == Word::Type::TEXT &&
               call.words[ 1 ].text == "eval" &&
               call.words[ 2 ].type == Word::Type::TEXT )
          {
            QualifiedName qn = {
              .ns = std::string( call.words[ 2 ].text ),
              .name = "",
            };
            context.nsPath.push_back( ResolveNamespace( index, qn, ns ).id );
            ScanWord( index, context, call.words[ 3 ] );
            context.nsPath.pop_back();
            scanned = true;
          }
        }
        else if ( cmdName == "set" )
        {
          if ( call.words.size() == 3 )
          {
            // set name value
            //
            // if name already in scope, it's an update, otherwise, it's a
            // declaration

          }
        }
        else if ( cmdName == "array" )
        {

        }
        else if ( cmdName == "global" )
        {
          // global name [name ...]
          //
          // could be that we're asking for the value, but most likely this is
          // either declaring or importing name from the namespae scope (like
          // global above)
        }
        else if ( cmdName == "variable" )
        {
          if ( call.words.size() > 3 )
          {
            if ( ( call.words.size() - 1 ) % 2 == 0 )
            {
              // variable [name value]... name value
            }
            else
            {
              // variable [name value]... name
            }
          }
          else if ( call.words.size() == 3 )
          {
            // variable name value
            //
            // if name already in scope, it's an update, otherwise, it's a
            // declaration
          }
          else if ( call.words.size() == 2 )
          {
            // variable name
            //
            // could be that we're asking for the value, but most likely this is
            // either declaring or importing name from the namespae scope (like
            // global above)
          }
        }
        else if ( cmdName == "upvar" )
        {

        }
        else if ( cmdName == "uplevel" )
        {

        }
      }

      if ( ! scanned )
      {
        for ( auto& word : call.words )
        {
          ScanWord( index, context, word );
        }
      }
    }
  }

  void IndexScript( Index& index,
                    ScanContext& context,
                    const Parser::Script& script );

  void IndexWord ( Index& index,
                   ScanContext& context,
                   const Parser::Word& word )
  {
    using Word = Parser::Word;
    switch( word.type )
    {
      case Word::Type::ARRAY_ACCESS:
      {
        const auto& arrayAccess = std::get< Parser::Word::ArrayAccess >(
          word.data );

        // if ( auto v = Find( index.variables, context, arrayAccess.name ) )
        // {
        //   AddVariableReference( index, *v, word.location );
        // }

        for( const auto& subWord : arrayAccess.index )
        {
          IndexWord( index, context, subWord );
        }
        break;
      }

      case Word::Type::EXPAND: // fall through
      case Word::Type::TOKEN_LIST:
      {
        const auto& subWords = std::get< Word::WordVec >( word.data );
        for( const auto& subWord : subWords )
        {
          IndexWord( index, context, subWord );
        }
        break;
      }

      case Word::Type::VARIABLE:
      {
        // if ( auto v = Find( index.variables, context, word.text ) )
        // {
        //   AddVariableReference( index, *v, word.location );
        // }
        break;
      }

      case Word::Type::SCRIPT:
      {
        const auto& script = *std::get< Word::ScriptPtr >( word.data );
        IndexScript( index, context, script );
        break;
      }

      default:
        break;
    }
  }


  void IndexScript( Index& index,
                    ScanContext& context,
                    const Parser::Script& script )
  {
    using Word = Parser::Word;

    for ( auto& call : script.commands )
    {

      // Add a reference to the proc being called if we can
      if ( call.words[ 0 ].type == Word::Type::TEXT )
      {
        //auto& cmdName = call.words[ 0 ].text;

        // if ( Proc* proc = Find( index.procs, context, cmdName ) )
        // {
        //   // AddCommandReference( index,
        //   //                      *proc,
        //   //                      call.words[ 0 ].location );
        // }
      }

      // Add references to any variables that are in the command
      for( const auto& word : call.words )
      {
        IndexWord( index, context, word );
      }
    }
  }

  void Build( Index& index,
              ScanContext& context,
              const Parser::Script& script )
  {
    ScanScript( index, context, script );
    IndexScript( index, context, script );
  }

}  // namespace Index
