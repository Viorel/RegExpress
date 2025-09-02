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
            MatcherNodeJs.StartGetVersion( SetNodeJsVersion );
            MatcherQuickJs.StartGetVersion( SetQuickJsVersion );
            MatcherSpiderMonkey.StartGetVersion( SetSpiderMonkeyVersion );
            MatcherBun.StartGetVersion( SetBunVersion );

            UpdateControls( );
        }

        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxRuntime_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );

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

        void UpdateControls( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                bool is_V8 = Options.Runtime == RuntimeEnum.WebView2 || Options.Runtime == RuntimeEnum.NodeJs;
                bool is_SM = Options.Runtime == RuntimeEnum.SpiderMonkey;

                checkboxV.Visibility = is_V8 ? Visibility.Visible : Visibility.Collapsed;
                checkboxNoNativeRegexp.Visibility = checkboxEnableDuplicateNames.Visibility = checkboxEnableRegexpModifiers.Visibility =
                    is_SM ? Visibility.Visible : Visibility.Collapsed;

                ++ChangeCounter;
            }
            finally
            {
                --ChangeCounter;
            }
        }

        internal Options GetSelectedOptions( )
        {
            return Dispatcher.CheckAccess( ) ? Options : Options.Clone( );
        }

        internal void SetSelectedOptions( Options options )
        {
            try
            {
                ++ChangeCounter;

                Options = options.Clone( );
                DataContext = Options;
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

        void SetNodeJsVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxRuntime.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "NodeJs" );

                cbi.Content = $"V8 (Node.js) {version}";
            } );
        }

        void SetQuickJsVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxRuntime.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "QuickJs" );

                cbi.Content = $"QuickJs {version}";
            } );
        }

        void SetSpiderMonkeyVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxRuntime.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "SpiderMonkey" );

                cbi.Content = $"SpiderMonkey {version}";
            } );

        }

        void SetBunVersion( string? version )
        {
            if( string.IsNullOrWhiteSpace( version ) ) return;

            Dispatcher.BeginInvoke( ( ) =>
            {
                ComboBoxItem cbi = cbxRuntime.Items.OfType<ComboBoxItem>( ).Single( i => (string)i.Tag == "Bun" );

                cbi.Content = $"JavaScriptCore (Bun) {version}";
            } );
        }
    }
}
