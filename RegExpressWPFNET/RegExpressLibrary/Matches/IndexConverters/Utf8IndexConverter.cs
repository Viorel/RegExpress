using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary.Matches.IndexConverters
{
    public class Utf8IndexConverter : IIndexConverter
    {
        readonly byte[] Utf8Bytes;

        public Utf8IndexConverter(string text)
        {
            Utf8Bytes = Encoding.UTF8.GetBytes( text );
        }

        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            int start = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, nativeStart );
            int end = Encoding.UTF8.GetCharCount( Utf8Bytes, 0, nativeEnd);

            return (start, end - start);
        }
    }
}
