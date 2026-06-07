using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.DirectoryServices.ActiveDirectory;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace RegExpressLibrary.UI
{
    public class ComboBoxItemWithVersion : ComboBoxItem
    {
        public static readonly DependencyProperty TextProperty =
            DependencyProperty.Register( nameof( Text ), typeof( string ), typeof( ComboBoxItemWithVersion ),
                new FrameworkPropertyMetadata(
                    defaultValue: "",
                    propertyChangedCallback: Text_Changed
                    ) );

        public string Text
        {
            get { return (string)GetValue( TextProperty ); }
            set { SetValue( TextProperty, value ); }
        }

        private static void Text_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            var This = (ComboBoxItemWithVersion)d;

            This.AdjustContent( );
        }

        public static readonly DependencyProperty VersionProperty =
            DependencyProperty.Register( nameof( Version ), typeof( string ), typeof( ComboBoxItemWithVersion ),
                new FrameworkPropertyMetadata(
                    defaultValue: "",
                    propertyChangedCallback: Version_Changed
                    ) );

        public string Version
        {
            get { return (string)GetValue( VersionProperty ); }
            set { SetValue( VersionProperty, value ); }
        }

        private static void Version_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            var This = (ComboBoxItemWithVersion)d;

            This.AdjustContent( );
        }

        private void AdjustContent( )
        {
            string? prefix = Text;
            if( string.IsNullOrWhiteSpace( prefix ) ) prefix = Tag.ToString( );

            string version = Version;

            if( string.IsNullOrWhiteSpace( version ) )
            {
                Content = $"{prefix}";
            }
            else
            {
                Content = $"{prefix} {Version}";
            }

        }


        protected override void OnPropertyChanged( DependencyPropertyChangedEventArgs e )
        {
            base.OnPropertyChanged( e );

            if( e.Property.Name == "Tag")
            {
                AdjustContent( );
            }
        }
    }
}
