#include <algorithm>
#include <cassert>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <stdint.h>
#include <string>
#include <string_view>
#include <cstring>
#include <iostream>
#include <vector>
#include <filesystem>
#include <charconv>

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

  int completeAt_ = -1;
  const char *SCRIPT{};
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
              << '\n'
              << indent << "--\n";

    // The numComponents of a given token includes all sub-tokens (the whole
    // tree), so
    //    X
    //      Y
    //        Z
    // Has:
    //    X (numComponents 2)
    //       Y (numComponent 1)
    //          Z (numComponents 0)
    size_t maxToken = tokenIndex + token.numComponents;
    while( tokenIndex < maxToken )
    {
      printToken( tokenPtr, depth + 1, tokenIndex );
    }

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
    Tcl_Token &token = parseResult.tokenPtr[ tokenIndex ];
    if ( token.type == TCL_TOKEN_SIMPLE_WORD )
    {
      tokenIndex += token.numComponents;
      return std::string_view(parseResult.tokenPtr[ tokenIndex ].start,
                              parseResult.tokenPtr[ tokenIndex ].size);
    }
    else if ( token.type == TCL_TOKEN_WORD )
    {
      // Maybe somethng like 
      //   X Y \
      //     Z
      //
      // TODO: We just faff with the ranges of the included tokens. This is
      // clearly wrong (why? it seems to give the right result? Maybe we should
      // be checking for { and " and stuff here ?, or actually concatting the
      // individual token texts?).
      //
      // From instrument.tcl's switch parse:
      //
      // # If the body token contains backslash sequences, there will
      // # be more than one subtoken, so we take the range for the whole
      // # body and subtract the braces.  Otherwise it's a "simple" word
      // # with only one part and we can get the range from the text
      // # subtoken. 
      //
      // Also, from isLiteral:
      //
      // # The text contains backslash sequences.  Bail if the text is
      // # not in braces because this would require complicated substitutions.
      // # Braces are a special case because only \newline is interesting and
      // # this won't interfere with recursive parsing.
      //
      // So perhaps we should be checking for braces.
      //
      Tcl_Token &first = parseResult.tokenPtr[ tokenIndex + 1];
      tokenIndex += token.numComponents;
      Tcl_Token &last = parseResult.tokenPtr[ tokenIndex ];
      return std::string_view( first.start,
                               last.start+last.size - first.start );
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

    // TODO: Split this logic out to a namespace splitting method, which can
    // then be used by a findCommand.
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

    // code-complete pass ?
    if (completeAt_ >= 0)
    {
      // see if we requested completion within any of these tokens
      size_t idx = 1; // skip the token that reprsents the whole command
      const char * completePtr = SCRIPT + completeAt_;
      for (int wordIndex = 0;
           wordIndex < parseResult.numWords;
           ++wordIndex, ++idx)
      {
        Tcl_Token &token = parseResult.tokenPtr[ idx ];
        std::string_view text = parseLiteral( parseResult, idx );
        if ( token.start > completePtr )
        {
          // We;ve gone too far
          break;
        }
        else if ( token.start + token.size < completePtr )
        {
          // we've not gone far enough
          continue;
        }
        // otherwise, this _is_ the droid we are looking for
        std::string_view partialToken( token.start, completePtr - token.start );
        std::cout << "COMPLETE: Token is: " << partialToken << '\n';
        if ( wordIndex == 0 )
        {
          std::cout << " -- Complete command name\n";
        }
        else
        {
          // FIXME: This is probably not the best way to search for commands,
          // but it will do for now.
          auto pos = std::find_if( commands_.begin(),
                                   commands_.end(),
                                   [&]( Command& c ) {
                                     // TODO: thisCommand namespace qualifiers
                                     return c.name == thisCommand;
                                   } );
          if ( pos != commands_.end() )
          {
            const Command &c = *pos;
            std::cout << " -- Complete argument ("
                      << wordIndex - 1
                      << ", "
                      << c.args[ wordIndex - 1 ]
                      << ") of " << thisCommand << "\n" ;
          }
          else
          {
            std::cout << "Unknown command: " << thisCommand << "\n";
          }
        }
      }

      return;
    }

    // Oteherwise, build the index
    if ( thisCommand == "proc" && parseResult.numWords == 4)
    {
      parseProc(interp, parseResult, tokenIndex, ns);
    }
    else if ( thisCommand == "namespace" )
    {
      if ( parseResult.numWords > 2 )
      {
        auto arg = parseLiteral( parseResult, ++tokenIndex );
        if (arg.empty())
        {
          return;
        }

        if (arg == "eval")
        {
          std::string new_ns( parseLiteral( parseResult, ++tokenIndex ) );
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
    // etc.

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
        // to to see where we are. Indeed it looks like parseResult is
        // populated, at least to a point
        std::cerr << "ERROR RECOVERY\n";
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
  const char * LONG_SCRIPT = R"(
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
      ${X}
      $X(\$Y + 1, 2) 1 \
                     2 \
                     [$3]
    }

    $X 1\
       2\
       [3 4 5]

    )";

  const char * SHORT_SCRIPT = R"(
    proc Test::Abort {} {
      $X($Y) 1\
             2\
             [3]
    }
  )";

  const char *INCOMPLETE = "test [X";

  int completeAt = -1;

  SCRIPT = LONG_SCRIPT;
  std::string input;
  auto shift = [&]() { ++argv, --argc; };
  shift();
  while ( argc > 0 )
  {
    std::string_view arg( argv[ 0 ] );
    if ( arg == "--short" )
    {
      SCRIPT = SHORT_SCRIPT;
      shift();
    }
    else if ( arg == "--stdin" )
    {
      // c++ is just fucking terrible
      std::cin >> std::noskipws;
      std::istream_iterator<char> begin( std::cin );
      std::istream_iterator<char> end;
      input = std::string( begin, end );
      SCRIPT = input.c_str();
      shift();
    }
    else if ( arg == "--file" )
    {
      shift();
      arg = argv[ 0 ];
      std::ifstream f( arg );
      if (!f)
      {
        std::cerr << "Unable to read file: " << arg << '\n';
        return 1;
      }
      f >> std::noskipws; // fucking hell
      std::istream_iterator<char> begin( f );
      std::istream_iterator<char> end;
      input = std::string( begin, end );
      SCRIPT = input.c_str();

      shift();
    }
    else if ( arg == "--string" )
    {
      shift();
      SCRIPT = argv[ 0 ];
      shift();
    }
    else if ( arg == "--incomplete" )
    {
      SCRIPT = INCOMPLETE;
      shift();
    }
    else if ( arg == "--codeCompleteAt" )
    {
      shift();
      arg = argv[ 0 ];
      if ( auto [ p, ec ] = std::from_chars( arg.data(),
                                             arg.data() + arg.length(),
                                             completeAt );
           ec != std::errc() )
      {
        std::cerr << "Invalid offset: " << arg << '\n';
        return 1;
      }
      shift();
    }
  }

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

  std::cout << "Parsing SCRIPT:\n" << SCRIPT << std::endl;

  // parse commands refs/etc.
  parseScript( interp, SCRIPT, strlen( SCRIPT ), "" );

  // code complete - (TODO: error is much more likely as we only
  // re-parse up to the complete offset, not the whole document; this is
  // intentional, but we haven't fixed up the ERROR RECOVERY code yet)
  if (completeAt >= 0)
  {
    completeAt_ = completeAt;
    parseScript( interp, SCRIPT, completeAt, "" );
  }

  Tcl_DeleteInterp(interp);

  return 0;
}
