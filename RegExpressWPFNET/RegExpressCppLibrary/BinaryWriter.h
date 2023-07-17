#pragma once

#include <string>


class BinaryWriter abstract
{
protected:

	BinaryWriter( HANDLE h )
		: mHandle( h )
	{

	}

public:

	void WriteBytes( const void* buffer0, uint32_t size );

	template<typename T>
	void WriteT( const T& v)
	{
		WriteBytes( &v, sizeof( v ) );
	}


private:

	HANDLE const mHandle;

protected:

	void Write7BitEncodedInt( int32_t value ); // (borrowed from .NET)
};



/// <summary>
/// A writer that is partially compatible with 'BinaryWriter' class from .NET, using UTF-8 encoding.
/// The strings that are written by this class can be read by 'BinaryReader' in .NET.
/// </summary>
class BinaryWriterA final : public BinaryWriter
{
public:

	explicit BinaryWriterA( HANDLE h )
		: BinaryWriter(h)
	{

	}

	void Write( LPCSTR s );
	void Write( LPCSTR s, uint32_t charlen );
	void Write( const std::string & s );
};


/// <summary>
/// A writer that is partially compatible with 'BinaryWriter' class from .NET, using Unicode encoding.
/// The strings that are written by this class can be read by 'BinaryReader' in .NET.
/// </summary>
class BinaryWriterW final : public BinaryWriter
{
public:

	explicit BinaryWriterW( HANDLE h )
		: BinaryWriter( h )
	{

	}

	void Write( LPCWSTR s );
	void Write( LPCWSTR s, uint32_t charlen );
	void Write( const std::wstring& s );
};

