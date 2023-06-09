﻿using RegexEngineInfrastructure;
using RegexEngineInfrastructure.Matches;
using RegexEngineInfrastructure.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;


namespace BoostRegexEngineNs
{
	public class BoostRegexEngine : IRegexEngine
	{
		readonly UCBoostRegexOptions OptionsControl;
		static readonly Lazy<string> LazyVersion = new Lazy<string>( GetVersion );

		struct Key
		{
			internal GrammarEnum Grammar;
			internal bool ModX;
		}

		static readonly Dictionary<Key, Regex> CachedColouringRegexes = new Dictionary<Key, Regex>( );
		static readonly Dictionary<Key, Regex> CachedHighlightingRegexes = new Dictionary<Key, Regex>( );


		public BoostRegexEngine( )
		{
			OptionsControl = new UCBoostRegexOptions( );
			OptionsControl.Changed += OptionsControl_Changed;
		}


		#region IRegexEngine

		public string Id => "CppBoostRegex";

		public string Name => "Boost.Regex";

		public string EngineVersion => LazyVersion.Value;

		public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.Default;

		public string NoteForCaptures => "requires ‘match_extra’";

		public event RegexEngineOptionsChanged OptionsChanged;


		public Control GetOptionsControl( )
		{
			return OptionsControl;
		}


		public string[] ExportOptions( )
		{
			return OptionsControl.ExportOptions( );
		}


		public void ImportOptions( string[] options )
		{
			OptionsControl.ImportOptions( options );
		}


		public IMatcher ParsePattern( string pattern )
		{
			var selected_options = OptionsControl.CachedOptions;

			return new BoostRegexInterop.Matcher( pattern, selected_options );
		}


		public void ColourisePattern( ICancellable cnc, ColouredSegments colouredSegments, string pattern, Segment visibleSegment )
		{
			GrammarEnum grammar = OptionsControl.GetGrammar( );
			bool mod_x = OptionsControl.GetModX( );

			Regex regex = GetCachedColouringRegex( grammar, mod_x );

			foreach( Match m in regex.Matches( pattern ) )
			{
				Debug.Assert( m.Success );

				if( cnc.IsCancellationRequested ) return;

				// escapes, '\...'
				{
					var g = m.Groups["escape"];
					if( g.Success )
					{
						if( cnc.IsCancellationRequested ) return;

						foreach( Capture c in g.Captures )
						{
							if( cnc.IsCancellationRequested ) return;

							var intersection = Segment.Intersection( visibleSegment, c.Index, c.Length );

							if( !intersection.IsEmpty )
							{
								colouredSegments.Escapes.Add( intersection );
							}
						}
					}
				}

				if( cnc.IsCancellationRequested ) return;

				// comments, '(?#...)', '#...'
				{
					var g = m.Groups["comment"];
					if( g.Success )
					{
						if( cnc.IsCancellationRequested ) return;

						foreach( Capture c in g.Captures )
						{
							if( cnc.IsCancellationRequested ) return;

							var intersection = Segment.Intersection( visibleSegment, c.Index, c.Length );

							if( !intersection.IsEmpty )
							{
								colouredSegments.Comments.Add( intersection );
							}
						}
					}
				}

				if( cnc.IsCancellationRequested ) return;

				// class (within [...] groups), '[:...:]', '[=...=]', '[. ... .]'
				{
					var g = m.Groups["class"];
					if( g.Success )
					{
						if( cnc.IsCancellationRequested ) return;

						foreach( Capture c in g.Captures )
						{
							if( cnc.IsCancellationRequested ) return;

							var intersection = Segment.Intersection( visibleSegment, c.Index, c.Length );

							if( !intersection.IsEmpty )
							{
								colouredSegments.Escapes.Add( intersection );
							}
						}
					}
				}

				if( cnc.IsCancellationRequested ) return;

				// named group, '(?<name>...)' or '(?'name'...)'
				{
					var g = m.Groups["name"];
					if( g.Success )
					{
						if( cnc.IsCancellationRequested ) return;

						foreach( Capture c in g.Captures )
						{
							if( cnc.IsCancellationRequested ) return;

							var intersection = Segment.Intersection( visibleSegment, c.Index, c.Length );

							if( !intersection.IsEmpty )
							{
								colouredSegments.GroupNames.Add( intersection );
							}
						}
					}
				}
			}
		}


