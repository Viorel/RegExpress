#include <iostream>
#include <exception>
#include <type_traits>

#include ".\\compile-time-regular-expressions\\single-header\\ctre-unicode.hpp"


template <typename Iterator, typename... Captures>
class Results
{
private:

    using CAPTURES = ctre::captures<ctre::captured_content<0>::template storage<Iterator>, typename Captures::template storage<Iterator>...>;
    CAPTURES captures{};

public:

    template<int I, ctll::fixed_string FIRST_NAME, ctll::fixed_string... TAIL_NAMES>
    constexpr void WriteNames( const auto& sa, const auto& match ) const noexcept
    {
        constexpr bool exists = CAPTURES::template exists<FIRST_NAME>( );

        if constexpr( !exists )
        {
            // name not found
        }
        else
        {
            auto g = match.get<FIRST_NAME>( );

            if( !g )
            {
                // named group does not match
            }
            else
            {
                auto position = std::distance( sa.begin( ).orig_begin, g.begin( ) );
                auto size = g.size( );

                std::cout << "n " << I << " " << position << " " << size << std::endl;
            }
        }

        WriteNames<I + 1, TAIL_NAMES...>( sa, match );
    }

    template<int I>
    constexpr void WriteNames( const auto& sa, const auto& match ) const noexcept
    {
        // no names
    }
};

template <typename Iterator, typename... Captures>
constexpr static Results<Iterator, Captures...> MakeResults( const ctre::regex_results<Iterator, Captures...>& ) noexcept
{
    return Results<Iterator, Captures...>{};
}

template<size_t I, size_t N>
static void WriteCaptures( const auto& sa, const auto& match )
{
    if constexpr( I < N )
    {
        const auto& g = match.get<I>( );

        if( !g )
        {
            std::cout << "g -1 -1" << std::endl;
        }
        else
        {
            auto position = std::distance( sa.begin( ).orig_begin, g.begin( ) );
            auto size = g.size( );

            std::cout << "g " << position << " " << size << std::endl;
        }

        WriteCaptures<I + 1, N>( sa, match );
    }
}

static void DoWork( )
{
    try
    {
        static constexpr ctll::fixed_string pattern = /*START-PATTERN*/L"(?<n>.)(?<m>.)"/*END-PATTERN*/;
        std::wstring_view text = /*START-TEXT*/L"a\nbcd"/*END-TEXT*/;

        auto sa = ctre::search_all<pattern /*START-MODIFIERS*/, ctre::singleline /*END-MODIFIERS*/ >( text );

        std::string::const_iterator::difference_type previous_position = -1;

        for( const auto& match : sa )
        {
            const auto& g = match.get<0>( );
            auto position = std::distance( sa.begin( ).orig_begin, g.begin( ) );

            if( previous_position == position )
            {
                throw std::runtime_error( "Infinite match. No advance." );
            }

            previous_position = position;

            std::cout << "M " << position << " " << g.size( ) << std::endl;

            constexpr size_t count = match.count( );

            WriteCaptures<1, count>( sa, match );

            auto my_results = MakeResults( match );

            my_results.WriteNames<0/*START-NAMES*/, L"n", L"x", "m"/*END-NAMES*/>( sa, match );
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
