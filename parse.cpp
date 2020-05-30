#include <cassert>
#include <cstddef>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <string>
#include <string_view>
#include <cstring>
#include <iostream>
#include <vector>
#include <filesystem>

// Naughty
#include <tcl.h>
#include <tclInt.h>
#include <tclParse.h>
#include <tclDecls.h>
#include "tclIntDecls.h"

namespace
{
  void parseScript( Tcl_Interp* interp,
                    const char *script,
                    int numBytes,
                    std::string ns );
}

namespace
{
  struct Command
  {
    std::string ns;
    std::string documenation;
    std::string name;
    std::vector<std::string> args;
  };
  std::vector<Command> commands_;

  void printToken( Tcl_Token *tokenPtr, int depth, size_t& tokenIndex )
  {
    Tcl_Token &token = tokenPtr[ tokenIndex++ ];
    std::string indent = std::string( depth * 2, ' ' );
    std::cout << indent << "Index: " << tokenIndex
                << "(+" << token.numComponents << ")\n"
              << indent << "Type: " << token.type << '\n'
              << indent << "Text: " << std::string_view( token.start,
                                                         token.size )
              << '\n';

    size_t maxToken = tokenIndex + token.numComponents;
    while( tokenIndex < maxToken )
    {
      printToken( tokenPtr, depth + 1, tokenIndex );
    }

    std::cout << indent << "<-\n";
  }

  void printCommandTree( Tcl_Parse& parseResult, size_t commandLen )
  {
    std::string_view command( parseResult.commandStart, commandLen );
    std::cout << "COMMAND: " << command << '\n';

    for( size_t tokenIndex = 0; tokenIndex < parseResult.numTokens; )
    {
      printToken( parseResult.tokenPtr, 1, tokenIndex );
    }
  }

  std::string_view parseLiteral( Tcl_Parse& parseResult, size_t& tokenIndex )
  {
    if ( parseResult.tokenPtr[ tokenIndex ].type == TCL_TOKEN_SIMPLE_WORD )
    {
      ++tokenIndex;
      return std::string_view(parseResult.tokenPtr[ tokenIndex ].start,
                              parseResult.tokenPtr[ tokenIndex ].size);
    }
    return "";
  }

  // TODO: replace with Tcl_SplitList ?
  // i wnder why does tclParser.c use this approach ? (because the string we
  // have isn't NULL terminated)
  std::vector<std::string_view> parseList( Tcl_Interp* interp,
                                           const char *start,
                                           size_t length )
  {
    std::vector<std::string_view> elements;
    const char *list = start;
    const char *prevList = nullptr;
    const char *last = list + length;
    size_t size;
    const char *element;

    while( true )
    {
      prevList = list;
      if (TclFindElement( interp,
                          list,
                          length,
                          &element,
                          &list,
                          &size,
                          NULL ) != TCL_OK)
      {
        return elements;
      }
      length -= (list - prevList);
      if (element >= last)
      {
        break;
      }
      elements.emplace_back( element, size );
    }

    return elements;
  }

  void parseProc( Tcl_Interp* interp,
                  Tcl_Parse& parseResult,
                  size_t& tokenIndex,
                  std::string ns )
  {
    // proc [namespace::]NAME SPEC:list BODY

    if (parseResult.numWords != 4)
    {
      return;
    }

    Command c;
    if ( parseResult.commentSize > 0 )
    {
      std::string_view comment( parseResult.commentStart,
                                parseResult.commentSize - 1 );
      c.documenation = comment;
    }

    // TODO: strip any namespace qualifier and append to ns.
    // TODO: if name is fully qualified, ignore ns
    auto name = parseLiteral( parseResult, ++tokenIndex );

    // If name starts with :: it's absolute and `ns` is ignored
    // Otherwise we concatenate anything up to the last :: to `ns`
    // name is always everything after the last ::

    auto pos = name.rfind( "::" );
    if ( pos == decltype(name)::npos )
    {
      c.name = name;
      c.ns = ns;
    }
    else if ( ns.empty() ||
              ( name.length() > 2 && name.substr( 0, 2 ) == "::" ) )
    {
      c.name = name.substr( pos + 2 );
      c.ns = name.substr( 0, pos );
    }
    else
    {
      c.name = name.substr( pos + 2 );
      c.ns = ns;
      c.ns += "::";
      c.ns += name.substr( 0, pos );
    }

    auto args = parseLiteral( parseResult, ++tokenIndex );
    auto listTokens = parseList( interp, args.data(), args.length() );
    for ( auto token : listTokens ) {
      // Args is strictly a list of lists with default args
      auto argTokens = parseList( interp, token.data(), token.length() );
      if ( argTokens.size() == 1 )
      {
        c.args.emplace_back( std::string_view( token.data(),
                                               token.length() ) );
      }
      else if ( argTokens.size() == 2 )
      {
        std::ostringstream arg;
        arg << std::string_view( argTokens[ 0 ].data(),
                                 argTokens[ 0 ].length() )
            << " ["
            << std::string_view( argTokens[ 1 ].data(),
                                 argTokens[ 1 ].length() )
            << ']';
        c.args.push_back( arg.str() );
      }
      else
      {
        // emit diagnostic ?
        return;
      }
    }

    commands_.push_back( c );

    std::cout << " Indexed: \n"
              << "   Command: " << c.ns << "::" << c.name << '\n'
              << "   Doc: " << c.documenation << '\n'
              << "   Args: ";
    std::copy( c.args.begin(),
               c.args.end(),
               std::ostream_iterator< std::string >( std::cout, "," ) );
    std::cout << '\n';

    auto body = parseLiteral( parseResult, ++tokenIndex );
    parseScript( interp, body.data(), body.length(), c.ns );
  }

