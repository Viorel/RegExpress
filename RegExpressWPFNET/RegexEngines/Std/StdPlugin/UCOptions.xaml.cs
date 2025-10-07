using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using RegExpressLibrary;


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

            MatcherMSVC.StartGetVersion( SetMSVCVersion );
            MatcherGCC.StartGetVersion( SetGCCVersion );
            MatcherSRELL.StartGetVersion( SetSRELLVersion );

            cbiSystemLocale.Content = $"System ({CultureInfo.CurrentCulture.Name})"; // TODO: watch for system changes

            UpdateControls( );
        }


        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxCompiler_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );
            Notify( preferImmediateReaction: true );
        }

        private void cbxGrammar_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );
            Notify( preferImmediateReaction: true );
        }

        private void cbxLocale_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );
            Notify( preferImmediateReaction: true );
        }


        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }


        private void tbREGEX_MAX_STACK_COUNT_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }


        private void tbREGEX_MAX_COMPLEXITY_COUNT_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        void UpdateControls( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                bool is_MSVC = Options.Compiler == CompilerEnum.MSVC;
                bool is_GCC = Options.Compiler == CompilerEnum.GCC;
                bool is_SRELL = Options.Compiler == CompilerEnum.SRELL;

                cbxLocale.Visibility = is_MSVC || is_GCC ? Visibility.Visible : Visibility.Collapsed;
                cbxLocaleDisabled.Visibility = !( is_MSVC || is_GCC ) ? Visibility.Visible : Visibility.Collapsed;

                chkMultilineMSVC.Visibility = pnlMSVCConstants.Visibility = is_MSVC ? Visibility.Visible : Visibility.Collapsed;
                chkMultiline.Visibility = is_GCC || is_SRELL ? Visibility.Visible : Visibility.Collapsed;
                chkPolynomial.Visibility = is_GCC ? Visibility.Visible : Visibility.Collapsed;
                chkDotall.Visibility = chkUnicodesets.Visibility = chkVMode.Visibility = is_SRELL ? Visibility.Visible : Visibility.Collapsed;
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

                UpdateControls( );
            }
            finally
            {
                --ChangeCounter;
            }
        }

        void SetMSVCVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxCompiler.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "MSVC" );

                cbi.Content = $"MSVC {version}";
            } );
        }

        void SetGCCVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxCompiler.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "GCC" );

                cbi.Content = $"GCC {version}";
            } );
        }

        void SetSRELLVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxCompiler.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "SRELL" );

                cbi.Content = $"SRELL {version}";
            } );
        }
    }
}
