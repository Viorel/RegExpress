using System;
using System.Collections.Generic;


namespace RegExpressLibrary
{
    public sealed class LazyData<K, T>
        where K : struct
        where T : class
    {
        readonly Func<K, T> mInit;
        readonly Lazy<Dictionary<K, T>> mCache = new( );


        public LazyData( Func<K, T> init )
        {
            mInit = init;
        }

        public T GetValue( K k )
        {
            lock( this )
            {
                var cache = mCache.Value;

                T? value;
                if( cache.TryGetValue( k, out value ) )
                {
                    return value;
                }

                value = mInit( k );
                cache.Add( k, value );

                return value;
            }
        }
    }
}
