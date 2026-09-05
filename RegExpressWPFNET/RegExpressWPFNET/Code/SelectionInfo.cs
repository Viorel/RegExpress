using System;

namespace RegExpressWPFNET.Code
{
    internal record struct SelectionInfo( int Start, int End )
    {
        internal int Length => Math.Abs( Start - End );

        public override string ToString( ) => $"{Start}..{End}";
    }
}
