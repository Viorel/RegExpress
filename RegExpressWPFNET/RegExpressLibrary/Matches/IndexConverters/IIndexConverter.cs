using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary.Matches.IndexConverters
{
    public interface IIndexConverter
    {
        (int index, int length) Convert( int nativeStart, int nativeEnd );
    }
}
