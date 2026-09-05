using System;
using System.Text.Json;
using System.Text.Json.Serialization;


namespace RegExpressWPFNET.Code
{
    internal sealed class RawJsonConverter : JsonConverter<string>
    {
        public override string? Read( ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options )
        {
            using( var jsonDocument = JsonDocument.ParseValue( ref reader ) )
            {
                return jsonDocument.RootElement.GetRawText( );
            }
        }

        public override void Write( Utf8JsonWriter writer, string value, JsonSerializerOptions options )
        {
            writer.WriteRawValue( Environment.NewLine + value );
        }
    }
}
