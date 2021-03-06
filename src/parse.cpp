#include <algorithm>
#include <cassert>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits.h>
#include <sstream>
#include <stdint.h>
#include <string>
#include <string_view>
#include <cstring>
#include <iostream>
#include <vector>
#include <filesystem>
#include <charconv>
#include <unordered_map>

// Naughty
#include <tcl.h>
#include <tclInt.h>
#include <tclParse.h>
#include <tclDecls.h>
#include "tclIntDecls.h"

namespace
{
  void parseScript( Tcl_Interp* interp,
                    const char* script,
                    int numBytes,
                    std::string ns );

  const char* SCRIPT{};
}  // namespace

namespace
{
  struct QualifiedName
  {
    std::string ns;
    std::string name;
  };

  std::unordered_map< int, std::string_view > TOKEN_TYPE = {
    { TCL_TOKEN_WORD, "TCL_TOKEN_WORD" },
    { TCL_TOKEN_SIMPLE_WORD, "TCL_TOKEN_SIMPLE_WORD" },
    { TCL_TOKEN_TEXT, "TCL_TOKEN_TEXT" },
    { TCL_TOKEN_BS, "TCL_TOKEN_BS" },
    { TCL_TOKEN_COMMAND, "TCL_TOKEN_COMMAND" },
    { TCL_TOKEN_VARIABLE, "TCL_TOKEN_VARIABLE" },
    { TCL_TOKEN_SUB_EXPR, "TCL_TOKEN_SUB_EXPR" },
    { TCL_TOKEN_OPERATOR, "TCL_TOKEN_OPERATOR" },
    { TCL_TOKEN_EXPAND_WORD, "TCL_TOKEN_EXPAND_WORD" },
  };

  void printToken( Tcl_Token* tokenPtr, int depth, size_t& tokenIndex )
  {
    Tcl_Token& token = tokenPtr[ tokenIndex++ ];
    std::string indent = std::string( depth * 2, ' ' );
    std::cout << indent << "Index: " << tokenIndex << "(+"
              << token.numComponents << ")\n"
              << indent << "Type: " << token.type << "("
              << TOKEN_TYPE[ token.type ] << ")" << '\n'
              << indent
              << "Text: " << std::string_view( token.start, token.size ) << '\n'
              << indent << "--\n";

    // The numComponents of a given token includes all sub-tokens (the whole
    // tree), so
    //    X
    //      Y
    //        Z
    //      A
    //        B
    // Has:
    //    X (numComponents 4)
    //       Y (numComponent 1)
    //          Z (numComponents 0)
    //       A (numComponent 1)
    //          B (numComponents 0)
    size_t maxToken = tokenIndex + token.numComponents;
    while ( tokenIndex < maxToken )
    {
      printToken( tokenPtr, depth + 1, tokenIndex );
    }
  }

  void printCommandTree( Tcl_Parse& parseResult, size_t commandLen )
  {
    std::string_view command( parseResult.commandStart, commandLen );
    std::cout << "COMMAND: " << command << '\n';

    for ( size_t tokenIndex = 0; tokenIndex < (size_t)parseResult.numTokens; )
    {
      printToken( parseResult.tokenPtr, 1, tokenIndex );
    }
  }

