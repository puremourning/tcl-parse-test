#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <variant>
#include <string>
#include <string_view>

#include <tcl.h>
#include <tclInt.h>
#include <tclParse.h>
#include <tclDecls.h>
#include "tclIntDecls.h"

#include "source_location.cpp"

namespace Parser
{
  std::vector< std::string_view > SplitPath( std::string_view path )
  {
    std::vector< std::string_view > vec;
    std::string_view::size_type start = 0;
    for ( std::string_view::size_type cur = 0; cur < path.length(); ++cur )
    {
      if ( path[ cur ] == ':' && cur + 1 < path.length() &&
           path[ cur + 1 ] == ':' )
      {
        vec.push_back( path.substr( start, cur - start ) );
        cur++;
        start = cur + 1;
      }
    }

    vec.push_back( path.substr( start ) );

    return vec;
  }

  struct QualifiedName
  {
    std::optional< std::string > ns;
    std::string name;

    bool IsAbs() const
    {
      if ( !ns )
      {
        // no namespace
        return false;
      }

      if ( ns.value().empty() )
      {
        // global namespace, like ::A
        return true;
      }

      if ( ns.value().length() < 2 )
      {
        // likely invalid, like :::B
        return false;
      }

      if ( ns.value().substr( 0, 2 ) == "::" )
      {
        // Has a leading ::, like ::A::B
        return true;
      }

      // No leading ::
      return false;
    }

    QualifiedName AbsPath( std::string_view current_path ) const
    {
      if ( IsAbs() )
      {
        return *this;
      }

      return QualifiedName{ .ns = std::string( current_path ) +
                                  ( ns.has_value() ? ns.value() : "" ),
                            .name = name };
    }

    std::string Path()
    {
      if ( ns.has_value() )
      {
        return ns.value() + "::" + name;
      }
      return name;
    }

    std::vector< std::string_view > NamespacePath() const
    {
      if ( !ns )
      {
        return {};
      }

      bool abs = IsAbs();
      if ( abs && !ns->empty() )
      {
        return SplitPath( std::string_view( *ns ).substr( 2 ) );
      }
      else if ( abs )
      {
        return {};
      }

      return SplitPath( std::string_view( *ns ) );
    }
  };

  template< typename Stream >
  auto& operator<<( Stream& s, const QualifiedName& qn )
  {
    if ( qn.ns )
    {
      s << * qn.ns << "::";
    }
    s << qn.name;
    return s;
  }

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

  struct ParseContext
  {
    SourceFile file;
    std::string cur_ns;
  };

  struct Script;

  struct Word
  {
    // TODO: Should use better names here ?
    using ScriptPtr = std::unique_ptr< Script >;
    using WordVec = std::vector< Word >;
    using WordPtr = std::unique_ptr< Word >;
    using Nothing = std::monostate;

    struct ArrayAccess
    {
      std::string name;
      WordVec index;
    };

    // We still use this discriminant because some variant members are re-used
    // for different node types
    enum class Type
    {
      TEXT,          // use text
      VARIABLE,      // use text, unles use WordVec
      ARRAY_ACCESS,  // use data.ArrayAccess
      SCRIPT,        // use data.ScriptPtr
      TOKEN_LIST,    // use data.WordVec
      EXPAND,        // use data.WordPtr
      LIST,          // use data.WordVec
      ERROR,         // Some sort of parse error (use text)
    } type;

    SourceLocation location;
    std::string_view text;

    std::variant< Nothing, ArrayAccess, ScriptPtr, WordVec, WordPtr > data;
  };


  struct Call
  {
    enum class Type
    {
      PROC,
      WHILE,
      FOR,
      FOREACH,
      NAMESPACE_EVAL,

      IF,
      USER,
    } type;
    std::vector< Word > words;
    std::string ns; // lexical namespace in which the call happens
  };

  struct Script
  {
    SourceLocation location;
    std::vector< Call > commands;
  };

  Script ParseScript( Tcl_Interp* interp,
                      ParseContext& context,
                      std::string_view script );

