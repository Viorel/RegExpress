using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Windows;
using System.Windows.Controls;


namespace ZigPlugin
{
    /// <summary>
    /// Interaction logic for UCOptions.xaml
    /// </summary>
    public partial class UCOptions : UserControl
    {
        internal event EventHandler<RegexEngineOptionsChangedArgs>? Changed;

        bool IsFullyLoaded = false;
        int ChangeCounter = 0;
        Options Options = new( );

        public UCOptions( )
        {
            InitializeComponent( );

            DataContext = Options;
        }

        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( IsFullyLoaded ) return;

            IsFullyLoaded = true;

            UpdateControls( );
        }

        void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

            }
            finally
            {
                --ChangeCounter;
            }
        }

        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        internal void SetOptions( Options options )
        {
            try
            {
                ++ChangeCounter;

                if( object.ReferenceEquals( options, Options ) ) DataContext = null;
                Options = options;
                DataContext = Options;

                UpdateUI( );
            }
            finally
            {
                --ChangeCounter;
            }
        }

        void UpdateControls( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                bool is_ZigRegex = Options.Library == RegexLibraryEnum.ZigRegex;
                bool is_Mvzr = Options.Library == RegexLibraryEnum.Mvzr;
                bool is_Pzre = Options.Library == RegexLibraryEnum.Pzre;
                bool is_EziGex = Options.Library == RegexLibraryEnum.EziGex;

                pnlZigRegexOptions.Display( is_ZigRegex );
                pnlMvzrOptions.Display( is_Mvzr );
                pnlPzreOptions.Display( is_Pzre );
                pnlEziGexOptions.Display( is_EziGex );
            }
            finally
            {
                --ChangeCounter;
            }
        }

        private void cbxLibrary_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );
            Notify( preferImmediateReaction: true );
        }

        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void cb_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }

        private void tb_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }
    }
}
