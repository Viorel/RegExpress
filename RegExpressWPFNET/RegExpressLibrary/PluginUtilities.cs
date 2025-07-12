using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;


namespace RegExpressLibrary;

/*
 * See: https://learn.microsoft.com/en-us/dotnet/core/tutorials/creating-app-with-plugin-support
 */

public class PluginLoadContext : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver _resolver;


    public PluginLoadContext( string pluginPath )
    {
        _resolver = new AssemblyDependencyResolver( pluginPath );
    }


    protected override Assembly? Load( AssemblyName assemblyName )
    {
        string? assemblyPath = _resolver.ResolveAssemblyToPath( assemblyName );

        return assemblyPath != null ? LoadFromAssemblyPath( assemblyPath ) : null;
    }


    protected override IntPtr LoadUnmanagedDll( string unmanagedDllName )
    {
        string? libraryPath = _resolver.ResolveUnmanagedDllToPath( unmanagedDllName );

        return libraryPath != null ? LoadUnmanagedDllFromPath( libraryPath ) : IntPtr.Zero;
    }
}


// "engines.json" file
public class EnginesData
{
    public EngineData[]? engines { get; set; }
}

public class EngineData
{
    public string? path { get; set; }
    public bool no_fm { get; set; } // do not include this plugin to "Feature Matrix" exports
}


public static class PluginLoader
{
    public static readonly JsonSerializerOptions JsonOptions = new( ) { AllowTrailingCommas = true, IncludeFields = true, ReadCommentHandling = JsonCommentHandling.Skip, WriteIndented = true };

    public static async Task<IReadOnlyList<RegexPlugin>?> LoadEngines( Window ownerWindow, string enginesJsonPath )
    {
        // -- deserialize "Engines.json"

        EnginesData? engines_data;

        try
        {
            using FileStream plugins_stream = File.OpenRead( enginesJsonPath );

            engines_data = await JsonSerializer.DeserializeAsync<EnginesData>( plugins_stream, JsonOptions );
            Debug.WriteLine( $"Total {engines_data?.engines?.Length} paths" );
        }
        catch( Exception exc )
        {
            if( Debugger.IsAttached ) Debugger.Break( );

            MessageBox.Show( ownerWindow, $"Failed to load plugins using '{enginesJsonPath}'.\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );

            return null;
        }

        // --- load plugins and their engines

        string plugin_root_folder = Path.GetDirectoryName( enginesJsonPath )!;
        List<RegexPlugin> plugins = [];

        if( engines_data?.engines != null )
        {
            foreach( EngineData engine_data in engines_data.engines )
            {
                string plugin_absolute_path = Path.Combine( plugin_root_folder, engine_data.path! );

                try
                {
                    Debug.WriteLine( $"Trying to load plugin \"{plugin_absolute_path}\"..." );

                    PluginLoadContext load_context = new( plugin_absolute_path );

                    var assembly = load_context.LoadFromAssemblyName( new AssemblyName( Path.GetFileNameWithoutExtension( plugin_absolute_path ) ) );

                    var plugin_type = typeof( RegexPlugin );

                    foreach( Type type in assembly.GetTypes( ) )
                    {
                        if( plugin_type.IsAssignableFrom( type ) )
                        {
                            try
                            {
                                Debug.WriteLine( $"Making plugin \"{type.FullName}\"..." );
                                RegexPlugin plugin = (RegexPlugin)Activator.CreateInstance( type )!;
                                plugins.Add( plugin );
                            }
                            catch( Exception exc )
                            {
                                if( Debugger.IsAttached ) Debugger.Break( );

                                MessageBox.Show( ownerWindow, $"Failed to create plugin \"{engine_data.path}\".\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );
                            }

                            if( engine_data.no_fm )
                            {
                                //............
                                //NoFmAssemblies.Add( type.AssemblyQualifiedName! );
                            }
                        }
                    }
                }
                catch( Exception exc )
                {
                    if( Debugger.IsAttached ) Debugger.Break( );

                    MessageBox.Show( $"Failed to load plugin \"{engine_data.path}\".\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );
                }
            }
        }

#if DEBUG
        Debug.WriteLine( $"Total plugins: {plugins.Count}" );

        foreach( var p in plugins )
        {
            foreach( var eng in p.GetEngines( ) )
            {
                Debug.WriteLine( $"   {eng.Kind} {eng.Version}" );
            }
        }
#endif
        if( plugins.Count == 0 )
        {
            MessageBox.Show( ownerWindow, $"No engines loaded using '{plugin_root_folder}'.\r\n", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );

            return null;
        }

        return plugins;
    }
}