  std::string_view parseWord( Tcl_Parse& parseResult, size_t& tokenIndex )
  {
    /*
     * Type values defined for Tcl_Token structures. These values are defined as
     * mask bits so that it's easy to check for collections of types.
     *
     * TCL_TOKEN_WORD -		The token describes one word of a command,
     *				from the first non-blank character of the word
     *				(which may be " or {) up to but not including
     *				the space, semicolon, or bracket that
     *				terminates the word. NumComponents counts the
     *				total number of sub-tokens that make up the
     *				word. This includes, for example, sub-tokens
     *				of TCL_TOKEN_VARIABLE tokens.
     * TCL_TOKEN_SIMPLE_WORD -	This token is just like TCL_TOKEN_WORD except
     *				that the word is guaranteed to consist of a
     *				single TCL_TOKEN_TEXT sub-token.
     * TCL_TOKEN_TEXT -		The token describes a range of literal text
     *				that is part of a word. NumComponents is
     *				always 0.
     * TCL_TOKEN_BS -		The token describes a backslash sequence that
     *				must be collapsed. NumComponents is always 0.
     * TCL_TOKEN_COMMAND -		The token describes a command whose result
     *				must be substituted into the word. The token
     *				includes the enclosing brackets. NumComponents
     *				is always 0.
     * TCL_TOKEN_VARIABLE -		The token describes a variable substitution,
     *				including the dollar sign, variable name, and
     *				array index (if there is one) up through the
     *				right parentheses. NumComponents tells how
     *				many additional tokens follow to represent the
     *				variable name. The first token will be a
     *				TCL_TOKEN_TEXT token that describes the
     *				variable name. If the variable is an array
     *				reference then there will be one or more
     *				additional tokens, of type TCL_TOKEN_TEXT,
     *				TCL_TOKEN_BS, TCL_TOKEN_COMMAND, and
     *				TCL_TOKEN_VARIABLE, that describe the array
     *				index; numComponents counts the total number
     *				of nested tokens that make up the variable
     *				reference, including sub-tokens of
     *				TCL_TOKEN_VARIABLE tokens.
     * TCL_TOKEN_SUB_EXPR -		The token describes one subexpression of an
     *				expression, from the first non-blank character
     *				of the subexpression up to but not including
     *				the space, brace, or bracket that terminates
     *				the subexpression. NumComponents counts the
     *				total number of following subtokens that make
     *				up the subexpression; this includes all
     *				subtokens for any nested TCL_TOKEN_SUB_EXPR
     *				tokens. For example, a numeric value used as a
     *				primitive operand is described by a
     *				TCL_TOKEN_SUB_EXPR token followed by a
     *				TCL_TOKEN_TEXT token. A binary subexpression
     *				is described by a TCL_TOKEN_SUB_EXPR token
     *				followed by the TCL_TOKEN_OPERATOR token for
     *				the operator, then TCL_TOKEN_SUB_EXPR tokens
     *				for the left then the right operands.
     * TCL_TOKEN_OPERATOR -		The token describes one expression operator.
     *				An operator might be the name of a math
     *				function such as "abs". A TCL_TOKEN_OPERATOR
     *				token is always preceeded by one
     *				TCL_TOKEN_SUB_EXPR token for the operator's
     *				subexpression, and is followed by zero or more
     *				TCL_TOKEN_SUB_EXPR tokens for the operator's
     *				operands. NumComponents is always 0.
     * TCL_TOKEN_EXPAND_WORD -	This token is just like TCL_TOKEN_WORD except
     *				that it marks a word that began with the
     *				literal character prefix "{*}". This word is
     *				marked to be expanded - that is, broken into
     *				words after substitution is complete.
     */

    Tcl_Token& token = parseResult.tokenPtr[ tokenIndex ];
    if ( token.type == TCL_TOKEN_SIMPLE_WORD )
    {
      tokenIndex += token.numComponents;
      return std::string_view( parseResult.tokenPtr[ tokenIndex ].start,
                               parseResult.tokenPtr[ tokenIndex ].size );
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
      Tcl_Token& first = parseResult.tokenPtr[ tokenIndex + 1 ];
      tokenIndex += token.numComponents;
      Tcl_Token& last = parseResult.tokenPtr[ tokenIndex ];
      // TODO: We should recursively call parseScript if any token is of type
      // TCL_TOKEN_COMMAND
      return std::string_view( first.start,
                               last.start + last.size - first.start );
    }
    return "";
  }

  // TODO: replace with Tcl_SplitList ?
  // i wnder why does tclParser.c use this approach ? (because the string we
  // have isn't NULL terminated)
  std::vector< std::string_view > parseList( Tcl_Interp* interp,
                                             const char* start,
                                             size_t length )
  {
    std::vector< std::string_view > elements;
    const char* list = start;
    const char* prevList = nullptr;
    const char* last = list + length;
    int size;
    const char* element;

    while ( true )
    {
      prevList = list;
      if ( TclFindElement( interp,
                           list,
                           length,
                           &element,
                           &list,
                           &size,
                           NULL ) != TCL_OK )
      {
        return elements;
      }
      length -= ( list - prevList );
      if ( element >= last )
      {
        break;
      }
      elements.emplace_back( element, size );
    }

    return elements;
  }

