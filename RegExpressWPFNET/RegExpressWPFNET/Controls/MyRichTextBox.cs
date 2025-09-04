using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using RegExpressLibrary;
using RegExpressWPFNET.Code;


namespace RegExpressWPFNET.Controls
{
    internal class MyRichTextBox : RichTextBox
    {
        readonly WeakReference<TextData?>[] mCachedTextData = [new WeakReference<TextData?>( null ), new WeakReference<TextData?>( null ), new WeakReference<TextData?>( null )];
        readonly SelectionInfo[] mCachedSelection = [new SelectionInfo( -1, -1 ), new SelectionInfo( -1, -1 ), new SelectionInfo( -1, -1 )];

        static readonly RoutedUICommand[] CommandsToDisable =
            [
                EditingCommands.ToggleBold,
                EditingCommands.ToggleBullets,
                EditingCommands.ToggleItalic,
                EditingCommands.ToggleNumbering,
                EditingCommands.ToggleSubscript,
                EditingCommands.ToggleSuperscript,
                EditingCommands.ToggleUnderline,
                EditingCommands.DecreaseFontSize,
                EditingCommands.IncreaseFontSize,
                EditingCommands.AlignCenter,
                EditingCommands.AlignJustify,
                EditingCommands.AlignLeft,
                EditingCommands.AlignRight,
                EditingCommands.IncreaseIndentation,
                EditingCommands.DecreaseIndentation,
            ];

        public ChangeEventHelper ChangeEventHelper { get; private init; }

        public MyRichTextBox( )
        {
            ChangeEventHelper = new ChangeEventHelper( this );

            AddCommandBindings( );
        }

        public MyRichTextBox( FlowDocument document ) : base( document )
        {
            ChangeEventHelper = new ChangeEventHelper( this );

            AddCommandBindings( );
        }

        internal TextData GetTextData( string? eol )//, [CallerMemberName] string? caller = null, [CallerFilePath] string? callerPath = null, [CallerLineNumber] int callerLine = 0 )
        {
#if DEBUG
            Debug.Assert( Document.Parent == this );

            if( eol != null ) TextData.DbgValidateEol( eol );
#endif

            //var t1 = Environment.TickCount;

            TextData? td;

            if( eol == null )
            {
                // get any

                foreach( var wr in mCachedTextData )
                {
                    if( wr.TryGetTarget( out td ) && td != null )
                    {
                        Debug.Assert( td.TextPointers.Doc == Document );

                        return td;
                    }
                }

                eol = "\r\n";
            }

            int eol_index = EolToIndex( eol );

            if( mCachedTextData[eol_index].TryGetTarget( out td ) && td != null )
            {
                Debug.Assert( td.TextPointers.Doc == Document );

                return td;
            }

            foreach( var wr in mCachedTextData ) // TODO: skip the checked one
            {
                if( wr.TryGetTarget( out td ) && td != null )
                {
                    TextData new_td = td.Export( eol );

                    Debug.Assert( new_td.TextPointers.Doc == Document );

                    mCachedTextData[eol_index].SetTarget( new_td );

                    return new_td;
                }
            }

            RtbTextHelper th = new( Document, eol );
            string text = th.GetText( );

            td = new TextData( text, eol, new TextPointers( Document, eol.Length ) );

            Debug.Assert( td.TextPointers.Doc == Document );

            mCachedTextData[eol_index].SetTarget( td );

            //var t2 = Environment.TickCount;
            //Debug.WriteLine( $"####### {nameof( GetTextData )} {t2 - t1:F0}: {caller} - {Path.GetFileNameWithoutExtension( callerPath )}:{callerLine}" );

            return td;
        }

