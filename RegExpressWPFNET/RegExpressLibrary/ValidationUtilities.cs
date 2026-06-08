using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary
{
    public class ValidationUtilities
    {
        public static Int128? ParseInt128( string name, string? input )
        {
            if( string.IsNullOrWhiteSpace( input ) ) return null;

            if( !Int128.TryParse( input, CultureInfo.InvariantCulture, out var result ) )
            {
                throw new ApplicationException( $"Invalid field: “{name}”. Enter an integer value." );
            }
            else
            {
                return result;
            }
        }

        public static UInt128? ParseUInt128( string name, string? input )
        {
            if( string.IsNullOrWhiteSpace( input ) ) return null;

            if( !UInt128.TryParse( input, CultureInfo.InvariantCulture, out var result ) )
            {
                throw new ApplicationException( $"Invalid field: “{name}”. Enter an integer unsigned value." );
            }
            else
            {
                return result;
            }
        }

        public static Int64? ParseInt64( string name, string? input )
        {
            Int128? result = ParseInt128( name, input );

            if( result == null ) return null;

            if( result.Value > Int64.MaxValue ) throw new ApplicationException( $"“{name}” is too large. Must not be greater than {Int64.MaxValue}." );
            if( result.Value < Int64.MinValue ) throw new ApplicationException( $"“{name}” is too small. Must not be less than {Int64.MinValue}." );

            return unchecked((Int64)result.Value);
        }

        public static UInt64? ParseUInt64( string name, string? input )
        {
            UInt128? result = ParseUInt128( name, input );

            if( result == null ) return null;

            if( result.Value > UInt64.MaxValue ) throw new ApplicationException( $"“{name}” is too large. Must not be greater than {UInt64.MaxValue}." );

            return unchecked((UInt64)result.Value);
        }

        public static Int32? ParseInt32( string name, string? input )
        {
            Int128? result = ParseInt128( name, input );

            if( result == null ) return null;

            if( result.Value > Int32.MaxValue ) throw new ApplicationException( $"“{name}” is too large. Must not be greater than {Int32.MaxValue}." );
            if( result.Value < Int32.MinValue ) throw new ApplicationException( $"“{name}” is too small. Must not be less than {Int32.MinValue}." );

            return unchecked((Int32)result.Value);
        }

        public static UInt32? ParseUInt32( string name, string? input )
        {
            UInt128? result = ParseUInt128( name, input );

            if( result == null ) return null;

            if( result.Value > UInt32.MaxValue ) throw new ApplicationException( $"“{name}” is too large. Must not be greater than {UInt32.MaxValue}." );

            return unchecked((UInt32)result.Value);
        }

        public static double? ParseDouble( string name, string? input )
        {
            if( string.IsNullOrWhiteSpace( input ) ) return null;

            if( !double.TryParse( input, CultureInfo.InvariantCulture, out var result ) )
            {
                throw new ApplicationException( $"Invalid field: “{name}”. Enter a floating-point value." );
            }
            else
            {
                return result;
            }
        }
    }
}
