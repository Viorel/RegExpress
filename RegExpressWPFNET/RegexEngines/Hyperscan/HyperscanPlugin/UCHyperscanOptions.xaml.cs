using RegExpressLibrary;
using System;
using System.Windows;
using System.Windows.Controls;


namespace HyperscanPlugin
{
    /// <summary>
    /// Interaction logic for UCHyperscanOptions.xaml
    /// </summary>
    public partial class UCHyperscanOptions : UserControl
    {
        internal event EventHandler<RegexEngineOptionsChangedArgs>? Changed;

        bool IsFullyLoaded = false;
        int ChangeCounter = 0;
        HyperscanOptions Options = new( );

        public UCHyperscanOptions( )
        {
            InitializeComponent( );

            DataContext = Options;
        }

        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( IsFullyLoaded ) return;

            IsFullyLoaded = true;
        }


        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }


        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }


        private void TextBox_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }


        private void cbxMode_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }


        private void cbxModeSom_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }

        internal void SetOptions( HyperscanOptions options )
        {
            try
            {
                ++ChangeCounter;

                if( object.ReferenceEquals( options, Options ) ) DataContext = null;
                Options = options;
                DataContext = Options;
            }
            finally
            {
                --ChangeCounter;
            }
        }
    }
}
