using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.Json;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

namespace RegExpressLibrary
{
    public static class JsonUtilities
    {
        /// <summary>
        /// Common JSON serialiser options.
        /// </summary>
        public static readonly JsonSerializerOptions JsonOptions = new( )
        {
            AllowTrailingCommas = true,
            IncludeFields = true,
            ReadCommentHandling = JsonCommentHandling.Skip,
            WriteIndented = true,
            Converters = { new JsonStringEnumConverter( namingPolicy: null, allowIntegerValues: false ) },
        };


        /// <summary>
        /// Get bytes of simple struct-like objects. The class sould have the [StructLayout(...)] attribute.
        /// </summary>
        /// <param name="obj"></param>
        /// <returns></returns>
        public static byte[] ToBytes( object obj )
        {
            int size = Marshal.SizeOf( obj );

            IntPtr mem = Marshal.AllocHGlobal( size );
            Marshal.StructureToPtr( structure: obj, ptr: mem, fDeleteOld: false );

            byte[] bytes = new byte[size];
            Marshal.Copy( mem, bytes, 0, size );
            
            Marshal.FreeHGlobal( mem );

            return bytes;
        }

    }
}
