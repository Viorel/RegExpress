﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegexEngineInfrastructure.SyntaxColouring
{
	public sealed class UnderlineInfo
	{
		public IReadOnlyList<Segment> Segments { get; }

		public UnderlineInfo( IReadOnlyList<Segment> segments )
		{
			Segments = segments;
		}


		public static UnderlineInfo Empty => new UnderlineInfo( Enumerable.Empty<Segment>( ).ToList( ) );
	}
}
