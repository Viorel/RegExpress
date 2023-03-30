using System;

namespace RegExpressLibrary
{
    public interface IRegexPlugin
    {
        string Id { get; }
        string Name { get; }
        string Version { get; }
    }
}
