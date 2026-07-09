using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json.Serialization;
using System.Threading.Tasks;


namespace DartPlugin
{
    enum PackageEnum
    {
        None,
        RegExp,
    }

    internal class Options
    {
        public PackageEnum package { get; set; } = PackageEnum.RegExp;

        public bool multiLine { get; set; }
        public bool caseSensitive { get; set; }
        public bool unicode { get; set; }
        public bool dotAll { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
