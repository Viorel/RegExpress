using System;
using System.Diagnostics;


namespace RegExpressLibrary.Matches.Simple
{
    public abstract class SimpleBase
    {
        protected readonly ISimpleTextGetter TextGetter;

        protected SimpleBase( int nativeIndex, int nativeLength, int charIndex, int charLength, ISimpleTextGetter textGetter )
        {
            Debug.Assert( textGetter != null );

            NativeIndex = nativeIndex;
            NativeLength = nativeLength;
            CharIndex = charIndex;
            CharLength = charLength;
            TextGetter = textGetter;
        }


        public int NativeIndex { get; }

        public int NativeLength { get; }

        public int CharIndex { get; }

        public int CharLength { get; }

        public string Value => CharIndex < 0 ? String.Empty : TextGetter.GetText( CharIndex, CharLength );
    }
}
