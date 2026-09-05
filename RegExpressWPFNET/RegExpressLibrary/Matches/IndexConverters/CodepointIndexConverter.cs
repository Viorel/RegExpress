namespace RegExpressLibrary.Matches.IndexConverters
{
    public sealed class CodepointIndexConverter( string text ) : IIndexConverter
    {
        readonly SurrogatePairsHelper Sph = new( text );

        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            int native_length = nativeEnd - nativeStart;

            return Sph.ToCharIndexAndLength( nativeStart, native_length );
        }
    }
}
