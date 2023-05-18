#pragma once


namespace
{
    template<typename FROM>
    class CheckedCastHelper sealed
    {
    public:

        CheckedCastHelper( const FROM& v ) : value( v ) {}

        CheckedCastHelper( ) = delete;
        CheckedCastHelper( const CheckedCastHelper& ) = delete;
        void operator = ( const CheckedCastHelper& ) = delete;
        void operator = ( CheckedCastHelper&& ) = delete;

        CheckedCastHelper( CheckedCastHelper&& ) = default;

        //template<typename TO>
        //operator TO( ) const
        //{
        //    if( value > std::numeric_limits<TO>::max( ) )
        //    {
        //        throw std::runtime_error( "Overflow: " + std::to_string( value ) );
        //    }
        //
        //    return (TO)value;
        //}


        template<typename TO>
        operator TO( ) const
        {
            return CheckedCast<TO>( value );
        }


    private:

        const FROM& value;

        template<typename TO, typename FROM>
        static TO CheckedCast( const FROM& ); // if error here, then the cast is not yet (but can be) implemented

    public:

        template<>
        static inline uint32_t CheckedCast( const uint64_t& v ) // (also from size_t)
        {
            if( v > std::numeric_limits<uint32_t>::max( ) )
            {
                throw std::runtime_error( "'uint32_t' overflow: " + std::to_string( v ) );
            }

            return (uint32_t)v;
        }


        template<>
        static inline int32_t CheckedCast( const uint64_t& v ) // (also from size_t)
        {
            if( v > std::numeric_limits<int32_t>::max( ) )
            {
                throw std::runtime_error( "'int32_t' overflow: " + std::to_string( v ) );
            }

            return (uint32_t)v;
        }


        template<>
        static inline uint64_t CheckedCast( const uint64_t& v ) // (also size_t)
        {
            return v;
        }


        template<>
        static inline int32_t CheckedCast( const int64_t& v )
        {
            if( v > std::numeric_limits<int32_t>::max( ) )
            {
                throw std::runtime_error( "'int32_t' overflow: " + std::to_string( v ) );
            }

            if( v < std::numeric_limits<int32_t>::min( ) )
            {
                throw std::runtime_error( "'int32_t' underflow: " + std::to_string( v ) );
            }

            return (int32_t)v;
        }


        template<>
        static inline uint32_t CheckedCast( const uint32_t& v )
        {
            return v;
        }


        template<>
        static inline int32_t CheckedCast( const uint32_t& v )
        {
            if( v > (uint32_t)std::numeric_limits<int32_t>::max( ) )
            {
                throw std::runtime_error( "'int32_t' overflow: " + std::to_string( v ) );
            }

            return (int32_t)v;
        }


        template<>
        static inline unsigned long CheckedCast( const uint32_t& v ) // (also to DWORD)
        {
            static_assert( sizeof( unsigned long ) == sizeof( uint32_t ), "" );

            return v;
        }


        template<>
        static inline uint64_t CheckedCast( const uint32_t& v )
        {
            return v;
        }

    };

}


/// <summary>
/// <para>Usage: 'CheckedCast(some_value)', not 'CheckedCast&lt;some_type>(some_value)'.
/// The target type is determined from context.</para>
/// <para>Example: <c>int x = 1234; char c = CheckedCast(x);</c> </para>
/// </summary>
/// <typeparam name="FROM">(Type determined automatically)</typeparam>
/// <param name="v">The value to convert</param>
/// <returns></returns>
template<typename FROM>
inline CheckedCastHelper<FROM> CheckedCast( const FROM& v )
{
    return CheckedCastHelper<FROM>( v );
}

// just an idea:
//template<typename TO, typename FROM>
//inline TO CheckedCastTo( const FROM& v )
//{
//    return (TO)CheckedCast( v );
//}

