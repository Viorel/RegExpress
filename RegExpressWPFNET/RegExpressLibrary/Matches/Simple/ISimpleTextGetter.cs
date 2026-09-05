namespace RegExpressLibrary.Matches.Simple
{
    public interface ISimpleTextGetter
    {
        void ThrowIfInvalid( int index, int length );

        string GetText( int index, int length );
    }
}
