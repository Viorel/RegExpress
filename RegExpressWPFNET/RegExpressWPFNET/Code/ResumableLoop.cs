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
        enum CommandEnum : int
        {
            None,
            Execute,
            WaitAndExecute,
            Rewind,
            Terminate,
        }

        readonly AutoResetEvent NotificationEvent = new( initialState: false );
        readonly Action<ICancellable> Action;
        readonly int[] Timeouts;
        readonly Thread TheThread;
        volatile CommandEnum Command = CommandEnum.None;
        readonly Lock Locker = new( );

#if DEBUG
        readonly int CreatorManagedThreadId;
#endif

        public ResumableLoop( string name, Action<ICancellable> action, int timeout1, int timeout2 = 0, int timeout3 = 0 )
        {
            Debug.Assert( action != null );
            Debug.Assert( timeout1 > 0 );

            Action = action;

            if( timeout2 <= 0 ) timeout2 = timeout1;
            if( timeout3 <= 0 ) timeout3 = timeout2;

            Timeouts = [timeout1, timeout2, timeout3];

            TheThread = new Thread( ThreadProc )
            {
                IsBackground = true,
                Priority = ThreadPriority.BelowNormal,
                Name = "RL: " + name,
            };

#if DEBUG
            CreatorManagedThreadId = Environment.CurrentManagedThreadId;
#endif

            TheThread.Start( );
        }

        public bool Terminate( int timeoutMs = 333 )
        {
            Signal( CommandEnum.Terminate );

            return TheThread.Join( timeoutMs );
        }

        public void SignalRewind( )
        {
            Signal( CommandEnum.Rewind );
        }

        public void SignalWaitAndExecute( )
        {
            Signal( CommandEnum.WaitAndExecute );
        }

        public void SignalExecute( )
        {
            Signal( CommandEnum.Execute );
        }

        void Signal( CommandEnum commandToSignal )
        {
#if DEBUG
            Debug.Assert( CreatorManagedThreadId == Environment.CurrentManagedThreadId );
#endif
            Debug.Assert( commandToSignal != CommandEnum.None );

            lock( Locker )
            {
                CommandEnum combined_command = Combine( Command, commandToSignal );
                Debug.Assert( combined_command != CommandEnum.None );

                if( Command != combined_command )
                {
                    Command = combined_command;
                    Thread.MemoryBarrier( ); //
                    NotificationEvent.Set( );
                }
            }
        }

        static CommandEnum Combine( CommandEnum oldCommand, CommandEnum newCommand )
        {
            Debug.Assert( newCommand != CommandEnum.None );

            if( oldCommand == CommandEnum.None ) return newCommand;

            if( oldCommand == CommandEnum.Terminate ) return CommandEnum.Terminate;

            if( newCommand == CommandEnum.Terminate ) return CommandEnum.Terminate;

            if( newCommand == CommandEnum.Rewind ) return CommandEnum.Rewind;

            if( newCommand == CommandEnum.WaitAndExecute )
            {
                if( oldCommand == CommandEnum.Execute ) return CommandEnum.Execute;

                return CommandEnum.WaitAndExecute;
            }

            Debug.Assert( newCommand == CommandEnum.Execute );

            return CommandEnum.Execute;
        }

        public ThreadPriority Priority
        {
            set
            {
                TheThread.Priority = value;
            }
        }

        void ThreadProc( )
        {
#if DEBUG
            Debug.Assert( CreatorManagedThreadId != Environment.CurrentManagedThreadId );
#endif

            try
            {
                for(; ; )
                {
                    NotificationEvent.WaitOne( Timeout.Infinite );

                    CommandEnum command;

                    lock( Locker ) (command, Command) = (Command, CommandEnum.None);

                    Debug.Assert( command != CommandEnum.None );

                    if( command == CommandEnum.Terminate ) break;
                    if( command == CommandEnum.Rewind ) continue;

                    Debug.Assert( command == CommandEnum.Execute || command == CommandEnum.WaitAndExecute );

                    if( command == CommandEnum.WaitAndExecute )
                    {
                        // wait for other commands that could override the execution intent;
                        // if another 'WaitAndExecute', then repeat the pause

                        for( var i = 0; ; i = Math.Min( i + 1, Timeouts.Length - 1 ) )
                        {
                            if( NotificationEvent.WaitOne( Timeouts[i] ) )
                            {
                                lock( Locker ) (command, Command) = (Command, CommandEnum.None);

                                if( command != CommandEnum.WaitAndExecute ) break;
                            }
                            else
                            {
                                // no disturbing event

                                command = CommandEnum.None;

                                break;
                            }
                        }

                        if( command == CommandEnum.Terminate ) break;
                        if( command == CommandEnum.Rewind ) continue;
                    }

                    Debug.Assert( command == CommandEnum.None || command == CommandEnum.Execute );

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
                return Command != CommandEnum.None; // (atomic)
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

                    using( NotificationEvent ) { }
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
