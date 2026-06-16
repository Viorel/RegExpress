using System;
using System.Collections.Generic;
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
using RegExpressLibrary.UI;


namespace JavaScriptPlugin
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

            MatcherWebView2.StartGetVersion( SetWebView2Version );

            UpdateUI( );
        }

        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxRuntime_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

            Notify( preferImmediateReaction: true );
        }

        private void cbxFunction_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }

        private void CheckBox_Changed( object sender, RoutedEventArgs e )
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

                bool is_V8 = Options.Runtime == RuntimeEnum.WebView2 || Options.Runtime == RuntimeEnum.NodeJs;
                bool is_SM = Options.Runtime == RuntimeEnum.SpiderMonkey;
                bool is_QuickJs = Options.Runtime == RuntimeEnum.QuickJs;
                bool is_RE2JS = Options.Runtime == RuntimeEnum.RE2JS;
                bool is_RegexPlus = Options.Runtime == RuntimeEnum.RegexPlus;

                cbxFunction.Display( !is_RE2JS );
                cbxFunctionRE2JS.Display( is_RE2JS );

                pnlCommon.Display( !is_RE2JS );
                pnlRE2JS.Display( is_RE2JS );
                pnlSM.Display( is_SM );

                checkboxU.Display( !is_RegexPlus );
                checkboxV.Display( is_V8 || is_QuickJs || is_RegexPlus );
                checkboxX.Display( is_RegexPlus );
                checkboxN.Display( is_RegexPlus );
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

        void SetWebView2Version( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxRuntime.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "WebView2" );

                cbi.Content = $"WebView2 {version}";
            } );
        }
    }
}
