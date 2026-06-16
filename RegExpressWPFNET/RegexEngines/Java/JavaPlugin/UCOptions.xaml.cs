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

            UpdateUI( );
        }


        void Notify( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = preferImmediateReaction } );
        }

        private void cbxPackage_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

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

        void UpdateUI( )
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

                CANON_EQ.Display( is_regex );
                COMMENTS.Display( is_regex );
                LITERAL.Display( is_regex );
                UNICODE_CASE.Display( is_regex );
                UNICODE_CHARACTER_CLASS.Display( is_regex );
                UNIX_LINES.Display( is_regex );
                DISABLE_UNICODE_GROUPS.Display( is_re2j );
                LONGEST_MATCH.Display( is_re2j );

                panelRegion.Display( is_regex );
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

        internal string GetSelectedPackageTitle( )
        {
            return Options.Package switch
            {
                PackageEnum.regex => "regex",
                PackageEnum.re2j => "re2j",
                _ => "Unknown"
            };
        }

    }
}
