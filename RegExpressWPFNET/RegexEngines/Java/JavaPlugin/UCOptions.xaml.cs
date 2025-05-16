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


namespace JavaPlugin
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


        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxPackage_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );

            Notify( preferImmediateReaction: true );
        }

        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void TextBox_Changed( object sender, TextChangedEventArgs e )
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

                PackageEnum package = ( (ComboBoxItem)cbxPackage.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "regex" => PackageEnum.regex,
                    "re2j" => PackageEnum.re2j,
                    _ => PackageEnum.None,
                };

                bool is_regex = package == PackageEnum.regex;
                bool is_re2j = package == PackageEnum.re2j;
                Visibility regex_visibility = is_regex ? Visibility.Visible : Visibility.Collapsed;
                Visibility re2j_visibility = is_re2j ? Visibility.Visible : Visibility.Collapsed;

                CANON_EQ.Visibility = regex_visibility;
                COMMENTS.Visibility = regex_visibility;
                LITERAL.Visibility = regex_visibility;
                UNICODE_CASE.Visibility = regex_visibility;
                UNICODE_CHARACTER_CLASS.Visibility = regex_visibility;
                UNIX_LINES.Visibility = regex_visibility;
                DISABLE_UNICODE_GROUPS.Visibility = re2j_visibility;
                LONGEST_MATCH.Visibility = re2j_visibility;

                panelRegion.Visibility = regex_visibility;
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

                UpdateControls( );
            }
            finally
            {
                --ChangeCounter;
            }
        }

        internal string GetSelectedPackageTitle( )
        {
            Options options = GetSelectedOptions( );

            return options.Package switch
            {
                PackageEnum.regex => "regex",
                PackageEnum.re2j => "re2j",
                _ => "Unknown"
            };
        }

    }
}
