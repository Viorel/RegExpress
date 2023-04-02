#pragma once


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

    StreamWriterA( HANDLE h )
        : StreamWriter( h )
    {
    }

    void WriteString( LPCSTR text ) const;
    void WriteString( const std::string & text ) const;

    void __cdecl WriteStringF( LPCSTR format, ... ) const;

    static std::string Printf( LPCSTR format, ... );

};


class StreamWriterW final : public StreamWriter
{
public:

    StreamWriterW( HANDLE h )
        : StreamWriter( h )
    {
    }

    void WriteString( LPCWSTR text ) const;
    void WriteString( const std::wstring& text ) const;

    void __cdecl WriteStringF( LPCWSTR format, ... ) const;

    static std::wstring Printf( LPCWSTR format, ... );

};

