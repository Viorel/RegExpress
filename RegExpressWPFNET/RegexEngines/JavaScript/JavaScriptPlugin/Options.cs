using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace JavaScriptPlugin
{
    enum RuntimeEnum
    {
        None,
        WebView2,
        NodeJs,
        QuickJs,
        SpiderMonkey,
        Bun,
        RE2JS,
    }

    enum FunctionEnum
    {
        None,
        MatchAll,
        Exec
    }

    class Options : INotifyPropertyChanged
    {
        private bool m_i, m_s, m_m;

        public RuntimeEnum Runtime { get; set; } = RuntimeEnum.WebView2;
        public FunctionEnum Function { get; set; } = FunctionEnum.Exec;

        public bool i
        {
            get => m_i;
            set
            {
                if( m_i != value ) { m_i = value; NotifyPropertyChanged( ); }
            }
        }
        public bool m
        {
            get => m_m;
            set
            {
                if( m_m != value ) { m_m = value; NotifyPropertyChanged( ); }
            }
        }
        public bool s
        {
            get => m_s;
            set
            {
                if( m_s != value ) { m_s = value; NotifyPropertyChanged( ); }
            }
        }
        public bool u { get; set; }
        public bool y { get; set; }
        public bool g { get; set; } = true;

        // V8 (WebView2, NodeJs)

        public bool v { get; set; }

        // SpiderMonkey

        public bool NoNativeRegexp { get; set; } // "--no-native-regexp"
        public bool EnableDuplicateNames { get; set; } // "--enable-regexp-duplicate-named-groups"
        public bool EnableRegexpModifiers { get; set; } // "--enable-regexp-modifiers"

        // RE2JS

        public bool DISABLE_UNICODE_GROUPS { get; set; }
        public bool LONGEST_MATCH { get; set; }

        [JsonIgnore]
        public bool d { get; set; } = true; // to avoid binding errors and to show the checkbox in checked state


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }

        void NotifyPropertyChanged( [CallerMemberName] string? propertyName = null )
        {
            PropertyChanged?.Invoke( this, new PropertyChangedEventArgs( propertyName ) );
        }

        #region INotifyPropertyChanged

        public event PropertyChangedEventHandler? PropertyChanged;

        #endregion
    }
}
