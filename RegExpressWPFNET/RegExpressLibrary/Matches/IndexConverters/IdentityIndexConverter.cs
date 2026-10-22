namespace RegExpressLibrary.Matches.IndexConverters
{
    public sealed class IdentityIndexConverter : IIndexConverter
    {
        public static IdentityIndexConverter Instance { get; } = new IdentityIndexConverter( );

        private IdentityIndexConverter( )
        {
        }
        
        public (int index, int length) Convert( int nativeStart, int nativeEnd )
        {
            return (nativeStart, nativeEnd - nativeStart);
        }
    }
}
