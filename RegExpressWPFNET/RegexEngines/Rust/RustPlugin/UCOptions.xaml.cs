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


namespace RustPlugin
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

        private void cbxCrate_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

            Notify( preferImmediateReaction: true );
        }

        private void cbxStruct_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );

            Notify( preferImmediateReaction: true );
        }

        private void tb_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void cbxUnicodeMode_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }

        void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                CrateEnum crate = ( (ComboBoxItem)cbxCrate.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "regex" => CrateEnum.regex,
                    "regex_lite" => CrateEnum.regex_lite,
                    "fancy_regex" => CrateEnum.fancy_regex,
                    "regress" => CrateEnum.regress,
                    "resharp" => CrateEnum.resharp,
                    "anre" => CrateEnum.anre,
                    _ => CrateEnum.None,
                };

                StructEnum @struct = ( (ComboBoxItem)cbxStruct.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "Regex" => StructEnum.Regex,
                    "RegexBuilder" => StructEnum.RegexBuilder,
                    _ => StructEnum.None,
                };

                bool is_regex_or_regex_lite = crate == CrateEnum.regex || crate == CrateEnum.regex_lite;
                bool is_fancy = crate == CrateEnum.fancy_regex;
                bool is_resharp = crate == CrateEnum.resharp;
                bool is_anre = crate == CrateEnum.anre;

                pnlStruct.Visibility =
                    pnlRegexBuilderOptions.Visibility = is_regex_or_regex_lite || is_fancy ? Visibility.Visible : Visibility.Collapsed;
                pnlRegressOptions.Visibility = crate == CrateEnum.regress ? Visibility.Visible : Visibility.Collapsed;
                pnlResharpOptions.Visibility = is_resharp ? Visibility.Visible : Visibility.Collapsed;
                pnlAnreOptions.Visibility = is_anre ? Visibility.Visible : Visibility.Collapsed;

                if( is_regex_or_regex_lite || is_fancy )
                {
                    bool is_builder = @struct == StructEnum.RegexBuilder;

                    pnlRegexBuilderOptions.IsEnabled = is_builder;
                    pnlRegexBuilderOptions.Opacity = pnlRegexBuilderOptions.IsEnabled ? 1 : 0.75;

                    if( is_builder )
                    {
                        pnlRegexBuilderOptions.ClearValue( DataContextProperty ); // (to use inherited context)
                    }
                    else
                    {
                        pnlRegexBuilderOptions.DataContext = new Options( ); // (to show defaults)
                    }
                }

                pnlRegexCrateLimits.Visibility = is_regex_or_regex_lite ? Visibility.Visible : Visibility.Collapsed;
                dsl.IsEnabled = crate == CrateEnum.regex;

                pnlFancyRegexCrateLimits.Visibility = is_fancy ? Visibility.Visible : Visibility.Collapsed;

                chbx_crlf.Visibility = is_regex_or_regex_lite || is_fancy ? Visibility.Visible : Visibility.Collapsed;
                chbx_swap_greed.Visibility = is_regex_or_regex_lite ? Visibility.Visible : Visibility.Collapsed;
                chbx_unicode.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                chbx_unicode_mode.Visibility = is_fancy ? Visibility.Visible : Visibility.Collapsed;
                chbx_octal.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                chbx_oniguruma_mode.Visibility = is_fancy ? Visibility.Visible : Visibility.Collapsed;
                chbx_find_not_empty.Visibility = is_fancy ? Visibility.Visible : Visibility.Collapsed;
                chbx_ignore_numbered_groups_when_named_groups_exist.Visibility = is_fancy ? Visibility.Visible : Visibility.Collapsed;
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

        internal string? GetSelectedCrateTitle( )
        {
            return Options.crate switch
            {
                CrateEnum.regex => "regex",
                CrateEnum.regex_lite => "regex_lite",
                CrateEnum.fancy_regex => "fancy_regex",
                CrateEnum.regress => "regress",
                CrateEnum.resharp => "resharp",
                CrateEnum.anre => "anre",
                _ => "unknown"
            };
        }

    }
}