  template< typename QN, typename S1, typename S2 >
  void splitName( QN& qn, S1 ns, S2 name )
  {
    // If name starts with :: it's absolute and `ns` is ignored
    // Otherwise we concatenate anything up to the last :: to `ns`
    // name is always everything after the last ::

    auto pos = name.rfind( "::" );
    if ( pos == decltype( name )::npos )
    {
      qn.name = name;
      qn.ns = ns;
    }
    else if ( ns.empty() ||
              ( name.length() > 2 && name.substr( 0, 2 ) == "::" ) )
    {
      qn.name = name.substr( pos + 2 );
      qn.ns = name.substr( 0, pos );
    }
    else
    {
      qn.name = name.substr( pos + 2 );
      qn.ns = ns;
      qn.ns += "::";
      qn.ns += name.substr( 0, pos );
    }
  }

  void parseProc( Tcl_Interp* interp,
                  Tcl_Parse& parseResult,
                  size_t& tokenIndex,
                  std::string ns )
  {
    // proc [namespace::]NAME SPEC:list BODY

    if ( parseResult.numWords != 4 )
    {
      return;
    }

    /**
    Command c;
    if ( parseResult.commentSize > 0 )
    {
      std::string_view comment( parseResult.commentStart,
                                parseResult.commentSize - 1 );
      c.documenation = comment;
    }
    */

    // TODO: strip any namespace qualifier and append to ns.
    // TODO: if name is fully qualified, ignore ns
    QualifiedName qn;
    auto name = parseWord( parseResult, ++tokenIndex );
    splitName( qn, ns, name );

    auto args = parseWord( parseResult, ++tokenIndex );
    auto listTokens = parseList( interp, args.data(), args.length() );
    //for ( auto token : listTokens ) {
    //  // Args is strictly a list of lists with default args
    //  auto argTokens = parseList( interp, token.data(), token.length() );
    //  if ( argTokens.size() == 1 )
    //  {
    //    c.args.emplace_back( std::string_view( token.data(),
    //                                           token.length() ) );
    //  }
    //  else if ( argTokens.size() == 2 )
    //  {
    //    std::ostringstream arg;
    //    arg << std::string_view( argTokens[ 0 ].data(),
    //                             argTokens[ 0 ].length() )
    //        << " ["
    //        << std::string_view( argTokens[ 1 ].data(),
    //                             argTokens[ 1 ].length() )
    //        << ']';
    //    c.args.push_back( arg.str() );
    //  }
    //  else
    //  {
    //    // emit diagnostic ?
    //    return;
    //  }
    //}

    //commands_.push_back( c );

    //if ( DEBUG )
    //{
    //  std::cout << " Indexed: \n"
    //            << "   Command: " << c.ns << "::" << c.name << '\n'
    //            << "   Doc: " << c.documenation << '\n'
    //            << "   Args: ";
    //  if (c.args.size() > 0)
    //  {
    //    std::copy( c.args.begin(),
    //               c.args.end() - 1,
    //               std::ostream_iterator< std::string >( std::cout, "," ) );
    //    std::cout << *c.args.rbegin();
    //  }
    //  std::cout << '\n';
    //}

    auto body = parseWord( parseResult, ++tokenIndex );
    parseScript( interp, body.data(), body.length(), qn.ns );
  }

