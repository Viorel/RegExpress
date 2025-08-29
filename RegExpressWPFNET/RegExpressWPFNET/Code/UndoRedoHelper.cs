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
        class Diff
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

            public override string ToString( )
            {
                return $"At {Position}, Remove '{Remove}', Add '{Add}'";
            }
        }

        class UndoItem
        {
            internal readonly Diff Diff;
            internal readonly SelectionInfo SelectionInfoA;
            internal SelectionInfo SelectionInfoB;

            public UndoItem( Diff diff, SelectionInfo selectionInfoA, SelectionInfo selectionInfoB )
            {
                Diff = diff;
                SelectionInfoA = selectionInfoA;
                SelectionInfoB = selectionInfoB;
            }
        }

        readonly MyRichTextBox Rtb;
        readonly List<UndoItem> UndoList = new( );
        readonly List<UndoItem> RedoList = new( );
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
            var td = Rtb.GetTextData( "\n" );

            PreviousText = td.Text;
            UndoList.Clear( );
            RedoList.Clear( );

            UndoList.Add( new UndoItem( diff: GetDiff( "", td.Text ), selectionInfoA: new SelectionInfo( 0, 0 ), selectionInfoB: td.Selection ) );
        }


        public void HandleTextChanged( TextChangedEventArgs e )
        {
            if( IsUndoOrRedo ) return;

            var td = Rtb.GetTextData( "\n" );
            var si = td.Selection;
            var ui = new UndoItem( diff: GetDiff( PreviousText, td.Text ), selectionInfoA: PreviousSelection, selectionInfoB: si );

            // try combining
            bool combined = false;
            if( UndoList.Count > 1 ) // (exclude the first initial one)
            {
                var last = UndoList.Last( );
                if( IsTrackingTextChange && CanBeCombined( last, ui ) )
                {
                    last.Diff!.Add += ui.Diff.Add;
                    last.SelectionInfoB = td.Selection;
                    combined = true;
                }
            }

            if( !combined ) UndoList.Add( ui );

            PreviousText = td.Text;
            PreviousSelection = si;

            RedoList.Clear( );

            IsTrackingTextChange = true;
        }


        public void HandleSelectionChanged( )
        {
            if( IsUndoOrRedo ) return;

            var td = Rtb.GetTextData( "\n" );

            PreviousSelection = td.Selection;
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
                var td = Rtb.GetTextData( "\n" );

                using( Rtb.DeclareChangeBlock( ) )
                {
                    var range = td.Range( last.Diff.Position, last.Diff.Add.Length );
                    range.Text = EolRegex( ).Replace( last.Diff.Remove, "\r" ); // (it does not like '\n')
                    range.ClearAllProperties( );
                }

                td = Rtb.GetTextData( "\n" );
                RtbUtilities.SafeSelect( Rtb, td, last.SelectionInfoA.Start, last.SelectionInfoA.End );

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
                var td = Rtb.GetTextData( "\n" );

                using( Rtb.DeclareChangeBlock( ) )
                {
                    var range = td.Range( last.Diff.Position, last.Diff.Remove.Length );
                    range.Text = EolRegex( ).Replace( last.Diff.Add, "\r" ); // (it does not like '\n')
                    range.ClearAllProperties( );
                }

                td = Rtb.GetTextData( "\n" );
                RtbUtilities.SafeSelect( Rtb, td, last.SelectionInfoB.Start, last.SelectionInfoB.End );

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


        static Diff GetDiff( string? first, string? second )
        {
            first ??= string.Empty;
            second ??= string.Empty;

            int i = 0;
            while( i < first.Length && i < second.Length && first[i] == second[i] ) ++i;

            int j1 = first.Length - 1;
            int j2 = second.Length - 1;
            while( j1 >= i && j2 >= i && first[j1] == second[j2] ) { --j1; --j2; }

            return new Diff( position: i, remove: first.Substring( i, j1 - i + 1 ), add: second.Substring( i, j2 - i + 1 ) );
        }


        static bool CanBeCombined( UndoItem ui1, UndoItem ui2 )
        {
            return
                string.IsNullOrEmpty( ui2.Diff.Remove ) &&
                ui1.SelectionInfoB.Length == 0 &&
                ui2.SelectionInfoA.Length == 0 &&
                ui1.SelectionInfoB.Start == ui2.SelectionInfoA.Start;
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
