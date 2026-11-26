using System.Text;

namespace RegExpressLibrary.Matches.IndexConverters;

public sealed class Utf8IndexConverter( string text ) : IndexConverter
{
    readonly byte[] Utf8Bytes = Encoding.UTF8.GetBytes( text );

    public override Coordinates ConvertIndexLength( int nativeIndex, int nativeLength )
    {
        int native_end = nativeIndex + nativeLength;
        int char_start = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, nativeIndex );
        int char_end = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, native_end );
        int char_length = char_end - char_start;

        return new Coordinates( nativeIndex, nativeLength, char_start, char_length );
    }
}
