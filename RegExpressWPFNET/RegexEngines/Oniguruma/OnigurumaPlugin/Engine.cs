using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary;
using RegExpressLibrary.Matches;


namespace OnigurumaPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<Version> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;
        //...        static readonly Dictionary<string, FeatureMatrix> CachedFeatureMatrices = new Dictionary<string, FeatureMatrix>( );


        public Engine( )
        {
            mOptionsControl = new Lazy<UCOptions>( ( ) =>
            {
                var oc = new UCOptions( );
                oc.Changed += OptionsControl_Changed;

                return oc;
            } );
        }


        #region IRegexEngine

        public string Kind => "Oniguruma";

        public Version Version => LazyVersion.Value;

        public string Name => "Oniguruma";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.Default;

        public string? NoteForCaptures => "requires ‘ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY’";

        public event RegexEngineOptionsChanged? OptionsChanged;


        public Control GetOptionsControl( )
        {
            return mOptionsControl.Value;
        }


        public string? ExportOptions( )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );
            string json = JsonSerializer.Serialize( options, JsonUtilities.JsonOptions );

            return json;
        }


        public void ImportOptions( string? json )
        {
            Options options_obj;

            if( string.IsNullOrWhiteSpace( json ) )
            {
                options_obj = new Options( );
            }
            else
            {
                try
                {
                    options_obj = JsonSerializer.Deserialize<Options>( json, JsonUtilities.JsonOptions )!;
                }
                catch
                {
                    // ignore versioning errors, for example
                    if( Debugger.IsAttached ) Debugger.Break( );

                    options_obj = new Options( );
                }
            }

            mOptionsControl.Value.SetSelectedOptions( options_obj );
        }


        public IMatcher ParsePattern( string pattern )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );

            return new Matcher( pattern, options );

        }

        public FeatureMatrix GetFeatureMatrix( )
        {
            //...
            return new FeatureMatrix( );
        }


        public GenericOptions GetGenericOptions( )
        {
            //...
            return new GenericOptions
            {
                Literal = true,
            };
        }


        public IReadOnlyList<(string variantName, FeatureMatrix fm)> GetFeatureMatrices( )
        {
            var list = new List<(string, FeatureMatrix)>( );

            return list;
        }

        #endregion


        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }


        static Version? GetVersion( )
        {
            return Matcher.GetVersion( NonCancellable.Instance );
        }


        //static FeatureMatrix BuildFeatureMatrix( GrammarEnum grammar )
        //{
        // //........
        //}
    }
}
