// MRegexSample.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>

#include "mregex.hpp"

template<typename NAME>
static void PrintName( const meta::regex_capture_base<NAME>& capture )
{
    std::cout << '<' << capture.name() << '>';
}

template<>
static void PrintName( const meta::regex_capture_base<meta::symbol::unnamed>& c )
{
    std::cout << "<>";
}


int main()
{
    using regex = meta::regex< /*START-PATTERN*/R"((x)(?<na>a)(?<nb>b?)(?<nc>c)?d)"/*END-PATTERN*/ /*START-MODIFIERS*/, meta::regex_flag::icase/*END-MODIFIERS*/ >;
    constexpr std::string_view text = /*START-TEXT*/"xyz xad xabcd"/*END-TEXT*/;

    for( auto&& match : regex::find_all( text ) )
    {
        std::cout << "M " << std::distance( text.cbegin( ), match.begin( ) ) << " " << match.length( ) << std::endl;

        bool is_first_group = true;

        match.for_each_group( [&text, &is_first_group]( auto&& group )
            {
                if( is_first_group )
                {
                    is_first_group = false;
                }
                else
                {
                    std::cout << "g ";
                    PrintName( group );

                    if( group.is_empty( ) ) // empty or failed
                    {
                        std::cout << " -1 -1" << std::endl;
                    }
                    else
                    {
                        std::cout << " " << std::distance( text.cbegin( ), group.begin( ) ) << " " << group.length( ) << std::endl;
                    }
                }
            } );
    }

}

