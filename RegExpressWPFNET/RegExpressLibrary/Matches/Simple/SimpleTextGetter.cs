using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
    public sealed class SimpleTextGetter : ISimpleTextGetter
    {
        readonly string Text;

        public SimpleTextGetter( string text )
        {
            Text = text;
        }


        #region ISimpleTextGetter

        public void ThrowIfInvalid( int index, int length )
        {
            if( index < 0 ) throw new ArgumentException( $"Negative index: {index}" );
            if( length < 0 ) throw new ArgumentException( $"Negative length: {length}" );
            if( index > Text.Length ) throw new ArgumentException( $"Index too large: {index}, text length: {Text.Length}" );
            if( index + length > Text.Length ) throw new ArgumentException( $"Index+length too large. Index: {index}, length: {length}, text length: {Text.Length}" );
        }

        public string GetText( int index, int length )
        {
            return Text.Substring( index, length );
        }

        #endregion ISimpleTextGetter
    }
}
