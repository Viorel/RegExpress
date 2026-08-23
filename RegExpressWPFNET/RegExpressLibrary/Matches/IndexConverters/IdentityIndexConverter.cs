using System;
using System.Collections.Generic;
using System.Text;

namespace RegExpressLibrary.Matches.IndexConverters
{
    public sealed class IdentityIndexConverter : IIndexConverter
    {
        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            return (nativeStart, nativeEnd - nativeStart);
        }
    }
}
