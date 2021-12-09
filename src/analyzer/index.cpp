#include "script.cpp"
#include "source_location.cpp"
#include "db.cpp"
#include "tclDecls.h"
#include "tclInt.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <map>
#include <variant>
#include <vector>
#include <string>
#include <unordered_set>

namespace Index
{
  struct Namespace;

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
      VariableID id;
    };
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
      ProcID id;
    };
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
      NamespaceID id;
    };
  };

  struct Index
  {
    DB::RefRecord< Namespace > namespaces;
    DB::RefRecord< Proc > procs;
    DB::RefRecord< Variable > variables;

    NamespaceID global_namespace_id;
  };

  Index make_index()
  {
    Index index{};

    index.namespaces.table.reserve( 20 );
    index.procs.table.reserve( 1024 );
    index.variables.table.reserve( 1024 * 1024 );

    auto& global_namespace = index.namespaces.Insert( new Namespace{
      .name = "",
    } );

    index.global_namespace_id = global_namespace.id;

    return index;
  }

  struct ScanContext
  {
    std::vector< NamespaceID > nsPath;
  };

  void ScanScript( Index& index,
                   ScanContext& context,
                   const Parser::Script& script );

  void ScanWord( Index& index, ScanContext& context, const Parser::Word& word )
  {
    using Word = Parser::Word;
    switch ( word.type )
    {
      case Word::Type::EXPAND:
      case Word::Type::TOKEN_LIST:
      {
        for ( auto& subWord : std::get< Word::WordVec >( word.data ) )
        {
          ScanWord( index, context, subWord );
        }
        break;
      }
      case Word::Type::SCRIPT:
      {
        ScanScript( index, context, *std::get< Word::ScriptPtr >( word.data ) );
        break;
      }

      default:  // ignore
        break;
    }
  }


  template< typename Entity >
  std::string GetPrintName( Index& index, const Entity& e )
  {
    constexpr bool is_opt =
      std::is_same< decltype( e.parent_namespace ),
                    std::optional< typename Entity::ID > >();

    if constexpr ( is_opt )
    {
      if ( !e.parent_namespace )
      {
        return e.name;
      }
    }

    std::vector< std::string_view > parts;
    parts.push_back( e.name );
    std::optional< NamespaceID > curr_id = e.parent_namespace;
    while ( curr_id )
    {
      const Namespace& curr = index.namespaces.Get( *curr_id );
      parts.push_back( curr.name );
      curr_id = curr.parent_namespace;
    }

    std::ostringstream o;
    for ( auto i = parts.rbegin(); i != parts.rend(); ++i )
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
                               const Parser::QualifiedName& qn,
                               Namespace& ns )
  {
    auto cur_id = ns.id;
    std::vector< std::string_view > parts = qn.NamespaceParts();
    if ( qn.absolute )
    {
      cur_id = index.global_namespace_id;
    }

    for ( auto part : parts )
    {
      auto& current = index.namespaces.Get( cur_id );
      auto& children = current.child_namespaces;
      auto child_pos =
        std::find_if( children.begin(),
                      children.end(),
                      [ & ]( auto child_id ) {
                        return index.namespaces.Get( child_id ).name == part;
                      } );

      if ( child_pos == children.end() )
      {
        auto& child = index.namespaces.Insert( new Namespace{
          .name = std::string{ part },
          .parent_namespace = current.id,
        } );
        children.push_back( child.id );
        cur_id = child.id;
      }
      else
      {
        cur_id = *child_pos;
      }
    }

    return index.namespaces.Get( cur_id );
  }

  Namespace* FindNamespace( const Index& index, std::string_view ns_name )
  {
    auto qn = Parser::SplitName( ns_name );
    assert( qn.absolute );
    auto cur_id = index.global_namespace_id;

    std::vector< std::string_view > parts = qn.Parts();
    for ( auto part : parts )
    {
      auto& current = index.namespaces.Get( cur_id );
      auto& children = current.child_namespaces;
      auto child_pos =
        std::find_if( children.begin(),
                      children.end(),
                      [ & ]( auto child_id ) {
                        return index.namespaces.Get( child_id ).name == part;
                      } );

      if ( child_pos == children.end() )
      {
        return nullptr;
      }
      else
      {
        cur_id = *child_pos;
      }
    }

    return &index.namespaces.Get( cur_id );
  }

  template< typename WordVec >
  void AddProcToIndex( Index& index, Namespace& ns, const WordVec& words )
  {
    using Word = Parser::Word;
    // proc name { arg|{ arg default } ... } { body }
    std::vector< VariableID > args;
    if ( words[ 2 ].type == Word::Type::LIST )
    {
      auto& vec = std::get< Word::WordVec >( words[ 2 ].data );
      args.reserve( vec.size() );
      for ( auto& arg : vec )
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

        auto& v = index.variables.Insert( new Variable{
          .name = std::move( argName ),
        } );
        args.push_back( v.id );
      }
    }
    auto qn = Parser::SplitName( words[ 1 ].text );
    auto& proc = index.procs.Insert( new Proc{
      .name = qn.name,
      .arguments{ std::move( args ) },
    } );

    if ( qn.absolute || qn.ns )
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

  void ScanScript( Index& index,
                   ScanContext& context,
                   const Parser::Script& script )
  {
    using Call = Parser::Call;

    // Find namespace, proc and variable declarations
    for ( auto& call : script.commands )
    {
      auto& ns = index.namespaces.Get( context.nsPath.back() );

      auto scanned = false;

      switch ( call.type )
      {
        case Call::Type::NAMESPACE_EVAL:
        {
          Parser::QualifiedName qn = {
            .ns = std::string( call.words[ 2 ].text ),
            .name = "",
          };
          context.nsPath.push_back( ResolveNamespace( index, qn, ns ).id );
          ScanWord( index, context, call.words[ 3 ] );
          context.nsPath.pop_back();
          scanned = true;
          break;
        }
        case Call::Type::PROC:
        {
          AddProcToIndex( index, ns, call.words );
          break;
        }
#if 0
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
#endif
        default:
          // ignore
          break;
      }

      if ( !scanned )
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

  void IndexWord( Index& index, ScanContext& context, const Parser::Word& word )
  {
    using Word = Parser::Word;
    switch ( word.type )
    {
      case Word::Type::ARRAY_ACCESS:
      {
        const auto& arrayAccess =
          std::get< Parser::Word::ArrayAccess >( word.data );

        // if ( auto v = Find( index.variables, context, arrayAccess.name ) )
        // {
        //   AddVariableReference( index, *v, word.location );
        // }

        for ( const auto& subWord : arrayAccess.index )
        {
          IndexWord( index, context, subWord );
        }
        break;
      }

      case Word::Type::EXPAND:  // fall through
      case Word::Type::TOKEN_LIST:
      {
        const auto& subWords = std::get< Word::WordVec >( word.data );
        for ( const auto& subWord : subWords )
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

  void AddCommandReference( Index& index,
                            const Parser::SourceLocation& location,
                            const Proc& proc )
  {
    index.procs.AddReference(
      Proc::Reference{ .location = location, .id = proc.id } );
  }

  Proc* FindProc( Index& index, Namespace& ns, std::string_view cmdName )
  {
    auto qn = Parser::SplitName( cmdName );
    Namespace::ID target_namespace = ns.id;

    auto range = index.procs.byName.equal_range( qn.name );

    if ( qn.absolute || qn.ns )
    {
      target_namespace = ResolveNamespace( index, qn, ns ).id;
    }

    for ( auto it = range.first; it != range.second; ++it )
    {
      auto& p = index.procs.Get( it->second );
      if ( p.parent_namespace == target_namespace )
      {
        return &p;
      }
    }

    if ( !qn.absolute && ns.parent_namespace )
    {
      // Find in the parent namespace
      // FIXME: This recursion is extremely SUB-optimal. A loop would be much
      // faster
      return FindProc( index,
                       index.namespaces.Get( *ns.parent_namespace ),
                       cmdName );
    }

    return nullptr;
  }


  void IndexScript( Index& index,
                    ScanContext& context,
                    const Parser::Script& script )
  {
    using Call = Parser::Call;

    for ( auto& call : script.commands )
    {
      auto& ns = index.namespaces.Get( context.nsPath.back() );

      auto scanned = false;
      switch ( call.type )
      {
        case Call::Type::NAMESPACE_EVAL:
        {
          Parser::QualifiedName qn = {
            .ns = std::string( call.words[ 2 ].text ),
            .name = "",
          };
          context.nsPath.push_back( ResolveNamespace( index, qn, ns ).id );
          IndexWord( index, context, call.words[ 3 ] );
          context.nsPath.pop_back();
          scanned = true;
          break;
        }
        case Call::Type::PROC:
        {
          Parser::QualifiedName procName = Parser::SplitName(
            call.words[ 1 ].text );
          context.nsPath.push_back(
            ResolveNamespace( index, procName, ns ).id );
          IndexWord( index, context, call.words[ 3 ] );
          context.nsPath.pop_back();
          scanned = true;
          break;
        }
        case Call::Type::USER:
        {
          if ( Proc* proc = FindProc( index, ns, call.words[ 0 ].text ) )
          {
            // Add a reference to the proc being called if we can
            AddCommandReference( index, call.words[ 0 ].location, *proc );
          }
        }

        default:
          // ignore
          break;
      }

      // Add references to any variables that are in the command
      if ( !scanned )
      {
        for ( const auto& word : call.words )
        {
          IndexWord( index, context, word );
        }
      }
    }
  }

  void Build( Index& index, ScanContext& context, const Parser::Script& script )
  {
    ScanScript( index, context, script );
    IndexScript( index, context, script );
  }

  // TODO: This isn't really a good cursor, as Calls/Scripts don't point to
  // their parents. If they did we'd have more of a tree cursor
  struct ScriptCursor
  {
    // TODO: Need the namespace here, but that's part of the parser
    const Parser::Call* call;
    size_t argument;
    const Parser::Word* word;
  };

  ScriptCursor LinePosToScriptPosition( const Parser::Script& script,
                                        Parser::LinePos pos )
  {
    // TODO: binary chop the commands
    ScriptCursor result = {};

    for ( const auto& call : script.commands )
    {
      // TODO: Do namespace tracking like we do above ?
      //
      // Or better store the namespace in the Script somehow as part of the
      // parser walk above (or perhaps move that part to the parser, so the
      // indexer already has the QualifiedNames)
      for ( size_t arg = 0; arg < call.words.size(); ++ arg )
      {
        const auto& word = call.words[ arg ];
        if ( word.location.line > pos.line )
        {
          goto LinePosToScriptPosition_finished;
        }
        else if ( word.location.line == pos.line &&
                  word.location.column > pos.column )
        {
          goto LinePosToScriptPosition_finished;
        }

        if ( word.type == Parser::Word::Type::SCRIPT )
        {
          result = LinePosToScriptPosition(
            *std::get<Parser::Word::ScriptPtr>( word.data ),
            pos );
        }
        else
        {
          result = {
            .call = &call,
            .argument = arg,
            .word = &word
          };
        }
      }
    }
    LinePosToScriptPosition_finished:
      return result;
  }


}  // namespace Index

namespace Index::Test
{
  void Run()
  {
  }
}  // namespace Index::Test
