// CompileTimeRegexSample.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <exception>

#include ".\\compile-time-regular-expressions\\single-header\\ctre-unicode.hpp"

template<size_t I, size_t N>
static void write_capture( const auto& sa, const auto& match )
{
    if constexpr( I < N )
    {
        const auto& g = match.get<I>( );
        std::cout << "g " << g.begin( ) - sa.begin( ).orig_begin << " " << g.size( ) << std::endl;

        write_capture<I + 1, N>( sa, match );
    }
}

static void DoWork( )
{
    try
    {
        static constexpr ctll::fixed_string pattern = /*START-PATTERN*/L"."/*END-PATTERN*/;
        std::wstring_view text = /*START-TEXT*/L"a\nb"/*END-TEXT*/;

        auto sa = ctre::search_all<pattern /*START-MODIFIERS*/, ctre::singleline /*END-MODIFIERS*/ >( text );

        std::string::const_iterator::difference_type previous_position = -1;

        for( const auto& match : sa )
        {
            const auto& g = match.get<0>( );
            auto position = g.begin( ) - sa.begin( ).orig_begin;

            if( previous_position == position )
            {
                throw std::runtime_error( "Infinite match. No advance." );
            }

            previous_position = position;

            std::cout << "M " << position << " " << g.size( ) << std::endl;

            constexpr size_t count = match.count( );

            write_capture<1, count>( sa, match );
        }
    }
    catch( const std::exception& exc )
    {
        std::cerr << exc.what( ) << std::endl;
    }
    catch( ... )
    {
        std::cerr << "Unknown error" << std::endl;
    }
}

int main( )
{
#define EXCEPTION_EXECUTE_HANDLER      1 // (copied from 'excpt.h' to avoid including larger header files)

    __try
    {
        DoWork( );
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
        std::cerr << "SEH exception" << std::endl;
    }

    return 0;
}
