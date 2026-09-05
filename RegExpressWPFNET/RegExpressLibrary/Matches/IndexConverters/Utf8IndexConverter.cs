using System.Text;

namespace RegExpressLibrary.Matches.IndexConverters
{
    public sealed class Utf8IndexConverter( string text ) : IIndexConverter
    {
        readonly byte[] Utf8Bytes = Encoding.UTF8.GetBytes( text );

        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            int start = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, nativeStart );
            int end = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, nativeEnd );

            return (start, end - start);
        }
    }
}
