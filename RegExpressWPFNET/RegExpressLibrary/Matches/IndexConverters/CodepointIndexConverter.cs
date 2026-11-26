namespace RegExpressLibrary.Matches.IndexConverters;

public sealed class CodepointIndexConverter( string text ) : IndexConverter
{
    readonly SurrogatePairsHelper Sph = new( text );

    public override Coordinates ConvertIndexLength( int nativeIndex, int nativeLength )
    {
        (int charIndex, int charLength) = Sph.ToCharIndexAndLength( nativeIndex, nativeLength );

        return new Coordinates( nativeIndex, nativeLength, charIndex, charLength );
    }
}
