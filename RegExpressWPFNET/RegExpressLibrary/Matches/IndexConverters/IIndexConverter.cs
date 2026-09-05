namespace RegExpressLibrary.Matches.IndexConverters
{
    public interface IIndexConverter
    {
        (int index, int length) Convert( int nativeStart, int nativeEnd );
    }
}
