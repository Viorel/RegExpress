﻿using RegexEngineInfrastructure.Matches;
using RegexEngineInfrastructure.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Controls;


namespace RegexEngineInfrastructure
{

	public class RegexEngineOptionsChangedArgs : EventArgs
	{
		public bool PreferImmediateReaction;
	}


	public delegate void RegexEngineOptionsChanged( IRegexEngine sender, RegexEngineOptionsChangedArgs args );


	public interface IRegexEngine
	{
		string Id { get; }

		string Name { get; }

		string EngineVersion { get; }

		RegexEngineCapabilityEnum Capabilities { get; }

		string NoteForCaptures { get; }

		event RegexEngineOptionsChanged OptionsChanged;

		Control GetOptionsControl( );

		string[] ExportOptions( );

		void ImportOptions( string[] options );

		IMatcher ParsePattern( string pattern );

		void ColourisePattern( ICancellable cnc, ColouredSegments colouredSegments, string pattern, Segment visibleSegment );

		void HighlightPattern( ICancellable cnc, Highlights highlights, string pattern, int selectionStart, int selectionEnd, Segment visibleSegment );
	}
}
