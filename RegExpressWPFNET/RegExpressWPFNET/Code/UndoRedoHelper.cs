using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using RegExpressWPFNET.Controls;


namespace RegExpressWPFNET.Code
{
    internal sealed partial class UndoRedoHelper
    {
        sealed class Diff
        {
            internal readonly int Position;
            internal readonly string Remove;
            internal string Add;

            public Diff( int position, string remove, string add )
            {
                Position = position;
                Remove = remove;
                Add = add;
            }

            public bool IsEmpty => Remove.Length == 0 && Add.Length == 0;

            public override string ToString( )
            {
                return $"At {Position}, Remove '{Remove}', Add '{Add}'";
            }
        }

        sealed class UndoItem
        {
            internal readonly Diff Diff;
            internal readonly SelectionInfo SelectionBefore;
            internal SelectionInfo SelectionAfter;

            public UndoItem( Diff diff, SelectionInfo selectionBefore, SelectionInfo selectionAfter )
            {
                Diff = diff;
                SelectionBefore = selectionBefore;
                SelectionAfter = selectionAfter;
            }
        }

        readonly MyRichTextBox Rtb;
        readonly List<UndoItem> UndoList = [];
        readonly List<UndoItem> RedoList = [];
        string? PreviousText;
        SelectionInfo PreviousSelection = new( 0, 0 );
        bool IsUndoOrRedo = false;
        bool IsTrackingTextChange = false;


        public UndoRedoHelper( MyRichTextBox rtb )
        {
            Rtb = rtb;
            Rtb.CommandBindings.Add( new CommandBinding( ApplicationCommands.Undo, HandleUndo ) );
            Rtb.CommandBindings.Add( new CommandBinding( ApplicationCommands.Redo, HandleRedo ) );

            Rtb.LostFocus += HandleLostFocus;

            Init( );
        }

        public void Init( )
        {
            TextData td = Rtb.GetTextData( "\n" );

            PreviousText = td.Text;
            UndoList.Clear( );
            RedoList.Clear( );

            UndoList.Add( new UndoItem( diff: GetDiff( "", td.Text ), selectionBefore: new SelectionInfo( 0, 0 ), selectionAfter: td.Selection ) );
        }

        public void HandleTextChanged( TextChangedEventArgs e )
        {
            if( IsUndoOrRedo ) return;

            TextData td = Rtb.GetTextData( "\n" );
            SelectionInfo selection_after = td.Selection;
            Diff diff = GetDiff( PreviousText, td.Text );

            UndoItem new_undo_item = new( diff: diff, selectionBefore: PreviousSelection, selectionAfter: selection_after );

            // try combining
            bool combined = false;
            if( UndoList.Count > 1 ) // exclude the first initial one
            {
                UndoItem last_undo_item = UndoList.Last( );

                if( IsTrackingTextChange && CanBeCombined( last_undo_item, new_undo_item ) )
                {
                    last_undo_item.Diff!.Add += new_undo_item.Diff.Add;
                    last_undo_item.SelectionAfter = selection_after;
                    combined = true;
                }
            }

            if( !combined ) UndoList.Add( new_undo_item );

            PreviousText = td.Text;
            PreviousSelection = selection_after;

            RedoList.Clear( );

            IsTrackingTextChange = true;
        }

        public void HandleSelectionChanged( )
        {
            if( IsUndoOrRedo ) return;

            TextData td = Rtb.GetTextData( "\n" );

            PreviousSelection = td.Selection;
        }

        void HandleLostFocus( object sender, RoutedEventArgs e )
        {
            IsTrackingTextChange = false;
        }

        void HandleUndo( object sender, ExecutedRoutedEventArgs e )
        {
            DoUndo( );
        }

        void HandleRedo( object sender, ExecutedRoutedEventArgs e )
        {
            DoRedo( );
        }

