using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace DotNETFrameworkPlugin
{
    class Matcher : IMatcher
    {
        class VersionResponse
        {
            public Version version { get; set; }
        }


        class ClientMatch
        {
            public int index { get; set; }
            public int length { get; set; }
            public List<ClientGroup> groups { get; set; } = new List<ClientGroup>();
        }


        class ClientGroup
        {
            public bool success { get; set; }
            public int index { get; set; }
            public int length { get; set; }
            public string name { get; set; }
            public List<ClientCapture> captures { get; set; } = new List<ClientCapture>();
        }


        class ClientCapture
        {
            public int index { get; set; }
            public int length { get; set; }
        }


        readonly string Pattern;
        readonly Options Options;


        public Matcher(string pattern, Options options)
        {
            Pattern = pattern;
            Options = options;
        }


        #region IMatcher

        public RegexMatches Matches(string text, ICancellable cnc)
        {
            var data = new { cmd = "m", text, pattern = Pattern, options = Options };

            string json = JsonSerializer.Serialize(data);

            string stdout_contents;
            string stderr_contents;

            bool r = ProcessUtilities.InvokeExe(cnc, GetClientExePath(), null, json, out stdout_contents, out stderr_contents, EncodingEnum.UTF8);

            if (!string.IsNullOrWhiteSpace(stderr_contents))
            {
                throw new Exception(stderr_contents);
            }

            ClientMatch[] client_matches = JsonSerializer.Deserialize<ClientMatch[]>(stdout_contents);

            SimpleMatch[] matches = new SimpleMatch[client_matches.Length];
            SimpleTextGetter text_getter = new SimpleTextGetter(text);

            for (int i = 0; i < client_matches.Length; i++)
            {
                ClientMatch m = client_matches[i];
                SimpleMatch sm = SimpleMatch.Create(m.index, m.length, text_getter);

                foreach (var g in m.groups)
                {
                    var sg = sm.AddGroup(g.index, g.length, g.success, g.name ?? string.Empty);

                    foreach (var c in g.captures)
                    {
                        sg.AddCapture(c.index, c.length);
                    }
                }

                matches[i] = sm;
            }

            return new RegexMatches(matches.Length, matches);
        }

        #endregion IMatcher


        public static Version GetVersion()
        {
            try
            {
                string stdout_contents;
                string stderr_contents;

                if (!ProcessUtilities.InvokeExe(NonCancellable.Instance, GetClientExePath(), null, @"{""cmd"":""v""}", out stdout_contents, out stderr_contents, EncodingEnum.UTF8))
                {
                    return null;
                }

                if (!string.IsNullOrWhiteSpace(stderr_contents))
                {
                    throw new Exception(stderr_contents);
                }

                VersionResponse response = JsonSerializer.Deserialize<VersionResponse>(stdout_contents)!;

                return response.version;
            }
            catch (Exception exc)
            {
                _ = exc;
                if (Debugger.IsAttached) Debugger.Break();

                return null;
            }
        }


        static string GetClientExePath()
        {
            string assembly_location = Assembly.GetExecutingAssembly().Location;
            string assembly_dir = Path.GetDirectoryName(assembly_location)!;
            string client_exe = Path.Combine(assembly_dir, "Client", @"DotNETFrameworkClient.bin"  );

            return client_exe;
        }

    }
}
