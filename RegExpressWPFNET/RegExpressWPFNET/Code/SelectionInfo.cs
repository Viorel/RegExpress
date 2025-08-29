using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressWPFNET.Code
{
    internal record struct SelectionInfo( int Start, int End )
    {
        internal int Length => Math.Abs( Start - End );

        public override string ToString( ) => $"{Start}..{End}";
    }
}
