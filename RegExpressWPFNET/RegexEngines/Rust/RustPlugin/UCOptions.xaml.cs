﻿using System;
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

            UpdateControls( );
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
            UpdateControls( );

            Notify( preferImmediateReaction: true );
        }

        private void cbxStruct_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateControls( );

            Notify( preferImmediateReaction: true );
        }


        private void tb_TextChanged( object sender, TextChangedEventArgs e )
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

                CrateEnum crate = ( (ComboBoxItem)cbxCrate.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "regex" => CrateEnum.regex,
                    "fancy_regex" => CrateEnum.fancy_regex,
                    "regress" => CrateEnum.regress,
                    _ => CrateEnum.None,
                };

                StructEnum @struct = ( (ComboBoxItem)cbxStruct.SelectedItem )?.Tag?.ToString( ) switch
                {
                    "Regex" => StructEnum.Regex,
                    "RegexBuilder" => StructEnum.RegexBuilder,
                    _ => StructEnum.None,
                };

                pnlStruct.Visibility =
                    pnlRegexBuilderOptions.Visibility = crate == CrateEnum.regex || crate == CrateEnum.fancy_regex ? Visibility.Visible : Visibility.Collapsed;
                pnlRegressOptions.Visibility = crate == CrateEnum.regress ? Visibility.Visible : Visibility.Collapsed;

                if( crate == CrateEnum.regex || crate == CrateEnum.fancy_regex )
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

                    chbx_case_insensitive.Visibility = Visibility.Visible;
                    chbx_multi_line.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    chbx_dot_matches_new_line.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    chbx_swap_greed.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    chbx_ignore_whitespace.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    chbx_unicode.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    chbx_octal.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;

                    pnlRegexCrateLimits.Visibility = crate == CrateEnum.regex ? Visibility.Visible : Visibility.Collapsed;
                    pnlFancyRegexCrateLimits.Visibility = crate == CrateEnum.fancy_regex ? Visibility.Visible : Visibility.Collapsed;
                }
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

    }
}
