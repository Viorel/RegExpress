using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Threading;


namespace RegExpressWPFNET.Code
{
	// See: https://www.codeproject.com/articles/692963/how-to-get-rid-of-dispatcher-invoke, with adjustments

	static class UITaskHelper
	{

		/// <summary>
		/// Invoke action on UI thread. Should not be called from UI thread.
		/// </summary>
		/// <param name="obj"></param>
		/// <param name="ct"></param>
		/// <param name="action"></param>
		public static void Invoke( DispatcherObject obj, CancellationToken ct, Action action )
		{
			Debug.Assert( !obj.Dispatcher.CheckAccess( ) );

			ct.ThrowIfCancellationRequested( );

			try
			{
				obj.Dispatcher.Invoke(
					( ) => Execute( action ),
					DispatcherPriority.Background,
					ct );
			}
			catch( OperationCanceledException exc ) // also 'TaskCanceledException'
			{
				_ = exc;
				throw;
			}
			catch( ThreadInterruptedException exc )
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


		public static void Invoke( DispatcherObject obj, Action action )
		{
			Debug.Assert( !obj.Dispatcher.CheckAccess( ) );

			try
			{
				obj.Dispatcher.Invoke(
					( ) => Execute( action ),
					DispatcherPriority.Background );
			}
			catch( Exception exc )
			{
				_ = exc;
				if(!( exc is TaskCanceledException ) )
                    RegExpressLibrary.InternalConfig.HandleException( exc );
				throw;
			}
		}


		/// <summary>
		/// Begin an action on UI thread. Should not be called from UI thread.
		/// Use 'task.Wait()' to wait for termination.
		/// </summary>
		/// <param name="ct"></param>
		/// <param name="action"></param>
		/// <returns></returns>
		public static Task BeginInvoke( DispatcherObject obj, CancellationToken ct, Action action )
		{
			Debug.Assert( !obj.Dispatcher.CheckAccess( ) );

			ct.ThrowIfCancellationRequested( );

			return obj.Dispatcher.InvokeAsync(
				( ) => Execute( action ),
				DispatcherPriority.Background,
				ct ).Task;
		}


		public static Task BeginInvoke( DispatcherObject obj, Action action )
		{
			Debug.Assert( !obj.Dispatcher.CheckAccess( ) );

			return obj.Dispatcher.InvokeAsync(
				( ) => Execute( action ),
				DispatcherPriority.Background
				).Task;
		}


		static void Execute( Action action )
		{
			try
			{
				action( );
			}
			catch( OperationCanceledException exc ) // also 'TaskCanceledException'
			{
				Utilities.DbgSimpleLog( exc );

				throw;//
			}
			catch( Exception exc )
			{
				_ = exc;
				RegExpressLibrary.InternalConfig.HandleException( exc );
				throw;
			}
		}
	}
}