  Word ParseWord( Tcl_Interp* interp,
                  ParseContext& context,
                  Tcl_Parse& parseResult,
                  size_t& nextToken )
  {
    Word word;
    Tcl_Token& token = parseResult.tokenPtr[ nextToken++ ];

    switch ( token.type )
    {
      case TCL_TOKEN_SIMPLE_WORD:
      {

        // contains a single "text" token - ignore this token and take the
        // contents.
        //
        // Examples
        //   TCL_TOKEN_SIMPLE_WORD: `{\nx y z\n}`
        //     TCL_TOKEN_TEXT: `\nx y z\n`
        //
        //   TCL_TOKEN_SIMPLE_WORD: foreach
        //     TCL_TOKEN_TEXT: foreach
        //
        assert( token.numComponents == 1 );
        word = ParseWord( interp, context, parseResult, nextToken );
        break;
      }

      case TCL_TOKEN_WORD:
      case TCL_TOKEN_EXPAND_WORD:
      {
        // contains pretty much everything else.
        if ( token.type == TCL_TOKEN_EXPAND_WORD )
        {
          word.type = Word::Type::EXPAND;
        }
        else
        {
          word.type = Word::Type::TOKEN_LIST;
        }
        word.location = make_source_location( context.file, token.start );
        word.text = std::string_view{ token.start, (size_t)token.size };
        auto& vec = word.data.emplace< Word::WordVec >();
        vec.reserve( token.numComponents );

        size_t maxToken = nextToken + token.numComponents;
        while ( nextToken < maxToken )
        {
          vec.emplace_back(
            ParseWord( interp, context, parseResult, nextToken ) );
        }
        break;
      }

      case TCL_TOKEN_TEXT:
      case TCL_TOKEN_BS:
      {
        word.type = Word::Type::TEXT;
        word.text = std::string_view{ token.start, (size_t)token.size };
        word.location = make_source_location( context.file, token.start );
        break;
      }

      case TCL_TOKEN_COMMAND:
      {
        word.type = Word::Type::SCRIPT;
        word.text = std::string_view{ token.start, (size_t)token.size };
        word.location = make_source_location( context.file, token.start );

        // A command like `[ a b c ]`
        //
        // NOTE: The text of TCL_TOKEN_COMMAND includes the [ and the ]. We need
        // to parse only the contents of the command, so we use start + 1 and
        // legth - 2 to ignore the [ and the ] to get the `a b c`
        word.data = std::make_unique< Script >(
          ParseScript( interp,
                       context,
                       { token.start + 1, (size_t)token.size - 2 } ) );

        break;
      }

      case TCL_TOKEN_VARIABLE:
      {
        // Usually contains a single TCL_TOKEN_TEXT, but might contain more if
        // it's an array
        if ( token.numComponents == 1 )
        {
          // scalar, just take the text
          word = ParseWord( interp, context, parseResult, nextToken );
          word.type = Word::Type::VARIABLE;
        }
        else
        {
          // Array - TCL_TOKEN_TEXT, followed by a list of other things that
          // make up the array index
          word.type = Word::Type::ARRAY_ACCESS;
          word.text = std::string_view{ token.start, (size_t)token.size };
          word.location = make_source_location( context.file, token.start );

          size_t maxToken = nextToken + token.numComponents;
          auto& arrayAccess = word.data.emplace< Word::ArrayAccess >();

          Word name = ParseWord( interp, context, parseResult, nextToken );
          assert( name.type == Word::Type::TEXT );
          arrayAccess.name = name.text;

          // Read the remainder into a word vector
          while ( nextToken < maxToken )
          {
            arrayAccess.index.emplace_back(
              ParseWord( interp, context, parseResult, nextToken ) );
          }
        }
        break;
      }

      case TCL_TOKEN_SUB_EXPR:
      case TCL_TOKEN_OPERATOR:
        assert( false && "Unhandled case!" );
        break;
    }

    return word;
  }