		public void HighlightPattern( ICancellable cnc, Highlights highlights, string pattern, int selectionStart, int selectionEnd, Segment visibleSegment )
		{
			GrammarEnum grammar = OptionsControl.GetGrammar( );
			bool mod_x = OptionsControl.GetModX( );

			int par_size = 1;
			int bracket_size = 1;

			bool is_POSIX_basic =
				grammar == GrammarEnum.basic ||
				grammar == GrammarEnum.sed ||
				grammar == GrammarEnum.grep ||
				grammar == GrammarEnum.emacs;

			if( is_POSIX_basic )
			{
				par_size = 2;
			}

			Regex regex = GetCachedHighlightingRegex( grammar, mod_x );

			HighlightHelper.CommonHighlighting( cnc, highlights, pattern, selectionStart, selectionEnd, visibleSegment, regex, par_size, bracket_size );
		}

		#endregion IRegexEngine


		private void OptionsControl_Changed( object sender, RegexEngineOptionsChangedArgs args )
		{
			OptionsChanged?.Invoke( this, args );
		}


		static Regex GetCachedColouringRegex( GrammarEnum grammar, bool modX )
		{
			var key = new Key { Grammar = grammar, ModX = modX };

			lock( CachedColouringRegexes )
			{
				if( CachedColouringRegexes.TryGetValue( key, out Regex regex ) ) return regex;

				regex = CreateColouringRegex( grammar, modX );

				CachedColouringRegexes.Add( key, regex );

				return regex;
			}
		}


		static Regex GetCachedHighlightingRegex( GrammarEnum grammar, bool modX )
		{
			var key = new Key { Grammar = grammar, ModX = modX };

			lock( CachedHighlightingRegexes )
			{
				if( CachedHighlightingRegexes.TryGetValue( key, out Regex regex ) ) return regex;

				regex = CreateHighlightingRegex( grammar, modX );

				CachedHighlightingRegexes.Add( key, regex );

				return regex;
			}
		}


		static Regex CreateColouringRegex( GrammarEnum grammar, bool modX )
		{
			bool is_perl =
				grammar == GrammarEnum.perl ||
				grammar == GrammarEnum.ECMAScript ||
				grammar == GrammarEnum.normal ||
				grammar == GrammarEnum.JavaScript ||
				grammar == GrammarEnum.JScript;

			bool is_POSIX_extended =
				grammar == GrammarEnum.extended ||
				grammar == GrammarEnum.egrep ||
				grammar == GrammarEnum.awk;

			bool is_POSIX_basic =
				grammar == GrammarEnum.basic ||
				grammar == GrammarEnum.sed ||
				grammar == GrammarEnum.grep ||
				grammar == GrammarEnum.emacs;

			bool is_emacs =
				grammar == GrammarEnum.emacs;


			var pb_escape = new PatternBuilder( );

			pb_escape.BeginGroup( "escape" );

			if( is_perl || is_POSIX_extended || is_POSIX_basic ) pb_escape.Add( @"\\[1-9]" ); // back reference
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\c[A-Za-z]" ); // ASCII escape
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\x[0-9A-Fa-f]{1,2}" ); // hex, two digits
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\x\{[0-9A-Fa-f]+(\}|$)" ); // hex, four digits
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\0[0-7]{1,3}" ); // octal, three digits
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\N\{.*?(\}|$)" ); // symbolic name
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\[pP]\{.*?(\}|$)" ); // property
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\[pP]." ); // property, short name
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\Q.*?(\\E|$)" ); ; // quoted sequence
			if( is_emacs ) pb_escape.Add( @"\\[sS]." ); // syntax group
			if( is_perl || is_POSIX_extended ) pb_escape.Add( @"\\." ); // various
			if( is_POSIX_basic ) pb_escape.Add( @"(?!\\\( | \\\) | \\\{ | \\\})\\." ); // various

			pb_escape.EndGroup( );

			var pb_class = new PatternBuilder( );

			pb_class.BeginGroup( "class" );

			if( is_perl || is_POSIX_extended || is_POSIX_basic ) pb_class.Add( @"\[(?'c'[:=.]) .*? (\k<c>\] | $)" );

			pb_class.EndGroup( );


			var pb = new PatternBuilder( );

			pb.BeginGroup( "comment" );
			if( is_perl ) pb.Add( @"\(\?\#.*?(\)|$)" ); // comment
			if( is_perl && modX ) pb.Add( @"\#.*?(\n|$)" ); // line-comment*/
			pb.EndGroup( );

			if( is_perl ) pb.Add( @"\(\?(?'name'<(?![=!]).*?(>|$)) | \(\?(?'name''.*?('|$))" );
			if( is_perl ) pb.Add( @"(?'name'\\g-?[1-9]) | (?'name'\\g\{.*?(\}|$))" ); // back reference
			if( is_perl ) pb.Add( @"(?'name'\\[gk]<.*?(>|$)) | (?'name'\\[gk]'.*?('|$))" ); // back reference

			if( is_perl || is_POSIX_extended || is_POSIX_basic )
				pb.AddGroup( null, $@"\[ \]? ({pb_class.ToPattern( )} | {pb_escape.ToPattern( )} | . )*? (\]|$)" );

			pb.Add( pb_escape.ToPattern( ) );

			return pb.ToRegex( );
		}


