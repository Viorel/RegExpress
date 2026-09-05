using System.Windows;

namespace RegExpressLibrary.UI
{
    public static class UIUtilities
    {
        // TODO: use extension properties (C# 14)

        public static void Display( this FrameworkElement e, bool yes )
        {
            e.Visibility = yes ? Visibility.Visible : Visibility.Collapsed;
        }

        public static void Display( this FrameworkElement[] a, bool yes )
        {
            foreach( FrameworkElement e in a )
            {
                e.Visibility = yes ? Visibility.Visible : Visibility.Collapsed;
            }
        }

        public static bool IsDisplayed( this FrameworkElement e )
        {
            return e.Visibility == Visibility.Visible;
        }
    }
}