  void parseCommand( Tcl_Interp* interp,
                     Tcl_Parse& parseResult,
                     std::string ns )
  {
    if ( parseResult.numWords < 1 )
    {
      return;
    }

    size_t tokenIndex = 0;
    auto thisCommand = parseWord( parseResult, tokenIndex );
    if ( thisCommand.empty() )
    {
      return;
    }

    // Oteherwise, build the index
    if ( thisCommand == "proc" && parseResult.numWords == 4 )
    {
      parseProc( interp, parseResult, tokenIndex, ns );
    }
    else if ( thisCommand == "namespace" )
    {
      if ( parseResult.numWords > 2 )
      {
        auto arg = parseWord( parseResult, ++tokenIndex );
        if ( arg.empty() )
        {
          return;
        }

        if ( arg == "eval" )
        {
          std::string new_ns( parseWord( parseResult, ++tokenIndex ) );
          if ( new_ns.empty() )
          {
            return;
          }

          ++tokenIndex;  // TCL_TOKEN_WORD ?
          ++tokenIndex;  // TCL_TOKEN_TEXT ?
          parseScript( interp,
                       parseResult.tokenPtr[ tokenIndex ].start,
                       parseResult.tokenPtr[ tokenIndex ].size,
                       new_ns );
        }
        // inscope ?
        // others?
      }
    }
    else if ( thisCommand == "set" )
    {
    }
    else if ( thisCommand == "variable" )
    {
    }
    // rename ?
    // unset ?
    // array
    // uplevel
    // upvar
    // interp?
    // foreach
    else if ( thisCommand == "foreach" )
    {
      // foreach
      parseWord( parseResult, ++tokenIndex );

      // names
      parseWord( parseResult, ++tokenIndex );

      // list
      parseWord( parseResult, ++tokenIndex );

      // { body }
      std::cout << "Parsing foreach body\n";
      parseScript( interp,
                   parseResult.tokenPtr[ tokenIndex ].start,
                   parseResult.tokenPtr[ tokenIndex ].size,
                   ns );
    }
    else if ( thisCommand == "if" )
    {
      // ugh, complex
    }
    // etc.
  }

  void parseScript( Tcl_Interp* interp,
                    const char* script,
                    int numBytes,
                    std::string ns )
  {
    Tcl_Parse parseResult;
    while ( numBytes > 0 )
    {
      if ( Tcl_ParseCommand( interp, script, numBytes, 0, &parseResult ) !=
           TCL_OK )
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
        // Advance to the next thing that looks like the end of a command and
        // see if we find a command _afer_ this one
        while ( ( ++script, --numBytes ) &&
                !( CHAR_TYPE( *script ) & TYPE_COMMAND_END ) )
        {
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
          if ( parseResult.term <= script + numBytes )
          {
            --commandLen;
          }

          printCommandTree( parseResult, commandLen );
          parseCommand( interp, parseResult, ns );
        }

        const char* end = script + numBytes;
        script = parseResult.commandStart + parseResult.commandSize;
        numBytes = end - script;

        Tcl_FreeParse( &parseResult );
      }
    }
  }
}  // namespace

int main( int argc, char** argv )
{
  const char* LONG_SCRIPT = R"(
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

    set y [proc DoesThisProcGetCreated {} {}; expr {""}]

    )";

  const char* SHORT_SCRIPT = R"(
    proc Test::Abort {} {
      $X($Y) 1\
             2\
             [3]
    }
  )";

  const char* INCOMPLETE = "test [X";

  SCRIPT = LONG_SCRIPT;
  std::string input;
  auto shift = [ & ]() { ++argv, --argc; };
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
      std::istream_iterator< char > begin( std::cin );
      std::istream_iterator< char > end;
      input = std::string( begin, end );
      SCRIPT = input.c_str();
      shift();
    }
    else if ( arg == "--file" )
    {
      shift();
      arg = argv[ 0 ];
      std::ifstream f( arg );
      if ( !f )
      {
        std::cerr << "Unable to read file: " << arg << '\n';
        return 1;
      }
      f >> std::noskipws;  // fucking hell
      std::istream_iterator< char > begin( f );
      std::istream_iterator< char > end;
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
    else
    {
      std::cerr << "Unrecognised argument: " << arg << "\n";
      return 1;
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
  Tcl_Interp* interp = Tcl_CreateInterp();

  // parse commands refs/etc.
  parseScript( interp, SCRIPT, strlen( SCRIPT ), "" );

  Tcl_DeleteInterp( interp );

  return 0;
}
