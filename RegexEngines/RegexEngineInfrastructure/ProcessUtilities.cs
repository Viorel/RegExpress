﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace RegexEngineInfrastructure
{
	public enum EncodingEnum
	{
		UTF8,
		Unicode,
	}


	public static class ProcessUtilities
	{

		static readonly Encoding Utf8Encoding = new UTF8Encoding( encoderShouldEmitUTF8Identifier: false );
		static readonly Encoding UnicodeEncoding = new UnicodeEncoding( bigEndian: false, byteOrderMark: false, throwOnInvalidBytes: true );


		public static bool InvokeExe( ICancellable cnc, string exePath, string arguments, Action<StreamWriter> stdinWriter, out string stdoutContents, out string stderrContents, EncodingEnum encoding0 )
		{
			var output_sb = new StringBuilder( );
			var error_sb = new StringBuilder( );

			using( Process p = new Process( ) )
			{
				Encoding encoding = GetEncoding( encoding0 );

				p.StartInfo.FileName = exePath;
				p.StartInfo.Arguments = arguments;

				p.StartInfo.UseShellExecute = false;
				p.StartInfo.CreateNoWindow = true;
				p.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;

				p.StartInfo.RedirectStandardInput = true;
				p.StartInfo.RedirectStandardOutput = true;
				p.StartInfo.RedirectStandardError = true;
				p.StartInfo.StandardOutputEncoding = encoding;
				p.StartInfo.StandardErrorEncoding = encoding;

				p.OutputDataReceived += ( s, a ) =>
				{
					output_sb.AppendLine( a.Data );
				};

				p.ErrorDataReceived += ( s, a ) =>
				{
					error_sb.AppendLine( a.Data );
				};

				p.Start( );
				p.BeginOutputReadLine( );
				p.BeginErrorReadLine( );

				using( StreamWriter sw = new StreamWriter( p.StandardInput.BaseStream, encoding ) ) // ('leaveOpen' must be false)
				{
					stdinWriter( sw );
				}

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

				if( cancel )
				{
					try
					{
						p.Kill( );
					}
					catch( Exception exc )
					{
						_ = exc;
						if( Debugger.IsAttached ) Debugger.Break( );

						// ignore
					}

					stdoutContents = null;
					stderrContents = null;

					return false;
				}

				Debug.Assert( done );
			}

			stderrContents = error_sb.ToString( );
			stdoutContents = output_sb.ToString( );

			return true;
		}


		public static bool InvokeExe( ICancellable cnc, string exePath, string arguments, Action<Stream> stdinWriter, out MemoryStream stdoutContents, out string stderrContents, EncodingEnum encoding0 )
		{
			var stdout_ms = new MemoryStream( );
			var error_sb = new StringBuilder( );

			using( Process p = new Process( ) )
			{
				Encoding encoding = GetEncoding( encoding0 );

				p.StartInfo.FileName = exePath;
				p.StartInfo.Arguments = arguments;

				p.StartInfo.UseShellExecute = false;
				p.StartInfo.CreateNoWindow = true;
				p.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;

				p.StartInfo.RedirectStandardInput = true;
				p.StartInfo.RedirectStandardOutput = true;
				p.StartInfo.RedirectStandardError = true;
				p.StartInfo.StandardOutputEncoding = encoding; //?
				p.StartInfo.StandardErrorEncoding = encoding;

				p.ErrorDataReceived += ( s, a ) =>
				{
					error_sb.AppendLine( a.Data );
				};

				p.Start( );
				p.BeginErrorReadLine( );

				Thread writing_thread = new Thread( ( ) =>
				{
					stdinWriter( p.StandardInput.BaseStream );
				} );
				writing_thread.IsBackground = true;
				writing_thread.Start( );

				Thread reading_thread = new Thread( ( ) =>
				{
					p.StandardOutput.BaseStream.CopyTo( stdout_ms );
				} );
				reading_thread.IsBackground = true;
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

				StopThread( writing_thread );
				StopThread( reading_thread );

				if( cancel )
				{
					try
					{
						p.Kill( );
					}
					catch( Win32Exception exc )
					{
						// if error is "Access Denied", the process is terminating

						if( unchecked((uint)exc.HResult) != 0x80004005 ) // 'E_ACCESSDENIED'
						{
							if( Debugger.IsAttached ) Debugger.Break( );
						}
					}
					catch( Exception exc )
					{
						_ = exc;
						if( Debugger.IsAttached ) Debugger.Break( );

						// ignore
					}

					stdoutContents = null;
					stderrContents = null;

					return false;
				}

				Debug.Assert( done );
			}

			stdout_ms.Position = 0;

			stderrContents = error_sb.ToString( );
			stdoutContents = stdout_ms;

			return true;
		}


		public static bool InvokeExe( ICancellable cnc, string exePath, string arguments, string stdinContents, out string stdoutContents, out string stderrContents, EncodingEnum encoding0 )
		{
			return InvokeExe( cnc, exePath, arguments, ( sw ) => sw.Write( stdinContents ), out stdoutContents, out stderrContents, encoding0 );
		}


		private static void StopThread( Thread thread )
		{
			try
			{
				if( !thread.Join( 11 ) ) thread.Interrupt( );
				if( !thread.Join( 11 ) ) thread.Abort( );
				thread.Join( 11 );
			}
			catch( Exception exc )
			{
				_ = exc;
				//?
				// ignore
			}
		}


		private static Encoding GetEncoding( EncodingEnum encoding0 )
		{
			switch( encoding0 )
			{
			case EncodingEnum.UTF8: return Utf8Encoding;
			case EncodingEnum.Unicode: return UnicodeEncoding;
			default:
				throw new NotSupportedException( $"Encoding not supported: {encoding0}" );
			}
		}
	}
}
