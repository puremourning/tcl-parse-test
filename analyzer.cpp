#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <optional>
#include <variant>

// Naughty
#include <tcl.h>
#include "tclDecls.h"

#include "source_location.cpp"
#include "script.cpp"

namespace Parser
{
  struct QualifiedName
  {
    std::string ns;
    std::string name;
  };

  struct Command
  {
    SourceLocation commentLocation;
    SourceLocation location;

    std::string documenation;
    QualifiedName command;
    std::vector< Word > args;
  };
}  // namespace Parser

namespace Parser::Test
{
  void Run()
  {
    TestOffsetToLineByte();
    TestWord();
  }
}

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
        std::ifstream f( arg );
        if ( !f )
        {
          std::cerr << "Unable to read file: " << arg << '\n';
          return 1;
        }
        f >> std::noskipws;  // fucking hell
        std::istream_iterator< char > begin( f );
        std::istream_iterator< char > end;
        mainFile = Parser::make_source_file( arg, { begin, end } );
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

  if (  mainFile.fileName.empty() )
  {
    std::cerr << "No script supplied\n";
    return 1;
  }

  Parser::ParseContext context { mainFile, "" };
  auto script = Parser::ParseScript( interp, context, context.file.contents );

  // script contains only the basic sctructure of the script. No semantics have
  // been applied. So for example, it doesn't know thatt `proc x y z` - z is a
  // script and, say `y` is a list of arguments.

  // Steps:
  //
  // 0. Parse the script into a first-level vector of Calls (done)
  //
  // 1. Find scripts - scan through the list of Calls and find Words which are
  //    really scripts (e.g. the bodies of functions), and parse them into the
  //    tree.
  //
  //    TODO: Should we have a new type of Call here. E.g. for a proc, replace
  //    the Call with a Proc. Perhaps Call could be a vector of variants of
  //    known "Call" types
  //
  // 2. Discover namespaces, procs, scopes and variables.
  //    Walk the tree and index all the namespaces, procedures and scopes
  //    defined by the known scope-commads (e.g proc, namespace eval, upleel)
  //
  // 3. Calculate references. Link together commands and calls. Link variables
  //    usages to scopes and variable instaces
  //

  Tcl_DeleteInterp( interp );

  return 0;
}
