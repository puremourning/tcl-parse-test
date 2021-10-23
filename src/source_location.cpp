#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace Parser
{
  struct SourceFile
  {
    std::string fileName;
    std::string contents;

    std::vector< size_t > newlines;

    void ParseNewLines();
  };

  SourceFile make_source_file( std::string_view fileName,
                               std::string&& contents )
  {
    SourceFile f{ .fileName = std::string( fileName ),
                  .contents = std::move( contents ),
                  .newlines{} };
    f.ParseNewLines();
    return f;
  }

  void SourceFile::ParseNewLines()
  {
    newlines.reserve( std::count_if( contents.begin(),
                                     contents.end(),
                                     []( auto x ) { return x == '\n'; } ) +
                      1 );

    for ( size_t i = 0; i <= contents.length(); ++i )
    {
      if ( contents[ i ] == '\n' )
      {
        newlines.push_back( i );
      }
    }
    newlines.push_back( contents.length() );
  }
}  // namespace Parser

namespace Parser
{
  using LinePos = std::tuple< size_t, size_t >;

  // TODO: SHould i be relying on ADL here, or should this be in the global
  // scope?
  std::ostream& operator<<( std::ostream& o, const LinePos& p )
  {
    o << std::get< 0 >( p ) << ':' << std::get< 1 >( p );
    return o;
  }

  /**
   * Returns the 0-based line number and 0-based byte offset into that line of
   * the supplied 0-based byte offset into the contnts of the file.
   */
  LinePos OffsetToLineByte( const SourceFile& sourceFile, size_t offset )
  {
    for ( size_t i = 0; i < sourceFile.newlines.size(); ++i )
    {
      if ( sourceFile.newlines[ i ] >= offset )
      {
        auto start_of_line = i == 0 ? 0 : sourceFile.newlines[ i - 1 ] + 1;
        auto column = offset - start_of_line;

        return { i, column };
      }
    }

    assert( false && "Invalid offset" );
    return { 0, 0 };
  }
}  // namespace Parser

namespace Parser
{
  struct SourceLocation
  {
    const SourceFile* sourceFile;
    size_t offset;  /// 0-based offset into source file
    size_t line;    /// 0-based line
    size_t byte;    /// 0-based offset into line
  };

  SourceLocation make_source_location( const SourceFile& sourceFile,
                                       size_t offset )
  {
    auto [ line, byte ] = OffsetToLineByte( sourceFile, offset );
    return { &sourceFile, offset, line, byte };
  }

  SourceLocation make_source_location( const SourceFile& sourceFile,
                                       const char* pos )
  {
    return make_source_location( sourceFile, pos - sourceFile.contents.data() );
  }
}  // namespace Parser

namespace Parser::Test
{
  void TestOffsetToLineByte()
  {
    struct Test
    {
      SourceFile file;
      size_t offset;
      LinePos pos;
    };

    std::vector< Test > tests = {
      { make_source_file( "test", "this is some text" ), 0, { 0, 0 } },
      { make_source_file( "test", "" ), 0, { 0, 0 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 0, { 0, 0 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 1, { 0, 1 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 2, { 1, 0 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 3, { 1, 1 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 4, { 2, 0 } },
      { make_source_file( "test", "1\n2\n3\n4" ), 5, { 2, 1 } },
      { make_source_file( "test", "aaaa\nbbbb\ncccc" ), 8, { 1, 3 } },
    };

    for ( auto&& test : tests )
    {
      auto result = OffsetToLineByte( test.file, test.offset );
      if ( result != test.pos )
      {
        std::cerr << "Expected " << test.pos << " but got " << result << '\n';
        abort();
      }
    }
  }
};  // namespace Parser::Test
