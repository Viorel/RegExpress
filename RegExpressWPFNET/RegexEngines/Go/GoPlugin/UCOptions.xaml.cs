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


namespace GoPlugin
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

                bool is_regexp = Options.Package == PackageEnum.regexp;
                bool is_regexp2 = Options.Package == PackageEnum.regexp2;
                bool is_rexa = Options.Package == PackageEnum.rexa;
                bool is_coregex = Options.Package == PackageEnum.coregex;

                pnlRegexp2Flags.Visibility = is_regexp2 ? Visibility.Visible : Visibility.Collapsed;
                pnlRexaFlags.Visibility = is_rexa ? Visibility.Visible : Visibility.Collapsed;

                cbxPosix.Visibility = is_regexp || is_coregex ? Visibility.Visible : Visibility.Collapsed;
                cbxLongest.Visibility = is_regexp || is_rexa || is_coregex ? Visibility.Visible : Visibility.Collapsed;
                cbxLiteral.Visibility = is_regexp || is_rexa || is_coregex ? Visibility.Visible : Visibility.Collapsed;
            }
            finally
            {
                --ChangeCounter;
            }
        }

        private void cbxPackage_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );

            UpdateUI( );
        }

    }
}
