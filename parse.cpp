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

// Naughty
#include <tcl.h>
#include <tclInt.h>
#include <tclParse.h>
#include <tclDecls.h>
#include "tclIntDecls.h"

namespace
{
  bool DEBUG = false;

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
  std::vector<Command> commands_ {
    // This is interesting. The call semantics of even basic commands really
    // depeend on the _documenatation_ for a command rather than the signature.
    { "", "display string to cmdline", "puts", { "[fp]", "[-nonewline]", "str"} },
    { "", "set or get a variable", "set", { "var", "[value]" } },
    { "", "define a new command", "proc", { "name", "args", "body" } },
  };

  struct Call
  {
    std::string ns;
    size_t offset;
  };
  std::vector<Call> commandStartPositions_;

  struct QualifiedName
  {
    std::string ns;
    std::string name;
  };

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
    //      A
    //        B
    // Has:
    //    X (numComponents 4)
    //       Y (numComponent 1)
    //          Z (numComponents 0)
    //       A (numComponent 1)
    //          B (numComponents 0)
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

  template< typename QN, typename S1, typename S2 >
  void splitName( QN& qn, S1 ns, S2 name )
  {
    // If name starts with :: it's absolute and `ns` is ignored
    // Otherwise we concatenate anything up to the last :: to `ns`
    // name is always everything after the last ::

    auto pos = name.rfind( "::" );
    if ( pos == decltype(name)::npos )
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
    auto name = parseWord( parseResult, ++tokenIndex );
    splitName( c, ns, name );

    auto args = parseWord( parseResult, ++tokenIndex );
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

    if ( DEBUG )
    {
      std::cout << " Indexed: \n"
                << "   Command: " << c.ns << "::" << c.name << '\n'
                << "   Doc: " << c.documenation << '\n'
                << "   Args: ";
      if (c.args.size() > 0)
      {
        std::copy( c.args.begin(),
                   c.args.end() - 1,
                   std::ostream_iterator< std::string >( std::cout, "," ) );
        std::cout << *c.args.rbegin();
      }
      std::cout << '\n';
    }

    auto body = parseWord( parseResult, ++tokenIndex );
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
    auto thisCommand = parseWord( parseResult, tokenIndex );
    if (thisCommand.empty())
    {
      return;
    }

    // code-complete pass ?
    if (completeAt_ >= 0)
    {
      // FIXME/TODO if this is a token which is "code" such as the body of a
      // proc, we need to recurse into the body like we do when parsing
      // otherwise we always recognise the top level command (e.g. proc)

      // see if we requested completion within any of these tokens
      size_t idx = 1; // skip the token that reprsents the whole command
      const char * completePtr = SCRIPT + completeAt_;
      for (int wordIndex = 0;
           wordIndex < parseResult.numWords;
           ++wordIndex, ++idx)
      {
        Tcl_Token &token = parseResult.tokenPtr[ idx ];
        if ( wordIndex == parseResult.numWords - 1 )
        {
          // we hit the last token - it must be this one
        }
        else if ( token.start + token.size < completePtr )
        {
          // we've not gone far enough.
          continue;
        }

        // otherwise, this _is_ the droid we are looking for
        assert( completePtr > token.start );
        if ( completePtr - token.start > token.size )
        {
          // the completion position is _after_ the current token
          wordIndex ++;
        }

        std::string_view partialToken( token.start,
                                       completePtr - token.start );
        std::cout << "COMPLETE: Token is: " << partialToken << '\n';
        if ( wordIndex == 0 )
        {
          std::cout << " -- Complete command name in ns: " << ns << "\n";
          for ( auto& c : commands_ )
          {
            // TODO/FIXME: this is not a good test for "in scope", it just
            // checks if the namespaces are equal, or if the command namespace
            // is a prefix of the current one....
            if ( c.ns != ns && ns.find_first_of( c.ns ) != 0 )
            {
              std::cout << " - (ns) " << c.ns << "::" << c.name << '\n';
              continue;
            }

            // FIXME: This is the worlds worst "fuzzy" match
            if ( c.name.find_first_of( partialToken ) != 0 )
            {
              std::cout << " - (match) " << c.ns << "::" << c.name << '\n';
              continue;
            }

            std::cout << " + " << c.ns << "::" << c.name << '\n';
          }
        }
        else
        {
          // FIXME: This is probably not the best way to search for commands,
          // but it will do for now.
          // findCommand; <<-- tag for later
          QualifiedName qn;
          splitName( qn, ns, thisCommand );
          auto pos = std::find_if( commands_.begin(),
                                   commands_.end(),
                                   [&]( Command& c ) {
                                     return c.name == qn.name &&
                                            c.ns == qn.ns;
                                   } );
          if ( pos != commands_.end() )
          {
            const Command &c = *pos;
            auto argIndex = wordIndex - 1;
            if ( argIndex >= c.args.size() )
            {
              argIndex = c.args.size() - 1;
            }

            std::cout << " -- Complete argument ("
                      << argIndex
                      << ", "
                      << c.args[ argIndex ]
                      << ") of " << thisCommand << "\n" ;

            std::cout << "   -> Signature: ";
            if (c.args.size() > 0)
            {
              // TODO: make a proper join() function
              std::copy( c.args.begin(),
                         c.args.end() - 1,
                         std::ostream_iterator< std::string >( std::cout, "," ) );
              std::cout << *c.args.rbegin();
            }
            std::cout << '\n';
          }
          else
          {
            std::cout << "Unknown command: " << thisCommand << "\n";
          }
        }
        break;
      }

      return;
    }

    // record where the command started
    commandStartPositions_.push_back(
      Call { ns, (size_t)(parseResult.commandStart - SCRIPT) } );

    // Oteherwise, build the index
    if ( thisCommand == "proc" && parseResult.numWords == 4)
    {
      parseProc(interp, parseResult, tokenIndex, ns);
    }
    else if ( thisCommand == "namespace" )
    {
      if ( parseResult.numWords > 2 )
      {
        auto arg = parseWord( parseResult, ++tokenIndex );
        if (arg.empty())
        {
          return;
        }

        if (arg == "eval")
        {
          std::string new_ns( parseWord( parseResult, ++tokenIndex ) );
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
        if ( DEBUG )
        {
          std::cerr << "ERROR RECOVERY\n";
        }
        if ( completeAt_ >= 0 )
        {
          // TODO: We're completing, so it's likely that we don't yet have a
          // complete command, so back up until we find something
          // that starts a word, then asume that's the end of the command
          auto original_start = script;
          auto original_bytes = numBytes;
          script += numBytes;
          numBytes = 0;
          while ( (--script, --completeAt_, script>=original_start ) &&
                  !( CHAR_TYPE(*script) &
                     ( TYPE_SPACE | TYPE_QUOTE | TYPE_BRACE ) ) ) {
            // reverse
          }
          // parse up to this point, then see where we get to
          // if the above loop ran out, we'd get script == original_start and
          // bail because numBytes is 0
          numBytes = script - original_start;
          script = original_start;
        }
        else
        {
          // Advance to the next thing that looks like the end of a command and
          // see if we find a command _afer_ this one
          while ( (++script, --numBytes) &&
                  !(CHAR_TYPE(*script) & TYPE_COMMAND_END) ) {
            // advance
          }
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

          if (DEBUG)
          {
            printCommandTree( parseResult, commandLen );
          }

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
    else if ( arg == "--debug" )
    {
      shift();
      DEBUG = true;
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
    // find the _last_ command start posiiton which is <= completeAt and start
    // parsing from there
    //
    // FIXME: this doesn't work because we don't know the ns for the commands in
    // the commandStartPosisions_
    auto pos = std::lower_bound( commandStartPositions_.rbegin(),
                                 commandStartPositions_.rend(),
                                 Call{ "", (size_t)completeAt },
                                 []( const auto& a, const auto& b ) {
                                   return a.offset > b.offset;
                                 } );
    if ( pos != commandStartPositions_.rend() )
    {
      parseScript( interp,
                   SCRIPT + pos->offset,
                   completeAt - pos->offset,
                   "" );
    }
  }

  Tcl_DeleteInterp(interp);

  return 0;
}
