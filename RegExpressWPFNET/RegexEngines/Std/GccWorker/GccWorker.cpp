#include <iostream>
#include <string>
//#include <format> // does not seem to be implemented
#include <regex>

#include ".\..\..\..\RegExpressCppLibrary\PartialJSON.h"

using namespace std;

int main( )
{
    try
    {
        // input is UTF-8
        string commandS;
        std::getline( std::cin, commandS );

        string commandA;
        if( !PartialJSON::ParseString( &commandA, commandS.c_str( ) ) ) throw std::runtime_error( "cannot read command: '" + commandS + '\'' );

        if( commandA == "v" )
        {
            wcout << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__ << endl;
        }
        else if( commandA == "m" )
        {
            string patternS;
            string textS;
            string syntaxS;
            string flagsS;

            std::getline( std::cin, patternS );
            std::getline( std::cin, textS );
            std::getline( std::cin, syntaxS );
            std::getline( std::cin, flagsS );

            string patternA;
            string textA;
            string syntaxA;
            string flagsA;

            if( !PartialJSON::ParseString( &patternA, patternS.c_str( ) ) ) throw std::runtime_error( "cannot read pattern: '" + patternS + '\'' );
            if( !PartialJSON::ParseString( &textA, textS.c_str( ) ) ) throw std::runtime_error( "cannot read text: '" + textS + '\'' );
            if( !PartialJSON::ParseString( &syntaxA, syntaxS.c_str( ) ) ) throw std::runtime_error( "cannot read syntax: '" + syntaxS + '\'' );
            if( !PartialJSON::ParseString( &flagsA, flagsS.c_str( ) ) ) throw std::runtime_error( "cannot read flags: '" + flagsS + '\'' );

            wstring patternW = PartialJSON::UTF8_to_wchar( patternA.c_str( ) );
            wstring textW = PartialJSON::UTF8_to_wchar( textA.c_str( ) );

            wregex::flag_type regexFlags{};

            if( syntaxA == "ECMAScript" || syntaxA.length( ) == 0 ) regexFlags |= regex_constants::ECMAScript;
            else if( syntaxA == "basic" ) regexFlags |= regex_constants::basic;
            else if( syntaxA == "extended" ) regexFlags |= regex_constants::extended;
            else if( syntaxA == "awk" ) regexFlags |= regex_constants::awk;
            else if( syntaxA == "grep" ) regexFlags |= regex_constants::grep;
            else if( syntaxA == "egrep" ) regexFlags |= regex_constants::egrep;
            else throw std::runtime_error( "Invalid syntax: '" + syntaxA + '\'' );

            flagsA = ' ' + flagsA + ' ';

            // does not seem implemented
            //if(flagsA.contains(" icase "));

            if( flagsA.find( " icase " ) != std::string::npos ) regexFlags |= regex_constants::icase;
            if( flagsA.find( " nosubs " ) != std::string::npos ) regexFlags |= regex_constants::nosubs;
            if( flagsA.find( " optimize " ) != std::string::npos ) regexFlags |= regex_constants::optimize;
            if( flagsA.find( " collate " ) != std::string::npos ) regexFlags |= regex_constants::collate;
            if( flagsA.find( " multiline " ) != std::string::npos ) regexFlags |= regex_constants::multiline;

            regex_constants::match_flag_type matchFlags = regex_constants::match_default;

            if( flagsA.find( " match_not_bol " ) != std::string::npos ) matchFlags |= regex_constants::match_not_bol;
            if( flagsA.find( " match_not_eol " ) != std::string::npos ) matchFlags |= regex_constants::match_not_eol;
            if( flagsA.find( " match_not_bow " ) != std::string::npos ) matchFlags |= regex_constants::match_not_bow;
            if( flagsA.find( " match_not_eow " ) != std::string::npos ) matchFlags |= regex_constants::match_not_eow;
            if( flagsA.find( " match_any " ) != std::string::npos ) matchFlags |= regex_constants::match_any;
            if( flagsA.find( " match_not_null " ) != std::string::npos ) matchFlags |= regex_constants::match_not_null;
            if( flagsA.find( " match_continuous " ) != std::string::npos ) matchFlags |= regex_constants::match_continuous;
            if( flagsA.find( " match_prev_avail " ) != std::string::npos ) matchFlags |= regex_constants::match_prev_avail;

            wregex regex( patternW, regexFlags );

            wcregex_iterator results_begin( textW.c_str( ), textW.c_str( ) + textW.length( ), regex, matchFlags );
            wcregex_iterator results_end{};

            for( auto i = results_begin; i != results_end; ++i )
            {
                const std::wcmatch& match = *i;

                wcout << L"m " << i->position( ) << L" " << i->length( ) << endl; // ('wchar_t' units)

                int j = 0;

                for( auto k = match.cbegin( ); k != match.cend( ); ++k, ++j )
                {
                    if( j == 0 ) continue; // (first is the full match)

                    const std::wcsub_match& submatch = *k;

                    if( !submatch.matched )
                    {
                        wcout << L" g -1 -1" << endl;
                    }
                    else
                    {
                        wcout << L" g " << match.position( j ) << L" " << match.length( j ) << endl; // ('wchar_t' units)
                    }
                }
            }
        }
        else
        {
            throw std::runtime_error( "Invalid command: '" + commandA + "'" );
        }
    }
    catch( const std::regex_error& exc )
    {
        wcerr << "[code: " << exc.code( ) << "] " << exc.what( ) << endl;
    }
    catch( const std::exception& exc )
    {
        wcerr << exc.what( ) << endl;
    }
    catch( ... )
    {
        wcerr << L"Unknown error" << endl;
    }
}
