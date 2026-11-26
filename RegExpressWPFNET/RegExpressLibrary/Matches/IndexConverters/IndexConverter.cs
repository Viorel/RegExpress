namespace RegExpressLibrary.Matches.IndexConverters;

public abstract class IndexConverter
{
    public abstract Coordinates ConvertIndexLength( int nativeIndex, int nativeLength );

    public Coordinates ConvertStartEnd( int nativeStart, int nativeEnd )
    {
        return ConvertIndexLength( nativeStart, nativeEnd - nativeStart );
    }
}
