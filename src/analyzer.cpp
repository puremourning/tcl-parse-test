#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <optional>
#include <variant>
#include <iterator>

// Naughty
#include <tcl.h>
#include "tclDecls.h"
#include "tclInt.h"

#include <analyzer/source_location.cpp>
#include <analyzer/script.cpp>
#include <analyzer/db.cpp>
#include <analyzer/index.cpp>

int main( int argc, char** argv )
{
  Tcl_FindExecutable( argv[ 0 ] );
  Tcl_Interp* interp = Tcl_CreateInterp();

  Parser::SourceFile mainFile;

  auto shift = [ & ]() { ++argv, --argc; };
  shift();
  while ( argc > 0 )
  {
    std::string_view arg( argv[ 0 ] );
    if ( arg == "--test" )
    {
      shift();
      Parser::Test::Run();
      Index::Test::Run();
      return 0;
    }
    else if ( arg == "--file" )
    {
      shift();
      arg = argv[ 0 ];
      if ( arg == "-" )
      {
        // c++ is just fucking terrible
        std::cin >> std::noskipws;
        std::istream_iterator< char > begin( std::cin );
        std::istream_iterator< char > end;
        mainFile = Parser::make_source_file( "stdin", { begin, end } );
      }
      else
      {
        std::ifstream f{ std::string( arg ) };
        if ( !f )
        {
          std::cerr << "Unable to read file: " << arg << '\n';
          return 1;
        }
        f >> std::noskipws;  // fucking hell
        std::istream_iterator< char > begin( f );
        std::istream_iterator< char > end;
        mainFile = Parser::make_source_file( std::string( arg ),
                                             { begin, end } );
      }

      shift();
    }
    else if ( arg == "--string" )
    {
      shift();
      mainFile = Parser::make_source_file( "cmdline", argv[ 0 ] );
      shift();
    }
    else
    {
      std::cerr << "Unrecognised argument: " << arg << "\n";
      return 1;
    }
  }

  if ( mainFile.fileName.empty() )
  {
    std::cerr << "No script supplied\n";
    return 1;
  }

  Parser::ParseContext context{
    .file = std::move( mainFile ),
    .cur_ns = ""
  };
  auto script = Parser::ParseScript( interp, context, context.file.contents );

  // Smenatics to add the tree:
  //
  // 0. Parse the script into a first-level vector of Calls (done)
  //
  // 1. Find scripts - scan through the list of Calls and find Words which are
  //    really scripts (e.g. the bodies of functions), and parse them into the
  //    tree. (somewhat in progress)
  //
  //    TODO: Should we have a new type of Call here. E.g. for a proc, replace
  //    the Call with a Proc. Perhaps Call could be a vector of variants of
  //    known "Call" types. I think it's going to depend on what we want the
  //    analyzer to work with. I'm thining that he analyzer should be (as far as
  //    possible) absract, and just see "Script", "Expression" etc. and their
  //    attached scopes.
  //
  // 1b. Find expressions. ? These will be like the if { expr }
  // 1c. Find declarations. ? Thes willbe like x y z in for { x y z } ...
  // 1d. Interesting scripts (like `after 10 uplevel 1 foreach x $y { puts $x
  // }`)
  //
  // 1e. Find packages. Try and resolve `package require`:
  //   - attempt to apply trivial updates to `auto_path` ?
  //   - require a set of paths to be supplied, e.g. via TCLLIBPATH (or ask the
  //     empbedded interp to find the package)?
  //   - queue for indexing any packages discovered
  //   - do we need to do this indexing first before 1b to 1d ? or before 2 ?
  //
  // 2. Discover namespaces, procs, scopes and variables.
  //    Walk the tree and index all the namespaces, procedures and scopes
  //    defined by the known scope-commads (e.g proc, namespace eval, upleel).
  //    Attach scopes to nodes in the tree of type Script or Expression
  //
  // 3. Calculate references. Link together commands and calls. Link variables
  //    usages to scopes and variable instaces
  //
  // 4. Coroutines... eh.
  //


  // TODO: pass the index to the parser, and have the parser inject found procs?
  //       or maybe it really is better to just scan twice: once to discover and
  //       once to find references
  Index::Index index = Index::make_index();
  Index::ScanContext scanContext{ .nsPath = { index.global_namespace_id } };
  Index::Build( index, scanContext, script );

  for ( auto& kv : index.namespaces.byName )
  {
    std::cout << "Namespace: "
              << Index::GetPrintName( index, index.namespaces.Get( kv.second ) )
              << '\n';
  }

  for ( auto& kv : index.procs.byName )
  {
    std::cout << "Proc: "
              << Index::GetPrintName( index, index.procs.Get( kv.second ) )
              << '\n';

    auto range = index.procs.refsByID.equal_range( kv.second );
    for ( auto it = range.first; it != range.second; ++it )
    {
      auto& r = index.procs.references[ it->second ];
      std::cout << "  " << r->type << " Ref: "
                << Index::GetPrintName( index, index.procs.Get( r->id ) )
                << " at " << r->location
                << '\n';
    }
  }


  Tcl_DeleteInterp( interp );

  return 0;
}
