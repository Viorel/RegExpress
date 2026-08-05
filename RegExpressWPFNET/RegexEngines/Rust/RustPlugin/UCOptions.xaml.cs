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

        private void cbxBytesMode_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
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

        public void UpdateUI( )
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
                    "real_regex" => CrateEnum.real_regex,
                    "java_regex" => CrateEnum.java_regex,
                    "eregex" => CrateEnum.eregex,
                    _ => CrateEnum.None,
                };

                StructEnum @struct = ( (ComboBoxItem)cbxStruct.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "Regex" => StructEnum.Regex,
                    "RegexBuilder" => StructEnum.RegexBuilder,
                    _ => StructEnum.None,
                };

                bool is_regex = crate == CrateEnum.regex;
                bool is_regex_lite = crate == CrateEnum.regex_lite;
                bool is_regex_or_regex_lite = is_regex || is_regex_lite;
                bool is_fancy = crate == CrateEnum.fancy_regex;
                bool is_regress = crate == CrateEnum.regress;
                bool is_resharp = crate == CrateEnum.resharp;
                bool is_anre = crate == CrateEnum.anre;
                bool is_real = crate == CrateEnum.real_regex;
                bool is_java_regex = crate == CrateEnum.java_regex;
                bool is_eregex = crate == CrateEnum.eregex;

                pnlStruct.Display( is_regex_or_regex_lite || is_fancy || is_real );
                pnlRegexBuilderOptions.Display( is_regex_or_regex_lite || is_fancy || is_real );
                pnlRegressOptions.Display( is_regress );
                pnlResharpOptions.Display( is_resharp );
                pnlAnreOptions.Display( is_anre );

                if( pnlStruct.IsVisible )
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

                pnlRegexCrateLimits.Display( is_regex_or_regex_lite );
                dsl.IsEnabled = is_regex;

                pnlFancyRegexCrateLimits.Display( is_fancy );

                chbx_crlf.Display( is_regex_or_regex_lite || is_fancy );
                chbx_swap_greed.Display( is_regex_or_regex_lite );
                chbx_unicode.Display( is_regex || is_real );
                chbx_unicode_mode.Display( is_fancy );
                chbx_octal.Display( is_regex );
                chbx_oniguruma_mode.Display( is_fancy );
                chbx_find_not_empty.Display( is_fancy );
                chbx_ignore_numbered_groups_when_named_groups_exist.Display( is_fancy );
                chbx_seek.Display( is_fancy );
                chbx_disallow_empty_match_at_eof_after_newline.Display( is_fancy );
                chbx_allow_input_assertion_overrides.Display( is_fancy );
                chbx_start_text.Display( is_fancy );
                chbx_end_text.Display( is_fancy );
                chbx_fallback.Display( is_real );

                pnlJavaRegexOptions.Display( is_java_regex );
                pnlERegexOptions.Display( is_eregex );
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
                CrateEnum.regex_lite => "regex-lite",
                CrateEnum.fancy_regex => "fancy",
                CrateEnum.regress => "regress",
                CrateEnum.resharp => "resharp",
                CrateEnum.anre => "anre",
                CrateEnum.real_regex => "real",
                CrateEnum.java_regex => "java_regex",
                CrateEnum.eregex => "eregex",
                _ => "unknown"
            };
        }

    }
}
