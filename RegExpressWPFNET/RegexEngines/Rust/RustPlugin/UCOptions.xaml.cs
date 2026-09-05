using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;


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

        private void chbx_use_builder_Changed( object sender, RoutedEventArgs e )
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

        internal void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                CrateEnum crate = Options.crate;
                bool use_builder = Options.UseBuilder;

                bool is_regex = crate == CrateEnum.regex;
                bool is_regex_lite = crate == CrateEnum.regex_lite;
                bool is_regex_or_regex_lite = is_regex || is_regex_lite;
                bool is_fancy = crate == CrateEnum.fancy_regex;
                bool is_regress = crate == CrateEnum.regress;
                bool is_resharp = crate == CrateEnum.resharp;
                bool is_anre = crate == CrateEnum.anre;
                bool is_real = crate == CrateEnum.real_regex;
                bool is_java_regex = crate == CrateEnum.java_regex;
                bool is_regexr = crate == CrateEnum.regexr;
                bool is_rexile = crate == CrateEnum.rexile;

                chbx_use_builder.Display( is_regex_or_regex_lite || is_fancy || is_real || is_regexr );
                pnlRegexBuilderOptions.Display( is_regex_or_regex_lite || is_fancy || is_real );
                pnlRegressOptions.Display( is_regress );
                pnlResharpOptions.Display( is_resharp );
                pnlAnreOptions.Display( is_anre );
                pnlJavaRegexOptions.Display( is_java_regex );
                pnlFancyRegexCrateLimits.Display( is_fancy );
                pnlRegexrOptions.Display( is_regexr );

                pnlRegexCrateLimits.Display( is_regex_or_regex_lite );
                dsl.IsEnabled = is_regex;

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

                if( chbx_use_builder.IsDisplayed( ) )
                {
                    var pnls = new[] { pnlRegexBuilderOptions, pnlRegexrOptions };

                    foreach( var pnl in pnls.Where( p => p.IsDisplayed( ) ) )
                    {
                        pnl.IsEnabled = use_builder;
                        pnl.Opacity = pnlRegexBuilderOptions.IsEnabled ? 1 : 0.75;

                        if( use_builder )
                        {
                            pnl.ClearValue( DataContextProperty ); // (to use inherited context)
                        }
                        else
                        {
                            pnl.DataContext = new Options( ); // (to show defaults)
                        }
                    }
                }
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
                CrateEnum.regexr => "regexr",
                CrateEnum.rexile => "rexile",
                _ => "unknown"
            };
        }

    }
}
