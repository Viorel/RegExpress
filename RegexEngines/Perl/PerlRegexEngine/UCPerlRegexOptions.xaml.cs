﻿using RegexEngineInfrastructure;
using RegexEngineInfrastructure.UI;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;


namespace PerlRegexEngineNs
{
	/// <summary>
	/// Interaction logic for UCPerlRegexOptions.xaml
	/// </summary>
	partial class UCPerlRegexOptions : UserControl
	{
		internal event EventHandler<RegexEngineOptionsChangedArgs> Changed;
		internal string[] CachedOptions; // (accessible from threads)


		bool IsFullyLoaded = false;
		int ChangeCounter = 0;


		public UCPerlRegexOptions( )
		{
			InitializeComponent( );

			// insert checkboxes
			{
				var options = Matcher.GetOptionInfoList( );

				foreach( var o in options )
				{
					var cb = new CheckBox
					{
						Tag = o.Modifier,
						Content = new TextAndNote { Text = o.Modifier, Note = o.Note }
					};

					pnlOptions.Children.Add( cb );
				}
			}
		}


		internal string[] ExportOptions( )
		{
			return GetSelectedOptions( );
		}


		internal void ImportOptions( string[] options )
		{
			SetSelectedOptions( options );
		}


		internal string[] GetSelectedOptions( )
		{
			var selected_options =
				pnlOptions.Children.OfType<CheckBox>( )
					.Where( cb => cb.IsChecked == true )
					.Select( cb => cb.Tag.ToString( ) );

			if( chbUseReStrict.IsChecked == true )
			{
				selected_options = selected_options.Append( "strict" );
			}

			return selected_options.ToArray( );
		}


		internal void SetSelectedOptions( string[] options )
		{
			try
			{
				++ChangeCounter;

				options = options ?? new string[] { };

				foreach( var cb in pnlOptions.Children.OfType<CheckBox>( ) )
				{
					cb.IsChecked = options.Contains( cb.Tag );
				}

				chbUseReStrict.IsChecked = options.Contains( "strict" );
			}
			finally
			{
				--ChangeCounter;
			}
		}


		internal bool IsModifierSelected( string m )
		{
			return CachedOptions.Any( o => o == m );
		}


		internal bool IsReStrict => CachedOptions.Contains( "strict" );


		private void UserControl_Loaded( object sender, RoutedEventArgs e )
		{
			if( IsFullyLoaded ) return;

			CachedOptions = GetSelectedOptions( );

			IsFullyLoaded = true;
		}


		private void CheckBox_Changed( object sender, RoutedEventArgs e )
		{
			if( !IsFullyLoaded ) return;
			if( ChangeCounter != 0 ) return;

			CachedOptions = GetSelectedOptions( );

			Changed?.Invoke( null, new RegexEngineOptionsChangedArgs { PreferImmediateReaction = false } );
		}

	}
}
