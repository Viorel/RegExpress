using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    public interface ICancellable
    {
        bool IsCancellationRequested { get; }

        public static ICancellable NonCancellable => RegExpressLibrary.NonCancellable.Instance;
    }


    public sealed class NonCancellable : ICancellable
    {
        public static readonly ICancellable Instance = new NonCancellable( );

        private NonCancellable( )
        {

        }

        #region ICancellable

        public bool IsCancellationRequested
        {
            get
            {
                return false;
            }
        }

        #endregion ICancellable
    }

    public sealed class SimpleCancellable : ICancellable
    {
        bool mCancel = false;

        public void SetCancel( ) => mCancel = true;

        #region ICancellable

        public bool IsCancellationRequested => mCancel;

        #endregion ICancellable
    }
}
