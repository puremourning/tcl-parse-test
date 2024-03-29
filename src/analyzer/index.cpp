#include "script.cpp"
#include "source_location.cpp"
#include "db.cpp"
#include "tclDecls.h"
#include "tclInt.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
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

  enum class ReferenceType
  {
    USAGE,
    DEFINITION,
    DECLARAION
  };

  auto& operator<<( auto& o, ReferenceType t )
  {
    switch ( t )
    {
      case ReferenceType::USAGE: o << "Usage"; break;
      case ReferenceType::DEFINITION: o << "Definition"; break;
      case ReferenceType::DECLARAION: o << "Declaration"; break;
    }
    return o;
  }

  struct Variable
  {
    using ID = VariableID;

    VariableID id;
    std::string name;

    struct Reference
    {
      Parser::SourceLocation location;
      VariableID id;
      ReferenceType type;
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

    bool is_variadic; // has args at the end
    unsigned int required_args; // number of positional args without defaults
    unsigned int optional_args; // number of positional args with defaults

    Scope scope;
    NamespaceID parent_namespace;

    struct Reference
    {
      Parser::SourceLocation location;
      ProcID id;
      ReferenceType type;
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
      ReferenceType type;
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

  void AddCommandReference( Index& index,
                            const Parser::SourceLocation& location,
                            const Proc& proc,
                            ReferenceType type )
  {
    index.procs.AddReference(
      Proc::Reference {
        .location = location,
        .id = proc.id,
        .type = type
      } );
  }

  template< typename WordVec >
  void AddProcToIndex( Index& index, Namespace& ns, const WordVec& words )
  {
    using Word = Parser::Word;
    // proc name { arg|{ arg default } ... } { body }
    auto qn = Parser::SplitName( words[ 1 ].text );

    auto proc = std::make_unique<Proc>();
    proc->name = qn.name;

    if ( words[ 2 ].type == Word::Type::LIST )
    {
      auto& vec = std::get< Word::WordVec >( words[ 2 ].data );
      proc->arguments.reserve( vec.size() );
      for ( auto it = vec.begin(); it != vec.end(); ++it )
      {
        auto& arg = *it;
        std::string argName;
        if ( arg.type == Word::Type::TEXT )
        {
          if ( argName == "args" && ( it + 1 ) == vec.end() )
          {
            proc->is_variadic = true;
          }
          else
          {
            ++proc->required_args;
          }

          argName = arg.text;
        }
        else
        {
          ++proc->optional_args;
          argName = std::get< Word::WordVec >( arg.data )[ 0 ].text;
        }
        auto& v = index.variables.Insert( new Variable{
          .name = std::move( argName ),
        } );
        proc->arguments.push_back( v.id );
        // TODO: Add reference with type ReferenceType::DEFINITION
      }
    }

    if ( qn.absolute || qn.ns )
    {
      auto& resolved = ResolveNamespace( index, qn, ns );
      resolved.scope.procs.push_back( proc->id );
      proc->parent_namespace = resolved.id;
    }
    else
    {
      ns.scope.procs.push_back( proc->id );
      proc->parent_namespace = ns.id;
    }

    auto& p = index.procs.Insert( std::move( proc ) );
    AddCommandReference( index,
                         words[ 1 ].location,
                         p,
                         ReferenceType::DEFINITION );
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

  std::vector<Proc*> FindProc( Index& index,
                               Namespace& ns,
                               std::string_view cmdName )
  {
    auto qn = Parser::SplitName( cmdName );
    Namespace::ID target_namespace = ns.id;

    auto range = index.procs.byName.equal_range( qn.name );

    if ( qn.absolute || qn.ns )
    {
      target_namespace = ResolveNamespace( index, qn, ns ).id;
    }

    std::vector<Proc*> result;
    result.reserve( std::distance( range.first, range.second ) );

    for ( auto it = range.first; it != range.second; ++it )
    {
      auto& p = index.procs.Get( it->second );
      if ( p.parent_namespace == target_namespace )
      {
        // NOTE: It's possible to have multiple definitions for the same proc.
        // for example:
        //
        // if { $x } {
        //   proc Proc {} {}
        // } else {
        //   proc Proc { x y z } {}
        // }
        //
        // By indexing everything on the proc's name and namespace, we always
        // pick whichever one is found first; so we actually use more semantic
        // information around the proc usage (e.g. number of args specified) to
        // determine the proc, by calling BestFitProcToCall
        result.push_back( &p );
      }
    }

    if ( result.empty() && !qn.absolute && ns.parent_namespace )
    {
      // Find in the parent namespace
      // FIXME: This recursion is extremely SUB-optimal. A loop would be much
      // faster
      return FindProc( index,
                       index.namespaces.Get( *ns.parent_namespace ),
                       cmdName );
    }

    return result;
  }

  Proc* BestFitProcToCall( std::vector<Proc*> procs,
                           const Parser::Call& call )
  {
    auto num_args = call.words.size() - 1;

    Proc* best_fit = nullptr;

    // TODO: Worlds shittiest overload resolution? This is really just
    // guessing (badly) based on the number of arguments.
    //
    // FIXME: best_fit is actualy last_fit
    for( auto* proc : procs )
    {
      if ( !best_fit )
      {
        best_fit = proc;
        continue;
      }

      if ( num_args < proc->required_args )
      {
        continue;
      }

      if ( num_args == proc->required_args )
      {
        best_fit = proc;
        break;
      }

      if ( num_args <= proc->required_args + proc->optional_args )
      {
        // This is a _good_ fit, but hard to know if it is _better_ than
        // what we have in best_fit right now.
        best_fit = proc;
        continue;
      }

      if ( proc->is_variadic )
      {
        best_fit = proc;
        continue;
      }
    }

    return best_fit;
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
          auto procs = FindProc( index, ns, call.words[ 0 ].text );
          auto* best_fit = BestFitProcToCall( procs, call );
          if ( best_fit )
          {
            // Add a reference to the proc being called if we can
            AddCommandReference( index,
                                 call.words[ 0 ].location,
                                 *best_fit,
                                 ReferenceType::USAGE );
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
    const Parser::Call* call{nullptr};
    size_t argument{0};
    const Parser::Word* word{nullptr};
  };

  ScriptCursor FindPositionInScript( const Parser::Script& script,
                                     Parser::LinePos pos );

  std::optional< ScriptCursor > FindPositionInWord( ScriptCursor result,
                                                    const Parser::Word& word,
                                                    Parser::LinePos pos )
  {
    if ( word.location.line > pos.line )
    {
      return std::nullopt;
    }
    else if ( word.location.line == pos.line &&
              word.location.column > pos.column )
    {
      return std::nullopt;
    }

    if ( word.type == Parser::Word::Type::SCRIPT )
    {
      result = FindPositionInScript(
        *std::get<Parser::Word::ScriptPtr>( word.data ),
        pos );
    }
    else if ( word.type == Parser::Word::Type::TOKEN_LIST ||
              word.type == Parser::Word::Type::EXPAND )
    {
        const auto& subWords = std::get< Parser::Word::WordVec >( word.data );
        for ( const auto& subWord : subWords )
        {
          auto subresult = FindPositionInWord( result, subWord, pos );
          if ( !subresult )
          {
            return std::nullopt;
          }
          result = *subresult;
        }
    }
    else
    {
      result.word = &word;
    }

    return result;
  }

  ScriptCursor FindPositionInScript( const Parser::Script& script,
    Parser::LinePos pos )
  {
    // TODO: binary chop the commands, which are neccesarily sorted by line
    // number
    std::optional< ScriptCursor > result = ScriptCursor{};

    for ( const auto& call : script.commands )
    {
      for ( size_t arg = 0; arg < call.words.size(); ++ arg )
      {
        const auto& word = call.words[ arg ];
        std::optional< ScriptCursor > subresult = ScriptCursor{
          .call = &call,
          .argument = arg,
        };
        subresult = FindPositionInWord( *subresult, word, pos );

        if ( !subresult )
        {
          // We've gone past the requested point. discard subresult and return
          return *result;
        }
        else
        {
          // We're still searching, use the result
          result = subresult;
        };
      }
    }
    return *result;
  }


}  // namespace Index

namespace Index::Test
{
  void Run()
  {
  }
}  // namespace Index::Test
