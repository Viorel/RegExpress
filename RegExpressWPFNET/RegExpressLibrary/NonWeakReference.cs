using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary;

/// <summary>
/// Replaces WeakReference<T> to keep the data. For experiments only.
/// </summary>
/// <typeparam name="T"></typeparam>
public class NonWeakReference<T> where T : class?
{
    T? mTarget;

    public NonWeakReference( )
    {
        this.mTarget = null;
    }

    public NonWeakReference( T target )
    {
        this.mTarget = target;
    }

    public bool TryGetTarget( out T target )
    {
        target = mTarget;

        return target != null;
    }

    public void SetTarget( T target )
    {
        if( target == null )
        {
            int x = 0;
        }

        this.mTarget = target;
    }
}
