using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
    /*
     * See: https://learn.microsoft.com/en-us/dotnet/core/tutorials/creating-app-with-plugin-support
     */

    class PluginLoadContext : AssemblyLoadContext
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

    class EnginesData
    {
        public EngineData[]? engines { get; set; }
    }


    class EngineData
    {
        public string? path { get; set; }
        public bool no_fm { get; set; } // do not include this engine to "Feature Matrix"
    }

}
