using System;
using System.Collections.Generic;
using System.Text;

namespace RegExpressLibrary.Matches;

public struct Coordinates
{
    public int NativeIndex { get; }
    public int NativeLength { get; }
    public int CharIndex { get; }
    public int CharLength { get; }

    public Coordinates( int nativeIndex, int nativeLength, int charIndex, int charLength )
    {
        NativeIndex = nativeIndex;
        NativeLength = nativeLength;
        CharIndex = charIndex;
        CharLength = charLength;
    }
}
