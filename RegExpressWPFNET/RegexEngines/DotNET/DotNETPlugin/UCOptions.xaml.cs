using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Windows;
using System.Windows.Controls;


namespace DotNETPlugin
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

                var ttt = Options.Class.ToString( );

                ClassEnum @class = Options.Class;

                bool is_dotnet = @class == ClassEnum.RegexDotNet;
                bool is_dotnet_framework = @class == ClassEnum.RegexDotNetFramework;
                bool is_resharp = @class == ClassEnum.ReSharp;
                bool is_scout = @class == ClassEnum.Scout;
                bool is_locad = @class == ClassEnum.LokadUtf8Regex;
                bool is_gregsEx = @class == ClassEnum.GregsEx;
                bool is_corvus = @class == ClassEnum.CorvusEcma262;

                pnlRegex.Display( is_dotnet | is_dotnet_framework | is_locad );
                pnlReSharp.Display( is_resharp );
                pnlScout.Display( is_scout );
                pnlGregsEx.Display( is_gregsEx );
                pnlCorvusEcma262.Display( is_corvus );

                NonBacktracking.Display( is_dotnet | is_locad );
                NonBacktrackingDisabled.Display( !( is_dotnet | is_locad ) );
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

        private void cbxClass_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
        }

        private void CheckBox_Changed( object sender, RoutedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void tb_TextChanged( object sender, TextChangedEventArgs e )
        {
            Notify( preferImmediateReaction: false );
        }

        private void cbxByteRegexEngineMode_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            Notify( preferImmediateReaction: true );
        }

        internal void SetOptions( Options options )
        {
            if( object.ReferenceEquals( options, Options ) ) DataContext = null;
            Options = options;
            DataContext = options;

            UpdateUI( );
        }
    }
}