  Word WordToList( Tcl_Interp* interp,
                   const ParseContext& context,
                   Word&& word )
  {
    const char* list = word.text.data();
    size_t length = word.text.length();
    const char* prevList = nullptr;
    const char* last = list + length;
    size_t size;
    const char* element;

    Word::WordVec vec;

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
        // unable to convert to list, leave as a word
        return std::move( word );
      }
      length -= ( list - prevList );
      if ( element >= last )
      {
        break;
      }
      // TODO: What if the element is itself a list? do we need to really
      // defer this processing until we need it ? or say, this is specifically
      // for parsing function arguments. for now, leave it for the indexer to
      // =work out
      vec.emplace_back(
        Word{ .type = Word::Type::TEXT,
              .location = make_source_location( context.file, element ),
              .text = { element, (size_t)( size ) },
              .data{} } );
    }

    if ( vec.size() == 1 )
    {
      return std::move( word );
    }

    word.type = Word::Type::LIST;
    word.data = std::move( vec );
    return std::move( word );
  };

  void ParseCommand( Tcl_Interp* interp,
                     ParseContext& context,
                     Tcl_Parse& parseResult,
                     Script& s )
  {
    if ( parseResult.numWords < 1 )
    {
      // Nothing to parse here?
      return;
    }

    auto& call = s.commands.emplace_back( Call{
      .type = Call::Type::USER,
      .ns = context.cur_ns,
    } );
    call.words.reserve( parseResult.numWords );

    // TODO: Do this like the TclPro instrumenter:
    //  Parse the first word
    //  Match the fist word against a list of known commands
    //  If found, execute a series of ParseWord or ParseBody
    //  Otherwise just parse all the words

    size_t tokenIndex = 0;
    auto parseRest = [ & ]()
    {
      while ( tokenIndex < (size_t)parseResult.numTokens )
      {
        call.words.emplace_back(
          ParseWord( interp, context, parseResult, tokenIndex ) );
      }
    };

    auto parseWord = [ & ]() -> auto&
    {
      if ( tokenIndex >= (size_t)parseResult.numTokens )
      {
        return call.words.emplace_back(
          Word{ .type = Word::Type::ERROR,
                .location = make_source_location( context.file,
                                                  parseResult.commandStart +
                                                    parseResult.commentSize ),
                .text = "Expected word!",
                .data{} } );
      }

      return call.words.emplace_back(
        ParseWord( interp, context, parseResult, tokenIndex ) );
    };

    auto parseArgs = [ & ]() -> auto&
    {
      if ( tokenIndex >= (size_t)parseResult.numTokens )
      {
        return call.words.emplace_back(
          Word{ .type = Word::Type::ERROR,
                .location = make_source_location( context.file,
                                                  parseResult.commandStart +
                                                    parseResult.commentSize ),
                .text = "Expected list!",
                .data{} } );
      }

      auto word =
        WordToList( interp,
                    context,
                    ParseWord( interp, context, parseResult, tokenIndex ) );

      // If we recognised some sort of list, parse the second level (as args is
      // a list-of-lists)
      if ( word.type == Word::Type::LIST )
      {
        auto& vec = std::get< Word::WordVec >( word.data );
        Word::WordVec parsedArgs;
        parsedArgs.reserve( vec.size() );
        for ( auto& arg : vec )
        {
          parsedArgs.emplace_back(
            WordToList( interp, context, std::move( arg ) ) );
        }
        word.data = std::move( parsedArgs );
      }

      return call.words.emplace_back( std::move( word ) );
    };

    auto parseBody = [ & ]() -> auto&
    {
      if ( tokenIndex >= (size_t)parseResult.numTokens )
      {
        return call.words.emplace_back(
          Word{ .type = Word::Type::ERROR,
                .location = make_source_location( context.file,
                                                  parseResult.commandStart +
                                                    parseResult.commentSize ),
                .text = "Expected body!",
                .data{} } );
      }

      auto word = ParseWord( interp, context, parseResult, tokenIndex );

      if ( word.type == Word::Type::TEXT )
      {
        // Body is a simple word, so we can parse it
        word.type = Word::Type::SCRIPT;
        word.data.emplace< Word::ScriptPtr >( std::make_unique< Script >(
          ParseScript( interp, context, word.text ) ) );
      }

      return call.words.emplace_back( std::move( word ) );
    };

    auto& cmdWord = parseWord();
    if ( cmdWord.type == Word::Type::TEXT )
    {
      if ( cmdWord.text == "proc" && parseResult.numWords == 4 )
      {
        // TODO: absolute namespace proc name
        call.type = Call::Type::PROC;
        parseWord();  // name
        parseArgs();  // arguments TODO: parseScopeArgs() ?
        parseBody();  // body
      }
      else if ( cmdWord.text == "while" && parseResult.numWords == 3 )
      {
        call.type = Call::Type::WHILE;
        parseWord();  // while-expression TODO parseExpr() ?
        parseBody();
      }
      else if ( cmdWord.text == "for" && parseResult.numWords == 5 )
      {
        call.type = Call::Type::FOR;
        parseBody();  // { set x 0 }
        parseWord();  // { $x < 100 } TODO parseExpr()
        parseBody();  // { incr x }
        parseBody();  // loop body
      }
      else if ( cmdWord.text == "foreach" && parseResult.numWords == 4 )
      {
        call.type = Call::Type::FOREACH;
        parseWord();  // { x y } // TODO: parseScopeArgs()
        parseWord();  // $list
        parseBody();  // loop body
      }
      else if ( cmdWord.text == "namespace" && parseResult.numWords > 1 )
      {
        auto& subCmd = parseWord();
        if ( subCmd.type == Word::Type::TEXT && subCmd.text == "eval" &&
             parseResult.numWords == 4 )
        {
          call.type = Call::Type::NAMESPACE_EVAL;

          auto& arg = parseWord();
          auto old_ns = context.cur_ns;

          if ( arg.type == Word::Type::TEXT )
          {
            QualifiedName qn = SplitName( arg.text );
            context.cur_ns = qn.AbsPath( context.cur_ns ).Path();
          }

          parseBody();
          context.cur_ns = old_ns;
        }
      }

      // TODO: Interesting cases:
      //  - after ms [script script script..]
      //    script concat'd, and executed as a command.
      //  - eval arg arg arg...
      //    args concat'd and executed as a script
      //    maybe just replace the arg arg arg with a SCRIPT entry after doing
      //    the concat, but the whole point of eval is the script is determined
      //    by the variable values, so maybe this is pointless.
      //  - coroutine
    }

    parseRest();
  }

  Script ParseScript( Tcl_Interp* interp,
                      ParseContext& context,
                      std::string_view script )
  {
    Tcl_Parse parseResult;

    Script s{
      .location = make_source_location( context.file, script.data() ),
      .commands{},
    };

    while ( script.size() > 0 )
    {
      if ( Tcl_ParseCommand( interp,
                             script.data(),
                             script.size(),
                             0,
                             &parseResult ) != TCL_OK )
      {
        // TDDO: ERROR RECOVERY
        return s;
      }

      // Parse the command
      ParseCommand( interp, context, parseResult, s );

      // calculate the remaining chunk of text to parse
      const char* end = script.data() + script.size();
      const char* start = parseResult.commandStart + parseResult.commandSize;
      assert( end >= start );
      script = { start, static_cast< size_t >( end - start ) };
      Tcl_FreeParse( &parseResult );
    }

    return s;
  };
}  // namespace Parser

