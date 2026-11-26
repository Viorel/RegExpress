namespace RegExpressLibrary.Matches.IndexConverters;

public sealed class IdentityIndexConverter : IndexConverter
{
    public static IdentityIndexConverter Instance { get; } = new IdentityIndexConverter( );

    private IdentityIndexConverter( )
    {
    }

    public override Coordinates ConvertIndexLength( int nativeStart, int nativeLength )
    {
        return new Coordinates( nativeStart, nativeLength, nativeStart, nativeLength );
    }
}
