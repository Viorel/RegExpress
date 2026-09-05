using System.Text.Json;
using System.Text.Json.Serialization;


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
    }
}
