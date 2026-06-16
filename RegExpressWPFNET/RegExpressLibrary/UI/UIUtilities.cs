using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

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

    }
}
