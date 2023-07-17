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
	int Read7BitEncodedInt( ) const; // (borrowed from .NET)

};


/// <summary>
/// A reader that is partially compatible with 'BinaryReader' class from .NET, using UTF-8 or ASCII encoding.
/// The strings that are written by 'BinaryWriter' in .NET can be read by this class in C++.
/// </summary>
class BinaryReaderA final : public BinaryReader
{
public:

	explicit BinaryReaderA( HANDLE h )
		: BinaryReader( h )
	{

	}

	std::string ReadString( ) const;

};



/// <summary>
/// A reader that is partially compatible with 'BinaryReader' class from .NET, using Unicode encoding.
/// The strings that are written by 'BinaryWriter' in .NET can be read by this class in C++.
/// </summary>
class BinaryReaderW final : public BinaryReader
{
public:

	explicit BinaryReaderW( HANDLE h )
		: BinaryReader( h )
	{

	}

	std::wstring ReadString( ) const;
	std::string ReadPrefixedString( ) const;

};

