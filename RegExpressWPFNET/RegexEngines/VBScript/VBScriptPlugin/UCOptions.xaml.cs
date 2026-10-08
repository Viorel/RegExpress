using RegExpressLibrary;
using RegExpressLibrary.UI;
using System;
using System.Windows;
using System.Windows.Controls;


namespace VBScriptPlugin
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

                bool is_vbscript = Options.Implementation == ImplementationEnum.VBScript;
                bool is_twinBasic = Options.Implementation == ImplementationEnum.TwinBasic;

                cbxDotAll.Display( is_twinBasic );
                cbxDotAllDISABLED.Display( !is_twinBasic );

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
            Notify(preferImmediateReaction: false );
        }

        private void cbxImplementation_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            UpdateUI( );
            Notify( preferImmediateReaction: true );
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

    }
}