namespace Parser::Test
{
  void TestQualifiedName()
  {
    struct Test
    {
      std::string lexeme;
      QualifiedName expect;
      bool isAbs;
      std::string path;
    };

    auto fail = 0;

    std::vector< Test > tests = {
      { "Test", { {}, "Test" }, false, "Test" },
      { "::Test", { { "" }, "Test" }, true, "::Test" },
      { "Test::Sub", { { "Test" }, "Sub" }, false, "Test::Sub" },
      { "::Test::Sub", { { "::Test" }, "Sub" }, true, "::Test::Sub" },
    };

    for ( auto&& test : tests )
    {
      QualifiedName qn = SplitName( test.lexeme );
      if ( qn.ns != test.expect.ns )
      {
        std::cerr << "Expected " << test.lexeme << " to have ns "
                  << ( test.expect.ns ? *test.expect.ns : "<unset>" )
                  << " but found " << ( qn.ns ? *qn.ns : "<unset>" );
        ++fail;
      }
      if ( qn.name != test.expect.name )
      {
        std::cerr << "Expected " << test.lexeme << " to have name "
                  << test.expect.name << " but found " << qn.name;
        ++fail;
      }
      if ( qn.Path() != test.path )
      {
        std::cerr << "Expected " << test.lexeme << " to have path " << test.path
                  << " but found " << qn.Path() << "\n";
        ++fail;
      }
      if ( qn.IsAbs() != test.isAbs )
      {
        std::cerr << "Expected " << test.lexeme << " to "
                  << ( test.isAbs ? "be" : "not be" ) << " absolute\n";
        ++fail;
      }
    }

    if ( fail )
    {
      abort();
    }
  }

  void TestWord()
  {
    Word w;

    Word w2;
    w2.type = Word::Type::TEXT;
    w2.text = "This is a test";

    w = std::move( w2 );

    auto& v = w.data.emplace< Word::WordVec >();
    Word w3 = {
      .type = Word::Type::TEXT,
      .location{},
      .text = "Something",
      .data{},
    };
    v.push_back( std::move( w3 ) );

    w2 = std::move( w );

    w2.data.emplace< Word::ScriptPtr >();
    w = std::move( w2 );

    std::vector< Word > words;
    words.reserve( 10 );
    words.emplace_back( std::move( w ) );
  }

  void TestLinePosToScriptCursor()
  {

  }

  void Run()
  {
    TestWord();
    TestQualifiedName();
    TestLinePosToScriptCursor();
    TestOffsetToLineByte();
  }
}  // namespace Parser::Test
