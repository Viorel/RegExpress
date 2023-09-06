using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Documents;
using RegExpressLibrary;


namespace RegExpressWPFNET.Code.OutputInfo
{
    abstract class Info
    {
        internal abstract MatchInfo GetMatchInfo( );
    }

    sealed class MatchInfo : Info
    {
        internal readonly Segment MatchSegment;
        internal readonly Span Span;
        internal readonly Inline ValueInline;
        internal readonly List<GroupInfo> GroupInfos = new( );

        public MatchInfo( Segment matchSegment, Span span, Inline valueInline )
        {
            MatchSegment = matchSegment;
            Span = span;
            ValueInline = valueInline;
        }

        internal override MatchInfo GetMatchInfo( ) => this;
    }

    sealed class GroupInfo : Info
    {
        internal readonly MatchInfo Parent;
        internal readonly bool IsSuccess;
        internal readonly Segment GroupSegment;
        internal readonly Span Span;
        internal readonly Inline ValueInline;
        internal readonly List<CaptureInfo> CaptureInfos = new( );
        internal readonly bool NoGroupDetails;

        public GroupInfo( MatchInfo parent, bool isSuccess, Segment groupSegment, Span span, Inline valueInline, bool noGroupDetails )
        {
            Parent = parent;
            IsSuccess = isSuccess;
            GroupSegment = groupSegment;
            Span = span;
            ValueInline = valueInline;
            NoGroupDetails = noGroupDetails;
        }

        internal override MatchInfo GetMatchInfo( ) => Parent.GetMatchInfo( );
    }

    sealed class CaptureInfo : Info
    {
        internal readonly GroupInfo Parent;
        internal readonly Segment CaptureSegment;
        internal readonly Span Span;
        internal readonly Inline ValueInline;

        public CaptureInfo( GroupInfo parent, Segment captureSegment, Span span, Inline valueInline )
        {
            Parent = parent;
            CaptureSegment = captureSegment;
            Span = span;
            ValueInline = valueInline;
        }

        internal override MatchInfo GetMatchInfo( ) => Parent.GetMatchInfo( );
    }
}
