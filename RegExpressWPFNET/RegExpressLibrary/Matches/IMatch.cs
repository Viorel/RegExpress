using System.Collections.Generic;


namespace RegExpressLibrary.Matches
{
    public interface IMatch : IGroup // TODO: reconsider the inheritance
    {
        IEnumerable<IGroup> Groups { get; }
    }
}
