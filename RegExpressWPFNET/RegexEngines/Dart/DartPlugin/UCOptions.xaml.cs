using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Windows;
using System.Windows.Controls;


namespace DartPlugin
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

            UpdateUI( );
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

        private void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                bool is_regexp = Options.package == PackageEnum.RegExp;
                bool is_oniguruma = Options.package == PackageEnum.OnigurumaDart;

                pnlRegExp.Display( is_regexp );
                pnlOnigurumaDart.Display( is_oniguruma );
                lblOnigurumaSyntax.Display( is_oniguruma );
                cbxOnigurumaSyntax.Display( is_oniguruma );
            }
            finally
            {
                --ChangeCounter;
            }
        }

        private void cbxPackage_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

            Notify( preferImmediateReaction: true );
        }

        private void cbxOnigurumaSyntax_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

            Notify( preferImmediateReaction: true );
        }
    }
}
