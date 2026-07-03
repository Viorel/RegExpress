using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary.Matches.IndexConverters
{
    public class CodepointIndexConverter : IIndexConverter
    {
        readonly SurrogatePairsHelper Sph;

        public CodepointIndexConverter( string text )
        {
            Sph = new( text );
        }

        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            int native_length = nativeEnd - nativeStart;

            return Sph.ToCharIndexAndLength( nativeStart, native_length );
        }
    }
}
