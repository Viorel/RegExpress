using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;


namespace RegExpressLibrary.UI
{
    /// <summary>
    /// Interaction logic for CheckboxWithNote.xaml
    /// </summary>
    public partial class CheckboxWithNote : UserControl
    {
        public static readonly RoutedEvent ChangedEvent = EventManager.RegisterRoutedEvent(
            name: "Changed",
            routingStrategy: RoutingStrategy.Bubble,
            handlerType: typeof( RoutedEventHandler ),
            ownerType: typeof( CheckboxWithNote ) );

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
                    propertyChangedCallback: IsChecked_Changed ) );

        public bool? IsChecked
        {
            get { return (bool?)GetValue( IsCheckedProperty ); }
            set { SetValue( IsCheckedProperty, value ); }
        }

        static void IsChecked_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            ( (CheckboxWithNote)d ).RaiseEvent( new RoutedEventArgs( ChangedEvent ) );
        }

        public static readonly DependencyProperty IsInvertedProperty =
            DependencyProperty.Register(
                name: nameof( IsInverted ),
                propertyType: typeof( bool ),
                ownerType: typeof( CheckboxWithNote ),
                typeMetadata: new PropertyMetadata(
                    defaultValue: false,
                    propertyChangedCallback: IsInverted_Changed ) );

        public bool IsInverted
        {
            get { return (bool)GetValue( IsInvertedProperty ); }
            set { SetValue( IsInvertedProperty, value ); }
        }

        static void IsInverted_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            CheckboxWithNote This = (CheckboxWithNote)d;

            This.ApplyProp( );
        }

        // (See: https://stackoverflow.com/questions/18158500/usercontrol-dependency-property-design-time)

        public static readonly DependencyProperty PropProperty =
            DependencyProperty.Register(
                name: nameof( Prop ),
                propertyType: typeof( string ),
                ownerType: typeof( CheckboxWithNote ),
                typeMetadata: new PropertyMetadata(
                    defaultValue: null,
                    propertyChangedCallback: Prop_Changed ) );

        public string Prop
        {
            get { return (string)GetValue( PropProperty ); }
            set { SetValue( PropProperty, value ); }
        }

        static void Prop_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            CheckboxWithNote This = (CheckboxWithNote)d;

            This.ApplyProp( );
        }

        public static readonly DependencyProperty TextProperty =
            DependencyProperty.Register(
                name: nameof( Text ),
                propertyType: typeof( string ),
                ownerType: typeof( CheckboxWithNote ),
                typeMetadata: new FrameworkPropertyMetadata(
                    defaultValue: "",
                    propertyChangedCallback: Text_Changed ) );

        public string Text
        {
            get { return (string)GetValue( TextProperty ); }
            set { SetValue( TextProperty, value ); }
        }

        static void Text_Changed( DependencyObject d, DependencyPropertyChangedEventArgs e )
        {
            CheckboxWithNote This = (CheckboxWithNote)d;

            This.AdjustCheckboxText( );
        }

        public static readonly DependencyProperty NoteProperty =
            DependencyProperty.Register(
                name: nameof( Note ),
                propertyType: typeof( string ),
                ownerType: typeof( CheckboxWithNote ),
                typeMetadata: new PropertyMetadata(
                    defaultValue: "" ) );

        public string Note
        {
            get { return (string)GetValue( NoteProperty ); }
            set { SetValue( NoteProperty, value ); }
        }

        void userControl_DataContextChanged( object sender, DependencyPropertyChangedEventArgs e )
        {
            ApplyProp( );
        }

        void ApplyProp( )
        {
            if( string.IsNullOrWhiteSpace( Prop ) ) return;

            Binding binding = new( Prop );

            if( IsInverted == true )
            {
                binding.Converter = new InverseNullableBooleanConverter( );
            }

            SetBinding( IsCheckedProperty, binding );

            AdjustCheckboxText( );
        }

        void AdjustCheckboxText( )
        {
            run.Text = string.IsNullOrWhiteSpace( Text ) ? Prop : Text;
        }
    }
}