        internal SelectionInfo GetSelection( string eol ) //, [CallerMemberName] string? caller = null, [CallerFilePath] string? callerPath = null, [CallerLineNumber] int callerLine = 0 )
        {
            TextData.DbgValidateEol( eol );

            //var t1 = Environment.TickCount;

            int eol_index = EolToIndex( eol );

            SelectionInfo selection = mCachedSelection[eol_index];

            if( selection.Start >= 0 ) return selection;

            // try to re-use data for the same length of EOL

            for( int i = 0; i < mCachedSelection.Length; ++i )
            {
                if( IndexToEol( i ).Length == eol.Length && mCachedSelection[i].Start >= 0 )
                {
                    selection = mCachedSelection[i];

                    mCachedSelection[eol_index] = selection;

                    return selection;
                }
            }

            // try to adjust data of the different length
            /*
             * Too slow
             * 
            for( int i = 0; i < mCachedSelection.Length; ++i )
            {
                if( mCachedSelection[i].Start >= 0 )
                {
                    Debug.Assert( mCachedSelection[i].End >= 0 );

                    if( mCachedTextData[i].TryGetTarget( out TextData? td ) && td != null )
                    {
                        string cached_text = td.Text;
                        string cached_eol = td.Eol;
                        int cached_start = mCachedSelection[i].Start;
                        int cached_end = mCachedSelection[i].End;
                        int new_start = cached_start;
                        int new_end = cached_end;
                        int len_diff = eol.Length - cached_eol.Length;

                        for( int j = 0; ; )
                        {
                            int k = cached_text.IndexOf( cached_eol, j );
                            if( k < 0 || k >= cached_start ) break;

                            new_start += len_diff;

                            j = k + cached_eol.Length;
                        }

                        if( cached_end == cached_start )
                        {
                            new_end = new_start;
                        }
                        else
                        {
                            for( int j = 0; ; )
                            {
                                int k = cached_text.IndexOf( cached_eol, j );
                                if( k < 0 || k >= cached_end ) break;

                                new_end += len_diff;

                                j = k + cached_eol.Length;
                            }
                        }

                        selection = new SelectionInfo( new_start, new_end );

                        mCachedSelection[eol_index] = selection;

                        return selection;
                    }
                }
            }
            */

            Debug.Assert( this.Document.Parent == this );

            TextPointers tp = new( this.Document, eol.Length );

            int start = tp.GetIndex( this.Selection.Start, LogicalDirection.Backward ); // TODO: do a single scan
            int end = this.Selection.IsEmpty ? start : tp.GetIndex( this.Selection.End, LogicalDirection.Forward );

            selection = new SelectionInfo( start, end );

            mCachedSelection[eol_index] = selection;

            //var t2 = Environment.TickCount;
            //Debug.WriteLine( $"####### {nameof( GetSelection )} {t2 - t1:F0}: {caller} - {Path.GetFileNameWithoutExtension( callerPath )}:{callerLine}" );

            return selection;
        }

        protected override void OnTextChanged( TextChangedEventArgs e )
        {
            if( ChangeEventHelper.IsInChange ) return;

            foreach( var wr in mCachedTextData ) wr.SetTarget( null );

            base.OnTextChanged( e );
        }

        protected override void OnSelectionChanged( RoutedEventArgs e )
        {
            for( var i = 0; i < mCachedSelection.Length; ++i ) mCachedSelection[i] = new SelectionInfo( -1, -1 );

            base.OnSelectionChanged( e );
        }

        void AddCommandBindings( )
        {
            foreach( var c in CommandsToDisable )
            {
                CommandBindings.Add( new CommandBinding( c, executed: BlockedExecuted, canExecute: BlockedCanExecute ) );
            }
        }

        void BlockedCanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = false;
            e.Handled = true;
        }

        void BlockedExecuted( object sender, ExecutedRoutedEventArgs e )
        {
            e.Handled = true;
        }

        static int EolToIndex( string eol )
        {
            return eol switch
            {
                "\r\n" => 0,
                "\r" => 1,
                "\n" => 2,
                _ => throw new InvalidOperationException( )
            };
        }

        static string IndexToEol( int eolIndex )
        {
            return eolIndex switch
            {
                0 => "\r\n",
                1 => "\r",
                2 => "\n",
                _ => throw new InvalidOperationException( )
            };
        }
    }
}