		static Regex CreateHighlightingRegex( GrammarEnum grammar, bool modX )
		{
			bool is_perl =
				grammar == GrammarEnum.perl ||
				grammar == GrammarEnum.ECMAScript ||
				grammar == GrammarEnum.normal ||
				grammar == GrammarEnum.JavaScript ||
				grammar == GrammarEnum.JScript;

			bool is_POSIX_extended =
				grammar == GrammarEnum.extended ||
				grammar == GrammarEnum.egrep ||
				grammar == GrammarEnum.awk;

			bool is_POSIX_basic =
				grammar == GrammarEnum.basic ||
				grammar == GrammarEnum.sed ||
				grammar == GrammarEnum.grep ||
				grammar == GrammarEnum.emacs;

			bool is_emacs =
				grammar == GrammarEnum.emacs;


			var pb = new PatternBuilder( );

			if( is_perl ) pb.Add( @"(\(\?\#.*?(\)|$))" ); // comment
			if( is_perl && modX ) pb.Add( @"(\#[^\n]*)" ); // line comment

			if( is_perl || is_POSIX_extended )
			{
				pb.Add( @"\\Q.*?(\\E|$)" ); // skip \Q...\E
				pb.Add( @"\\[xNpPgk]\{.*?(\}|$)" ); // (skip)
			}

			if( is_perl || is_POSIX_extended )
			{
				pb.AddGroup( "left_par", @"\(" ); // '('
				pb.AddGroup( "right_par", @"\)" ); // ')'
				pb.Add( @"(?'left_brace'\{) \s* \d+ \s* (, \s* \d*)? \s* ((?'right_brace'\})|$)" ); // '{...}' (spaces are allowed)
			}

			if( is_POSIX_basic )
			{
				pb.AddGroup( "left_par", @"\\\(" ); // '\('
				pb.AddGroup( "right_par", @"\\\)" ); // '\)'
				pb.Add( @"(?'left_brace'\\{).*?((?'right_brace'\\})|$)" ); // '\{...\}'
			}

			if( is_perl || is_POSIX_extended || is_POSIX_basic )
			{
				pb.Add( @"((?'left_bracket'\[) \]? ((\[:.*? (:\]|$)) | \\. | .)*? ((?'right_bracket'\])|$) )" ); // [...]
				pb.Add( @"\\." ); // '\...'
			}

			return pb.ToRegex( );
		}


		static string GetVersion( )
		{
			try
			{
				return BoostRegexInterop.Matcher.GetBoostVersion( );
			}
			catch( Exception exc )
			{
				_ = exc;
				if( Debugger.IsAttached ) Debugger.Break( );

				return null;
			}
		}

	}
}
