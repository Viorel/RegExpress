using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    public enum EncodingEnum
    {
        None,
        ASCII,
        UTF8,
        Unicode,
    }


    public sealed class ProcessHelper : IDisposable
    {
        static readonly Encoding Utf8Encoding = new UTF8Encoding( encoderShouldEmitUTF8Identifier: false );
        static readonly Encoding UnicodeEncoding = new UnicodeEncoding( bigEndian: false, byteOrderMark: false, throwOnInvalidBytes: true );

        readonly string FileName;
        bool ProcessExecuted = false;
        readonly MemoryStream OutputMemoryStream = new( );
        string? ErrorContents = null;
        BinaryReader? BinaryReaderObj = null;
        StreamReader? StreamReaderObj = null;
        Stream? OutputStreamObj = null;

        public EncodingEnum InputEncoding { get; set; }
        public EncodingEnum OutputEncoding { get; set; }
        public EncodingEnum ErrorEncoding { get; set; }

        public EncodingEnum AllEncoding
        {
            set
            {
                InputEncoding = OutputEncoding = ErrorEncoding = value;
            }
        }

        public Action<BinaryWriter>? BinaryWriter { get; set; }
        public Action<StreamWriter>? StreamWriter { get; set; }

        public string[]? Arguments { get; set; }


        public ProcessHelper( string fileName )
        {
            if( string.IsNullOrWhiteSpace( fileName ) ) throw new ArgumentException( nameof( fileName ) );

            FileName = fileName;
        }


        public bool Start( ICancellable cnc )
        {
            if( BinaryWriter != null && StreamWriter != null ) throw new InvalidOperationException( "Binary Writer and Stream Writer cannot be both set." );
            if( ( BinaryWriter != null || StreamWriter != null ) && InputEncoding == EncodingEnum.None ) throw new InvalidOperationException( "Input Encoding was not set." );

            ProcessExecuted = false;
            OutputMemoryStream.SetLength( 0 );
            ErrorContents = null;

            using Process p = new( );

            if( Arguments != null )
            {
                foreach( var arg in Arguments )
                {
                    p.StartInfo.ArgumentList.Add( arg );
                }
            }

            p.StartInfo.CreateNoWindow = true;
            //p.StartInfo.Domain
            //p.StartInfo.Environment
            //p.StartInfo.EnvironmentVariables
            p.StartInfo.ErrorDialog = false;
            //p.StartInfo.ErrorDialogParentHandle
            p.StartInfo.FileName = FileName;
            //p.StartInfo.LoadUserProfile 
            //p.StartInfo.Password
            //p.StartInfo.PasswordInClearText
            p.StartInfo.RedirectStandardError = true;
            p.StartInfo.RedirectStandardInput = true;
            p.StartInfo.RedirectStandardOutput = true;
            p.StartInfo.StandardErrorEncoding = ErrorEncoding == EncodingEnum.None ? Encoding.ASCII : GetEncoding( ErrorEncoding );
            p.StartInfo.StandardInputEncoding = InputEncoding == EncodingEnum.None ? Encoding.ASCII : GetEncoding( InputEncoding );
            p.StartInfo.StandardOutputEncoding = OutputEncoding == EncodingEnum.None ? Encoding.ASCII : GetEncoding( OutputEncoding );
            //p.StartInfo.UserName
            p.StartInfo.UseShellExecute = false;
            //p.StartInfo.Verb
            //p.StartInfo.Verbs
            p.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;
            //p.StartInfo.WorkingDirectory

            StringBuilder sb_errors = new( );

            p.ErrorDataReceived += ( s, a ) =>
            {
                sb_errors.AppendLine( a.Data );
            };

            p.Start( );
            p.BeginErrorReadLine( );

            Thread? writing_thread = null;

            if( BinaryWriter != null )
            {
                Debug.Assert( writing_thread == null );

                writing_thread = new Thread( ( ) =>
                {
                    try
                    {
                        using( BinaryWriter bw = new( p.StandardInput.BaseStream, GetEncoding( InputEncoding ), leaveOpen: false ) )
                        {
                            BinaryWriter( bw );
                        }
                    }
                    catch( Exception exc )
                    {
                        _ = exc;
                        if( Debugger.IsAttached ) Debugger.Break( );
                    }
                } )
                {
                    Name = "rxStdinWriting",
                    IsBackground = true
                };
                writing_thread.Start( );
            }

            if( StreamWriter != null )
            {
                Debug.Assert( writing_thread == null );

                writing_thread = new Thread( ( ) =>
                {
                    try
                    {
                        using( StreamWriter sw = new( p.StandardInput.BaseStream, GetEncoding( InputEncoding ), leaveOpen: false ) )
                        {
                            StreamWriter( sw );
                        }
                    }
                    catch( Exception exc )
                    {
                        _ = exc;
                        if( Debugger.IsAttached ) Debugger.Break( );
                    }
                } )
                {
                    Name = "rxStdinWriting",
                    IsBackground = true
                };
                writing_thread.Start( );
            }

            Thread reading_thread = new Thread( ( ) =>
            {
                try
                {
                    p.StandardOutput.BaseStream.CopyTo( OutputMemoryStream );
                }
                catch( Exception exc )
                {
                    _ = exc;
                    if( Debugger.IsAttached ) Debugger.Break( );
                    throw;
                }
            } )
            {
                Name = "rxStdoutReading",
                IsBackground = true
            };
            reading_thread.Start( );

            bool cancel = false;
            bool done = false;

            for(; ; )
            {
                cancel = cnc.IsCancellationRequested;
                if( cancel ) break;

                done = p.WaitForExit( 444 );
                if( done )
                {
                    // another 'WaitForExit' required to finish the processing of streams;
                    // see: https://stackoverflow.com/questions/9533070/how-to-read-to-end-process-output-asynchronously-in-c,
                    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.process.waitforexit

                    p.WaitForExit( );

                    break;
                }
            }

            //#if DEBUG
            //  if( p.WaitForExit( 0 ) ) Debug.WriteLine( $"Exit code: {p.ExitCode} -- {FileName}" );
            //#endif

            if( writing_thread != null ) StopThread( writing_thread );
            StopThread( reading_thread );

            if( cancel )
            {
                try
                {
                    p.Kill( );
                }
                catch( Exception exc )
                {
                    if( unchecked((uint)exc.HResult) != 0x80004005 && // 'E_ACCESSDENIED'
                        unchecked((uint)exc.HResult) != 0x80131509 ) // -2146233079, "Cannot process request because the process (<PID>) has exited."
                    {
                        if( Debugger.IsAttached ) Debugger.Break( );
                    }

                    // ignore
                }

                return false;
            }

            Debug.Assert( done );

            ErrorContents = sb_errors.ToString( );
            ProcessExecuted = true;

            return !cnc.IsCancellationRequested;
        }


        public BinaryReader BinaryReader
        {
            get
            {
                if( BinaryReaderObj == null )
                {
                    if( !ProcessExecuted ) throw new InvalidOperationException( "Process not executed" );
                    if( StreamReaderObj != null || OutputStreamObj != null ) throw new InvalidOperationException( "Output already got." );

                    OutputMemoryStream.Position = 0;
                    BinaryReaderObj = new BinaryReader( OutputMemoryStream, GetEncoding( OutputEncoding ), leaveOpen: false );
                }

                return BinaryReaderObj;
            }
        }


        public StreamReader StreamReader
        {
            get
            {
                if( StreamReaderObj == null )
                {
                    if( !ProcessExecuted ) throw new InvalidOperationException( "Process not executed" );
                    if( BinaryReaderObj != null || OutputStreamObj != null ) throw new InvalidOperationException( "Output already got." );

                    OutputMemoryStream.Position = 0;
                    StreamReaderObj = new StreamReader( OutputMemoryStream, GetEncoding( OutputEncoding ), leaveOpen: false );
                }

                return StreamReaderObj;
            }
        }


        public Stream OutputStream
        {
            get
            {
                if( OutputStreamObj == null )
                {
                    if( !ProcessExecuted ) throw new InvalidOperationException( "Process not executed" );
                    if( BinaryReaderObj != null || StreamReaderObj != null ) throw new InvalidOperationException( "Output already got." );

                    OutputMemoryStream.Position = 0;
                    OutputStreamObj = OutputMemoryStream;
                }

                return OutputStreamObj;
            }
        }


        public string? Error
        {
            get
            {
                if( !ProcessExecuted ) throw new InvalidOperationException( "Process not executed" );

                return ErrorContents;
            }
        }


        static void StopThread( Thread thread )
        {
            try
            {
                if( !thread.Join( 11 ) ) thread.Interrupt( );
                // NOT SUPPORTED: if( !thread.Join( 11 ) ) thread.Abort( );
                thread.Join( 11 );
            }
            catch( Exception exc )
            {
                _ = exc;
                //?
                // ignore
            }
        }


        static Encoding GetEncoding( EncodingEnum encoding )
        {
            switch( encoding )
            {
            case EncodingEnum.None: throw new InvalidOperationException( $"Encoding not specified" );
            case EncodingEnum.ASCII: return Encoding.ASCII;
            case EncodingEnum.UTF8: return Utf8Encoding;
            case EncodingEnum.Unicode: return UnicodeEncoding;
            default:
                throw new NotSupportedException( $"Encoding not supported: {encoding}" );
            }
        }


        #region IDisposable

        private bool disposedValue;

        private void Dispose( bool disposing )
        {
            if( !disposedValue )
            {
                if( disposing )
                {
                    // TODO: dispose managed state (managed objects)

                    OutputMemoryStream?.Dispose( );
                    BinaryReaderObj?.Dispose( );
                    StreamReaderObj?.Dispose( );
                }

                // TODO: free unmanaged resources (unmanaged objects) and override finalizer
                // TODO: set large fields to null
                disposedValue = true;
            }
        }

        // // TODO: override finalizer only if 'Dispose(bool disposing)' has code to free unmanaged resources
        // ~ProcessHelper()
        // {
        //     // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        //     Dispose(disposing: false);
        // }

        public void Dispose( )
        {
            // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
            Dispose( disposing: true );
            GC.SuppressFinalize( this );
        }

        #endregion IDisposable
    }
}
