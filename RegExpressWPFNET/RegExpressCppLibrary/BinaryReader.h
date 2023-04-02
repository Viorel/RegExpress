#pragma once

#include <string>


class BinaryReader abstract
{
protected:

	BinaryReader( HANDLE h )
		: mHandle( h )
	{

	}

public:

	uint8_t ReadByte( ) const;

	template<typename T>
	T ReadT( ) const
	{
		T v;

		ReadBytes( &v, sizeof( v ) );

		return v;
	}

private:

	HANDLE const mHandle;

protected:

	void ReadBytes( void* buffer, uint32_t size ) const;
	int Read7BitEncodedInt( ) const;

};


/// <summary>
/// A reader that is designed to be partially compatible with 'BinaryReader' class from .NET, using UTF-8 encoding.
/// </summary>
class BinaryReaderA final : public BinaryReader
{
public:

	BinaryReaderA( HANDLE h )
		: BinaryReader( h )
	{

	}

	std::string ReadString( ) const;

};



/// <summary>
/// A reader that is designed to be partially compatible with 'BinaryReader' class from .NET, using Unicode encoding.
/// </summary>
class BinaryReaderW final : public BinaryReader
{
public:

	BinaryReaderW( HANDLE h )
		: BinaryReader( h )
	{

	}

	std::wstring ReadString( ) const;

};

