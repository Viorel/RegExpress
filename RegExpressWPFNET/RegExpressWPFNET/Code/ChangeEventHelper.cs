using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;


namespace RegExpressWPFNET.Code
{
    public sealed class ChangeEventHelper
    {
        int mChangeIndex = 0;
        readonly RichTextBox mRtb;
        //bool mIsFocused = false;


        public ChangeEventHelper( RichTextBox rtb )
        {
            mRtb = rtb;

            //mUIElement.GotFocus += UIElement_GotFocus;
            //mUIElement.LostFocus += UIElement_LostFocus;

            // TODO: consider "-="
        }


        public bool IsInChange => mChangeIndex != 0;


        public Task BeginInvoke( CancellationToken ct, Action action )
        {
            Debug.Assert( !mRtb.Dispatcher.CheckAccess( ) ); // (should not happen, but the code handles it)

            ct.ThrowIfCancellationRequested( );

            if( !mRtb.Dispatcher.CheckAccess( ) )
            {
                return mRtb.Dispatcher.InvokeAsync( ( ) =>
                    {
                        ct.ThrowIfCancellationRequested( );

                        Do( action );
                    },
                    DispatcherPriority.Background,
                    ct ).Task;
            }
            else
            {
                Do( action );

                return Task.CompletedTask;
            }
        }


        public void Invoke( CancellationToken ct, Action action )
        {
            Debug.Assert( !mRtb.Dispatcher.CheckAccess( ) ); // (should not happen, but the code handles it)

            ct.ThrowIfCancellationRequested( );

            if( !mRtb.Dispatcher.CheckAccess( ) )
            {
                try
                {
                    mRtb.Dispatcher.Invoke(
                        ( ) => Do( action ),
                        DispatcherPriority.Background,
                        ct );
                }
                catch( OperationCanceledException exc ) // also 'TaskCanceledException'
                {
                    _ = exc;
                    throw;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    RegExpressLibrary.InternalConfig.HandleException( exc );
                    throw;
                }
            }
            else
            {
                Do( action );
            }
        }


        public void Do( Action action )
        {
            Debug.Assert( action != null );

            Interlocked.Increment( ref mChangeIndex );
            mRtb.BeginChange( );
            try
            {
                action( );
            }
            catch( Exception exc )
            {
                _ = exc;
                RegExpressLibrary.InternalConfig.HandleException( exc );
                throw;
            }
            finally
            {
                mRtb.EndChange( );
                Interlocked.Decrement( ref mChangeIndex );
            }
        }


        //private void UIElement_GotFocus( object sender, RoutedEventArgs e )
        //{
        //    mIsFocused = true;
        //}


        //private void UIElement_LostFocus( object sender, RoutedEventArgs e )
        //{
        //    mIsFocused = false;
        //}

    }
}
