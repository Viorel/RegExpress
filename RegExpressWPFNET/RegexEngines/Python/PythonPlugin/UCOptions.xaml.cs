using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Windows;
using System.Windows.Controls;


namespace PythonPlugin
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


        void UpdateUI( )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            try
            {
                ++ChangeCounter;

                bool is_re = Options.Module == ModuleEnum.re;
                bool is_regex = Options.Module == ModuleEnum.regex;
                bool is_real = Options.Module == ModuleEnum.real_regex;

                LOCALE.Display( is_re || is_regex );
                pnlAdditional.Display( is_regex );
                FALLBACK.Display( is_real );
                pnlLimits.Display( is_real );
            }
            finally
            {
                --ChangeCounter;
            }
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

        private void TextBox_Changed( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void cbxModule_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
        }

        internal void SetOptions( Options options )
        {
            if( object.ReferenceEquals( options, Options ) ) DataContext = null;
            Options = options;
            DataContext = Options;

            UpdateUI( );
        }

        internal string? GetSelectedModuleTitle( )
        {
            return Options.Module switch
            {
                ModuleEnum.re => "re",
                ModuleEnum.regex => "regex",
                ModuleEnum.real_regex => "real",
                _ => "unknown"
            };
        }
    }
}
