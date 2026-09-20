using System;
using System.IO;
using System.Text;

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

        //public static void WriteUTF8( this BinaryWriter binaryWriter, string value )
        //{
        //    var bytelen = value.Length * sizeof( char );
        //    binaryWriter.Write7BitEncodedInt( bytelen );

        //    byte[] bytes = Encoding.UTF8.GetBytes( value );
        //    binaryWriter.Write( bytes );
        //}

        //public static string ReadUTF8( this BinaryReader binaryReader )
        //{
        //    int bytelen = binaryReader.Read7BitEncodedInt( );
        //    byte[] bytes = binaryReader.ReadBytes( bytelen );

        //    return Encoding.UTF8.GetString( bytes );
        //}
    }
}
