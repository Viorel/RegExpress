#pragma once

#include <cassert>


class StreamWriter abstract
{
protected:

    StreamWriter( HANDLE h )
        : mHandle( h )
    {
        assert( h != INVALID_HANDLE_VALUE );
        // (probably 0 is a valid handle)
    }

private:

    HANDLE const mHandle;

protected:

    void WriteBytes( const void* buffer, uint32_t size ) const;
};


class StreamWriterA final : public StreamWriter
{
public:

    explicit StreamWriterA( HANDLE h )
        : StreamWriter( h )
    {
    }

    void WriteString( LPCSTR text ) const;
    void WriteString( const std::string & text ) const;

    template <typename... Args>
    void __cdecl WriteStringF( std::string_view frm, Args&& ...args ) const
    {
        std::string s = std::vformat( frm, std::make_format_args( args... ) );

        WriteString( s );
    }

};


class StreamWriterW final : public StreamWriter
{
public:

    explicit StreamWriterW( HANDLE h )
        : StreamWriter( h )
    {
    }

    void WriteString( LPCWSTR text ) const;
    void WriteString( const std::wstring& text ) const;

    template <typename... Args>
    void __cdecl WriteStringF( std::wstring_view frm, Args&& ...args ) const
    {
        std::wstring s = std::vformat( frm, std::make_wformat_args( args... ) );

        WriteString( s );
    }

};

