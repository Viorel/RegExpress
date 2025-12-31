using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Windows.Controls;


namespace HtmlAgilityPackPlugin
{
    class Engine : IRegexEngine, IAIBase


    {
        string IAIBase.AIPatternType => Options.SelectorMode == SelectorMode.XPath ? "html xpath" : "html css query selector";
        string IAIBase.AIPatternCodeblockType => Options.SelectorMode == SelectorMode.XPath ? "xpath" : "css";
        string IAIBase.AIAdditionalSystemPrompt => "";
        static readonly Lazy<string?> LazyVersion = new( Matcher.GetVersion );

        Options mOptions = new( );
        readonly Lazy<UCOptions> mOptionsControl;

        public Engine( )
        {
            mOptionsControl = new Lazy<UCOptions>( ( ) =>
            {
                UCOptions oc = new( );
                oc.SetOptions( Options );
                oc.Changed += OptionsControl_Changed;

                return oc;
            } );
        }

        public Options Options
        {
            get
            {
                return mOptions;
            }
            set
            {
                mOptions = value;

                if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
            }
        }

        #region IRegexEngine

        public string Kind => "HtmlAgilityPack";

        public string? Version => LazyVersion.Value;

        public string Name => "HtmlAgilityPack";

        public string Subtitle => mOptions.SelectorMode == SelectorMode.XPath ? "XPath" : "CSS";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.Default;// | RegexEngineCapabilityEnum.NoCaptures;

        public string? NoteForCaptures => "This engine uses XPath or CSS selectors to select HTML nodes, not regex patterns.";

        public event RegexEngineOptionsChanged? OptionsChanged;
#pragma warning disable 0067
        public event EventHandler? FeatureMatrixReady;
#pragma warning restore 0067

        public Control GetOptionsControl( )
        {
            return mOptionsControl.Value;
        }

        public string? ExportOptions( )
        {
            string json = JsonSerializer.Serialize( Options, JsonUtilities.JsonOptions );

            return json;
        }

        public void ImportOptions( string? json )
        {
            if( string.IsNullOrWhiteSpace( json ) )
            {
                Options = new Options( );
            }
            else
            {
                try
                {
                    Options = JsonSerializer.Deserialize<Options>( json, JsonUtilities.JsonOptions )!;
                }
                catch( Exception ex )
                {
                    // ignore versioning errors, for example
                    if( InternalConfig.HandleException( ex ) )
                        throw;

                    Options = new Options( );
                }
            }
        }

        public RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
        {
            return Matcher.GetMatches( cnc, pattern, text, Options );
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            // XPath and CSS selectors have their own syntax, not regex syntax
            // Return Literal mode so no regex syntax highlighting is applied
            return new SyntaxOptions
            {
                Literal = true,
                XLevel = XLevelEnum.none,
                FeatureMatrix = new FeatureMatrix( )
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            // This engine doesn't use regex, so return empty feature matrix
            return [];
        }

        public void SetIgnoreCase( bool yes )
        {
            // XPath/CSS selectors don't have a direct ignore case option
            // but we could potentially add this in the future
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            // Not applicable for XPath/CSS selectors
        }

        #endregion

        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }
    }
}