  void parseCommand( Tcl_Interp* interp,
                     Tcl_Parse& parseResult,
                     std::string ns )
  {
    if (parseResult.numWords < 1)
    {
      return;
    }

    size_t tokenIndex = 0;
    auto thisCommand = parseLiteral( parseResult, tokenIndex );
    if (thisCommand.empty())
    {
      return;
    }

    // Building the index
    if ( thisCommand == "proc" && parseResult.numWords == 4)
    {
      parseProc(interp, parseResult, tokenIndex, ns);
    }
    else if ( thisCommand == "namespace" )
    {
      if ( parseResult.numWords > 2 )
      {
        ++tokenIndex;
        auto arg = parseLiteral( parseResult, tokenIndex );
        if (arg.empty())
        {
          return;
        }

        if (arg == "eval")
        {
          ++tokenIndex;
          std::string new_ns( parseLiteral( parseResult, tokenIndex ) );
          if (new_ns.empty())
          {
            return;
          }

          ++tokenIndex; // TCL_TOKEN_WORD ?
          ++tokenIndex; // TCL_TOKEN_TEXT ?
          parseScript( interp,
                       parseResult.tokenPtr[ tokenIndex ].start,
                       parseResult.tokenPtr[ tokenIndex ].size,
                       new_ns );
        }
      }
    }
    else if ( thisCommand == "set" )
    {

    }
    else if ( thisCommand == "variable" )
    {

    }
  }

  void parseScript( Tcl_Interp* interp,
                    const char *script,
                    int numBytes,
                    std::string ns )
  {
    Tcl_Parse parseResult;
    while (numBytes > 0)
    {
      if (Tcl_ParseCommand( interp,
                            script,
                            numBytes,
                            0,
                            &parseResult ) != TCL_OK )
      {
        // ERROR RECOVERY.
        //
        // TODO ideas:
        //  - Advance script until the start of a new line, ; or something
        //  - Advance until the next non-normal character? This is wrong more
        //    than it is right maybe.
        //
        // I guess an indexer and a complter want different things. The indexer
        // wants to know that it is getting a _real_ command, whereas the
        // completer needs to strictly know _where_ in an incomplete command the
        // location in question is.
        //
        // I wonder if there is any way to work out the state of the parser at
        // the time of error.. does that help?
        //
        // I guess the states are either:
        //  - completing a command _name_, or
        //  - completing a command argument
        //
        // We'd need to know how much of a command it parsed, and where it got
        // to to see where we are.
        while ( (++script, --numBytes) &&
                !(CHAR_TYPE(*script) & TYPE_COMMAND_END) ) {
          // advance
        }
      }
      else
      {
        if ( parseResult.commandSize > 0 )
        {
          // The returned command range includes the terminating character,
          // which we don't really care about. So we strip it. The _only_ way to
          // tell if this was included (and didn't hit the end of string) is to
          // use the internal 'term' item.
          // See https://wiki.tcl-lang.org/page/Tcl_ParseCommand
          int commandLen = parseResult.commandSize;
          if ( parseResult.term <= script + numBytes ) {
            --commandLen;
          }

          printCommandTree( parseResult, commandLen );

          parseCommand( interp, parseResult, ns );
        }

        const char *end = script + numBytes;
        script = parseResult.commandStart + parseResult.commandSize;
        numBytes = end - script;

        Tcl_FreeParse(&parseResult);
      }
    }
  }
}

int main( int argc, char ** argv )
{

  const char * SCRIPT = R"(
    # Comment
    proc Test { a b
               { c
                 def } } {
      puts "This is a command"
      set cmd "Test"
      puts "And [$cmd test]"
    }

    Test test test test

    # A proc which defines another proc
    proc ProcInAProc {} {
      # Parc life
      #    Just another
      # Mark life
      proc Parc { a {b ""} args } {
        puts "test"
      }
    }

    proc {Name with Space} {} {}
    proc {Name} [list a b c] {}
    proc Name "single arguemnt?" {}

    This is [$a test

    if { $the

    if { $the logic } {
      is working

    This should work
    \}

    Of the recovery\
           logic

    namespace eval Test {
      proc Toast { } {
      }
      proc ::Tasty {} {}

      proc XYZ::Testing {} {
        proc This_Should_Be_In_XYZ_Right {} {}
      }
    }

    proc ::Toast {} {}
    proc Test::Abort {} {
      set X "test"
      set Y [$X eatpies {*}$X]
      set Z {*}$Y
      set A $X
    }

    )";

  const char * SCRIPT2 = R"(
    proc Test::Abort {} {
      set X "test"
      set Y [$X eatpies {*}$X]
      set Z {*}$Y
      set A $X
    }
  )";

  // TODO: Given a position in SCRIPT, find the closest start-of command, going
  // _backwards_, then parse forwards to see:
  //   - are we in the middle of a command
  //   - what is the command name
  //   - how many words have we seen (i.e. which argument)
  //   - is the command complete

  // Maybe scan backwards for TYPE_COMMAND_END, TYPE_SUBS_END
  // Or maybe we rely on an index to tell us the nearest command start ?

  Tcl_FindExecutable( argv[ 0 ] );
  Tcl_Interp *interp = Tcl_CreateInterp();

  // recursive parser
  parseScript( interp, SCRIPT2, strlen( SCRIPT2 ), "" );

  Tcl_DeleteInterp(interp);

  return 0;
}
