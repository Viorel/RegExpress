using System;
using System.Collections.Generic;


namespace RegExpressLibrary
{
    public sealed class LazyData<A1, T> where A1 : struct
    {
        readonly Func<A1, T?> mInit;
        readonly Lazy<Dictionary<A1, T?>> mCache = new( );


        public LazyData( Func<A1, T?> init )
        {
            mInit = init;
        }

        public T? GetValue( A1 a1 )
        {
            lock( this )
            {
                var cache = mCache.Value;

                T? value;
                if( cache.TryGetValue( a1, out value ) )
                {
                    return value;
                }

                value = mInit( a1 );
                cache.Add( a1, value );

                return value;
            }
        }
    }
}
