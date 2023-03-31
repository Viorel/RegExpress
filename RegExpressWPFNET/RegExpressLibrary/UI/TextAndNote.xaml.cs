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

namespace RegExpressLibrary.UI
{
    /// <summary>
    /// Interaction logic for TextAndNote.xaml
    /// </summary>
    public partial class TextAndNote : UserControl
    {
        public TextAndNote( )
        {
            InitializeComponent( );
        }

        // (See: https://stackoverflow.com/questions/18158500/usercontrol-dependency-property-design-time)


        public static readonly DependencyProperty TextProperty =
            DependencyProperty.Register( "Text", typeof( string ), typeof( TextAndNote ), new PropertyMetadata( "(text undefined)" ) );

        public string Text
        {
            get { return (string)GetValue( TextProperty ); }
            set { SetValue( TextProperty, value ); }
        }


        public static readonly DependencyProperty NoteProperty =
            DependencyProperty.Register( "Note", typeof( string ), typeof( TextAndNote ), new PropertyMetadata( "" ) );

        public string Note
        {
            get { return (string)GetValue( NoteProperty ); }
            set { SetValue( NoteProperty, value ); }
        }

    }
}
