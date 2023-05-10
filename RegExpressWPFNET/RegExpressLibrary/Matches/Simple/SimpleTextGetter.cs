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

        public string GetText( int index, int length )
        {
            return Text.Substring( index, length );
        }

        #endregion ISimpleTextGetter

    }


    public sealed class SimpleTextGetterWithOffset : ISimpleTextGetter
    {
        readonly string Text;
        readonly int Offset;

        public SimpleTextGetterWithOffset( int offset, string text )
        {
            Text = text;
            Offset = offset;
        }


        #region ISimpleTextGetter

        public string GetText( int index, int length )
        {
            return Text.Substring( index - Offset, length );
        }

        #endregion ISimpleTextGetter
    }

}