        public bool DoUndo( )
        {
            if( UndoList.Count < 2 ) return false;

            var last = UndoList.Last( );
            UndoList.RemoveAt( UndoList.Count - 1 );

            RedoList.Add( last );

            Debug.Assert( !IsUndoOrRedo );
            IsUndoOrRedo = true;

            try
            {
                TextData td = Rtb.GetTextData( "\n" );

                using( Rtb.DeclareChangeBlock( ) )
                {
                    TextRange range = td.Range( last.Diff.Position, last.Diff.Add.Length );
                    range.Text = EolRegex( ).Replace( last.Diff.Remove, "\r" ); // (it does not like '\n')
                    range.ClearAllProperties( );
                }

                td = Rtb.GetTextData( "\n" );
                RtbUtilities.SafeSelect( Rtb, td, last.SelectionBefore.Start, last.SelectionBefore.End );

                PreviousText = td.Text;
                PreviousSelection = td.Selection;

                IsTrackingTextChange = false;

                return true;
            }
            finally
            {
                Debug.Assert( IsUndoOrRedo );
                IsUndoOrRedo = false;
            }
        }

        public bool DoRedo( )
        {
            if( !RedoList.Any( ) ) return false;

            var last = RedoList.Last( );
            RedoList.RemoveAt( RedoList.Count - 1 );

            UndoList.Add( last );

            Debug.Assert( !IsUndoOrRedo );
            IsUndoOrRedo = true;

            try
            {
                TextData td = Rtb.GetTextData( "\n" );

                using( Rtb.DeclareChangeBlock( ) )
                {
                    TextRange range = td.Range( last.Diff.Position, last.Diff.Remove.Length );
                    range.Text = EolRegex( ).Replace( last.Diff.Add, "\r" ); // (it does not like '\n')
                    range.ClearAllProperties( );
                    Rtb.Selection.Select( range.End, range.End );
                }

                td = Rtb.GetTextData( "\n" );

                PreviousText = td.Text;
                PreviousSelection = td.Selection;

                IsTrackingTextChange = false;

                return true;
            }
            finally
            {
                Debug.Assert( IsUndoOrRedo );
                IsUndoOrRedo = false;
            }
        }

        static Diff GetDiff( string? first, string? second )
        {
            first ??= string.Empty;
            second ??= string.Empty;

            int i = 0;
            while( i < first.Length && i < second.Length && first[i] == second[i] ) ++i;

            int j_first = first.Length - 1;
            int j_second = second.Length - 1;
            while( j_first >= i && j_second >= i && first[j_first] == second[j_second] ) { --j_first; --j_second; }

            // fix surrogate pairs;
            // order in string: High Surrogate, Low Surrogate

            if( i > 0 && i < first.Length && char.IsLowSurrogate( first, i ) && i < second.Length && char.IsLowSurrogate( second, i ) )
            {
                --i;
                Debug.Assert( i >= 0 );
                Debug.Assert( char.IsHighSurrogate( first, i ) );
                Debug.Assert( char.IsHighSurrogate( second, i ) );
            }

            if( j_first >= 0 && j_first < first.Length && char.IsHighSurrogate( first, j_first ) )
            {
                ++j_first;
                Debug.Assert( char.IsLowSurrogate( first, j_first ) );
            }
            if( j_second >= 0 && j_second < second.Length && char.IsHighSurrogate( second, j_second ) )
            {
                ++j_second;
                Debug.Assert( char.IsLowSurrogate( second, j_second ) );
            }

            return new Diff( position: i, remove: first.Substring( i, j_first - i + 1 ), add: second.Substring( i, j_second - i + 1 ) );
        }

        static bool CanBeCombined( UndoItem ui1, UndoItem ui2 )
        {
            return
                string.IsNullOrEmpty( ui2.Diff.Remove ) &&
                ui1.SelectionAfter.Length == 0 &&
                ui2.SelectionBefore.Length == 0 &&
                ui1.SelectionAfter.Start == ui2.SelectionBefore.Start &&
                !EolRegex( ).IsMatch( ui1.Diff.Add ) &&
                !EolRegex( ).IsMatch( ui2.Diff.Add )
                ;
        }

        /*
        static string Undo( string s, Diff d )
        {
            return s.Remove( d.Position, d.Add.Length ).Insert( d.Position, d.Remove );
        }


        static string Redo( string s, Diff d )
        {
            return s.Remove( d.Position, d.Remove.Length ).Insert( d.Position, d.Add );
        }
        */


        [GeneratedRegex( @"\r\n|\n" )]
        private static partial Regex EolRegex( );
    }
}
