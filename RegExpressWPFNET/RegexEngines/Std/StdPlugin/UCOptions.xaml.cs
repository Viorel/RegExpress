using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;


namespace StdPlugin
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

            cbiSystemLocale.Content = $"System ({CultureInfo.CurrentCulture.Name})"; // TODO: watch for system changes

            UpdateUI( );
        }


        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxCompiler_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
        }

        private void cbxGrammar_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
        }

        private void cbxLocale_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
        }


        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void TextBox_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                bool is_MSVC = Options.Compiler == CompilerEnum.MSVC;
                bool is_GCC = Options.Compiler == CompilerEnum.GCC;
                bool is_SRELL = Options.Compiler == CompilerEnum.SRELL;
                bool is_SRELL_LINEAR = Options.Compiler == CompilerEnum.SRELL_LINEAR;

                cbxLocale.Display( is_MSVC || is_GCC );
                cbxLocaleDisabled.Display( !( is_MSVC || is_GCC ) );

                chkMultiline.Display( is_MSVC || is_GCC || is_SRELL || is_SRELL_LINEAR );
                chkPolynomial.Display( is_GCC );
                new FrameworkElement[] { chkDotall, chkUnicodesets, chkVMode, pnlSRELLConstants }.Display( is_SRELL || is_SRELL_LINEAR );
                lbl_limit_counter.IsEnabled = tb_limit_counter.IsEnabled = is_SRELL;
                tb_limit_counter.Display( is_SRELL );
                tb_limit_counter_DISABLED.Display( is_SRELL_LINEAR );
            }
            finally
            {
                --ChangeCounter;
            }
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
    }
}
