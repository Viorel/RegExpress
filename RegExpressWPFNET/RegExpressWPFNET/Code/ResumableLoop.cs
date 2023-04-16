using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using RegExpressLibrary;


namespace RegExpressWPFNET.Code
{
    public sealed class ResumableLoop : ICancellable, IDisposable
    {
        enum Command
        {
            None,
            Terminate,
            Rewind,
            WaitAndExecute,
            Execute,
        }

        readonly AutoResetEvent TerminateEvent = new AutoResetEvent( initialState: false );
        readonly AutoResetEvent RewindEvent = new AutoResetEvent( initialState: false );
        readonly AutoResetEvent WaitAndExecuteEvent = new AutoResetEvent( initialState: false );
        readonly AutoResetEvent ExecuteEvent = new AutoResetEvent( initialState: false );
        readonly AutoResetEvent[] Events;
        readonly Action<ICancellable> Action;
        readonly int[] Timeouts;
        readonly Thread TheThread;
        Command CancellingCommand = Command.None;


        public ResumableLoop( Action<ICancellable> action, int timeout1, int timeout2 = 0, int timeout3 = 0 )
        {
            Debug.Assert( action != null );
            Debug.Assert( timeout1 > 0 );

            Action = action;

            if( timeout2 <= 0 ) timeout2 = timeout1;
            if( timeout3 <= 0 ) timeout3 = timeout2;

            Timeouts = new[] { timeout1, timeout2, timeout3 };

            Events = new[] { TerminateEvent, RewindEvent, WaitAndExecuteEvent, ExecuteEvent };

            TheThread = new Thread( ThreadProc )
            {
                IsBackground = true,
                Priority = ThreadPriority.BelowNormal,
                Name = "rxResumableLoop"
            };

            TheThread.Start( );
        }


        public bool Terminate( int timeoutMs = 333 )
        {
            TerminateEvent.Set( );

            return TheThread.Join( timeoutMs );
        }


        public void SignalRewind( )
        {
            RewindEvent.Set( );
        }


        public void SignalWaitAndExecute( )
        {
            WaitAndExecuteEvent.Set( );
        }


        public void SignalExecute( )
        {
            ExecuteEvent.Set( );
        }


        public ThreadPriority Priority
        {
            set
            {
                TheThread.Priority = value;
            }
        }


        Command WaitForCommand( int timeoutMs )
        {
            int n = WaitHandle.WaitAny( Events, timeoutMs );

            switch( n )
            {
            case 0:
                return Command.Terminate;
            case 1:
                return Command.Rewind;
            case 2:
                return Command.WaitAndExecute;
            case 3:
                return Command.Execute;
            case WaitHandle.WaitTimeout:
                break;
            default:
                Debug.Assert( false );
                break;
            }

            return Command.None;
        }


        void ThreadProc( )
        {
            try
            {
                for(; ; )
                {
                    Command command;

                    if( CancellingCommand == Command.None )
                    {
                        command = WaitForCommand( -1 );
                    }
                    else
                    {
                        command = WaitForCommand( 0 );

                        if( command == Command.None ) command = CancellingCommand;

                        CancellingCommand = Command.None;
                    }

                    if( command == Command.Terminate ) break;
                    if( command == Command.Rewind ) continue;

                    Debug.Assert( command == Command.WaitAndExecute || command == Command.Execute );

                    if( command == Command.WaitAndExecute )
                    {
                        // wait for other commands that might cancel the intention for execution;
                        // if another 'WaitAndExecute', then repeat the pause

                        for( var i = 0; ; i = Math.Min( i + 1, Timeouts.Length - 1 ) )
                        {
                            command = WaitForCommand( Timeouts[i] );

                            if( command != Command.WaitAndExecute ) break;
                        }

                        if( command == Command.Terminate ) break;
                        if( command == Command.Rewind ) continue;
                    }

                    Debug.Assert( command == Command.None || command == Command.Execute );
                    Debug.Assert( CancellingCommand == Command.None );

                    try
                    {
                        Action( this ); //
                    }
                    catch( OperationCanceledException ) // also 'TaskCanceledException'
                    {
                    }
                    catch( Exception exc )
                    {
                        _ = exc;
                        if( Debugger.IsAttached ) Debugger.Break( );

                        throw; // TODO: maybe restart the loop?
                    }
                }
            }
            catch( ThreadInterruptedException )
            {
                // ignore
            }
            catch( ThreadAbortException )
            {
                // ignore
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );
                throw;
            }
        }


        #region ICancellable

        public bool IsCancellationRequested
        {
            get
            {
                if( CancellingCommand == Command.None )
                {
                    CancellingCommand = WaitForCommand( 0 );
                }

                return CancellingCommand != Command.None;
            }
        }

        #endregion ICancellable


        #region IDisposable Support

        private bool disposedValue = false; // To detect redundant calls

        void Dispose( bool disposing )
        {
            if( !disposedValue )
            {
                if( disposing )
                {
                    // TODO: dispose managed state (managed objects).

                    using( TerminateEvent ) { }
                    using( RewindEvent ) { }
                    using( WaitAndExecuteEvent ) { }
                    using( ExecuteEvent ) { }
                }

                // TODO: free unmanaged resources (unmanaged objects) and override a finalizer below.
                // TODO: set large fields to null.

                disposedValue = true;
            }
        }

        // TODO: override a finalizer only if Dispose(bool disposing) above has code to free unmanaged resources.
        // ~ResumableLoop()
        // {
        //   // Do not change this code. Put cleanup code in Dispose(bool disposing) above.
        //   Dispose(false);
        // }

        // This code added to correctly implement the disposable pattern.
        public void Dispose( )
        {
            // Do not change this code. Put cleanup code in Dispose(bool disposing) above.
            Dispose( true );
            // TODO: uncomment the following line if the finalizer is overridden above.
            // GC.SuppressFinalize(this);
        }

        #endregion IDisposable Support
    }
}
