using System;

namespace RegExpressLibrary.Matches.Simple
{
    public sealed class TrivialTextGetter : ISimpleTextGetter
    {
        readonly string mValue;

        public TrivialTextGetter( string value )
        {
            this.mValue = value;
        }

        #region ISimpleTextGetter

        public void ThrowIfInvalid( int index, int length )
        {
            if( length != mValue.Length ) throw new ArgumentException( $"Invalid length: {length}, expected: {mValue.Length}" );
        }

        public string GetText( int index, int length )
        {
            return mValue;
        }

        #endregion ISimpleTextGetter
    }
}
