using RegExpressLibrary;
using System;
using System.Windows;
using System.Windows.Controls;


namespace HtmlAgilityPackPlugin
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
        }

        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( IsFullyLoaded ) return;

            IsFullyLoaded = true;
        }

        private void SelectorMode_Changed( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            if( rbXPath.IsChecked == true )
                Options.SelectorMode = SelectorMode.XPath;
            else if( rbCssSelector.IsChecked == true )
                Options.SelectorMode = SelectorMode.CssSelector;

            Changed?.Invoke( this, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = false } );
        }

        private void OutputMode_Changed( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( ChangeCounter != 0 ) return;

            if( rbOuterHtml.IsChecked == true )
                Options.OutputMode = OutputMode.OuterHtml;
            else if( rbInnerHtml.IsChecked == true )
                Options.OutputMode = OutputMode.InnerHtml;
            else if( rbInnerText.IsChecked == true )
                Options.OutputMode = OutputMode.InnerText;

            Changed?.Invoke( this, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = false } );
        }

        internal void SetOptions( Options options )
        {
            try
            {
                ++ChangeCounter;

                Options = options;

                // Update selector mode radio buttons
                rbXPath.IsChecked = options.SelectorMode == SelectorMode.XPath;
                rbCssSelector.IsChecked = options.SelectorMode == SelectorMode.CssSelector;

                // Update output mode radio buttons
                rbOuterHtml.IsChecked = options.OutputMode == OutputMode.OuterHtml;
                rbInnerHtml.IsChecked = options.OutputMode == OutputMode.InnerHtml;
                rbInnerText.IsChecked = options.OutputMode == OutputMode.InnerText;
            }
            finally
            {
                --ChangeCounter;
            }
        }
    }
}
