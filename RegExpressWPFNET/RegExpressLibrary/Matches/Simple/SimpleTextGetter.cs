using System;


namespace RegExpressLibrary.Matches.Simple;

public sealed class SimpleTextGetter : ISimpleTextGetter
{
    readonly string Text;

    public SimpleTextGetter( string text )
    {
        Text = text;
    }


    #region ISimpleTextGetter

    public void ThrowIfInvalid( int charIndex, int charLength )
    {
        if( charIndex < 0 ) throw new ArgumentException( $"Negative index: {charIndex}" );
        if( charLength < 0 ) throw new ArgumentException( $"Negative length: {charLength}" );
        if( charIndex > Text.Length ) throw new ArgumentException( $"Index too large: {charIndex}, text length: {Text.Length}" );
        if( charIndex + charLength > Text.Length ) throw new ArgumentException( $"Index+length too large. Index: {charIndex}, length: {charLength}, text length: {Text.Length}" );
    }

    public string GetText( int charIndex, int charLength )
    {
        return Text.Substring( charIndex, charLength );
    }

    #endregion ISimpleTextGetter
}
