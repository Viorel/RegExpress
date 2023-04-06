using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
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
    /// Interaction logic for CheckboxWithNote.xaml
    /// </summary>
    public partial class CheckboxWithNote : UserControl
    {
        public static readonly RoutedEvent ChangedEvent = EventManager.RegisterRoutedEvent( "Changed", RoutingStrategy.Bubble, typeof( RoutedEventHandler ), typeof( CheckboxWithNote ) );

        public event RoutedEventHandler Changed
        {
            add { AddHandler( ChangedEvent, value ); }
            remove { RemoveHandler( ChangedEvent, value ); }
        }


        public CheckboxWithNote( )
        {
            InitializeComponent( );
        }


        public static readonly DependencyProperty IsCheckedProperty =
            DependencyProperty.Register(
                name: nameof( IsChecked ),
                propertyType: typeof( bool? ),
                ownerType: typeof( CheckboxWithNote ),
                typeMetadata: new FrameworkPropertyMetadata(
                    defaultValue: false,
                    flags: FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
                    propertyChangedCallback: pccb ) );

        private static void pccb( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            ( (CheckboxWithNote)d ).RaiseEvent( new RoutedEventArgs( ChangedEvent ) );
        }

        public bool? IsChecked
        {
            get { return (bool?)GetValue( IsCheckedProperty ); }
            set { SetValue( IsCheckedProperty, value ); }
        }


        // (See: https://stackoverflow.com/questions/18158500/usercontrol-dependency-property-design-time)


        public static readonly DependencyProperty TextProperty =
            DependencyProperty.Register( nameof( Text ), typeof( string ), typeof( CheckboxWithNote ), new PropertyMetadata( "(text undefined)" ) );

        public string Text
        {
            get { return (string)GetValue( TextProperty ); }
            set { SetValue( TextProperty, value ); }
        }


        public static readonly DependencyProperty NoteProperty =
            DependencyProperty.Register( nameof( Note ), typeof( string ), typeof( CheckboxWithNote ), new PropertyMetadata( "" ) );

        public string Note
        {
            get { return (string)GetValue( NoteProperty ); }
            set { SetValue( NoteProperty, value ); }
        }
    }
}
