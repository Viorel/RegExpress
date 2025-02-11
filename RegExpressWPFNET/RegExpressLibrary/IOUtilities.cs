using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary
{
    public static class IOUtilities
    {
        public static void WriteOptional( this BinaryWriter binaryWriter, Int32? value )
        {
            binaryWriter.Write( value != null );
            if( value != null ) binaryWriter.Write( value.Value );
        }

        public static void WriteOptional( this BinaryWriter binaryWriter, UInt32? value )
        {
            binaryWriter.Write( value != null );
            if( value != null ) binaryWriter.Write( value.Value );
        }

        public static void WriteOptional( this BinaryWriter binaryWriter, Int64? value )
        {
            binaryWriter.Write( value != null );
            if( value != null ) binaryWriter.Write( value.Value );
        }

        public static void WriteOptional( this BinaryWriter binaryWriter, UInt64? value )
        {
            binaryWriter.Write( value != null );
            if( value != null ) binaryWriter.Write( value.Value );
        }
    }
}
