with Ada.Characters;
with Ada.Characters.Latin_1;
with Ada.Strings.Maps;
with Ada.Text_IO;
with Ada.Integer_Text_IO;
with Ada.Strings.Unbounded.Text_IO;
with Ada.Exceptions;
with Ada.Wide_Characters;
with Ada.Wide_Characters.Unicode;
with GNATCOLL.Buffer;
with GNATCOLL.JSON; 
with GNAT.Regpat;
with GNATCOLL.OS;
with GNATCOLL.OS.FS;


procedure AdaWorker is

   use type GNAT.Regpat.Regexp_Flags; -- for 'or' operators

   -- example of input: "{""pattern"": ""(((a)|(b))"", ""text"": ""abc\u00D0""}";

   inputString       : GNATCOLL.JSON.UTF8_Unbounded_String;
   line              : Ada.Strings.Unbounded.Unbounded_String;

   inputJson         : GNATCOLL.JSON.JSON_Value;
   value             : GNATCOLL.JSON.JSON_Value;
   pattern           : GNATCOLL.JSON.UTF8_Unbounded_String;
   text              : GNATCOLL.JSON.UTF8_Unbounded_String;
   options           : GNATCOLL.JSON.JSON_Value;

   reflags           : GNAT.Regpat.Regexp_Flags := GNAT.Regpat.No_Flags;

   b : Boolean;
begin

   -- read all lines

   loop
      line := Ada.Strings.Unbounded.Text_IO.Get_Line;

      inputString.Append( line);
      inputString.Append( ASCII.CR);
      inputString.Append( ASCII.LF);

      if Ada.Text_IO.Standard_Input.End_Of_File then
         exit;
      end if;

   end loop;

   inputJson := GNATCOLL.JSON.Read( inputString);

   value := inputJson.Get( "pattern");
   pattern := value.Get;

   value := inputJson.Get( "text");
   text := value.Get;

   --Ada.Text_IO.Put_Line(jsonValue.Write);
   --Ada.Text_IO.Put_Line(pattern.To_String);
   --Ada.Text_IO.Put_Line(text.To_String);

   options := inputJson.Get( "options");

   if not options.Is_Empty then 
      value := options.Get( "Case_Insensitive");
      if not value.Is_Empty then
         b := value.Get;
         if b then
            reflags := reflags or GNAT.Regpat.Case_Insensitive;
         end if;
      end if;

      value := options.Get( "Single_Line");
      if not value.Is_Empty then
         b := value.Get;
         if b then
            reflags := reflags or GNAT.Regpat.Single_Line;
         end if;
      end if;

      value := options.Get( "Multiple_Lines");
      if not value.Is_Empty then
         b := value.Get;
         if b then
            reflags := reflags or GNAT.Regpat.Multiple_Lines;
         end if;
      end if;
   end if;

   declare

      txt         : String := text.To_String;
      start       : Positive := txt'First;

      matcher     : GNAT.Regpat.Pattern_Matcher := GNAT.Regpat.Compile( pattern.To_String, reflags);--, Flags => Regexp_Flags)
      matches     : GNAT.Regpat.Match_Array( 0..99);
      parcount    : GNAT.Regpat.Match_Count := GNAT.Regpat.Paren_Count( matcher);

   begin

      loop

         GNAT.Regpat.Match( matcher, txt(start..txt'Last), matches);

         if matches(0).First = 0 then -- '0' means 'did not match'
            exit;
         end if;

         Ada.Text_IO.Put( "m "); 
         Ada.Integer_Text_IO.Put( Item => matches( 0).First, Width => 0);
         Ada.Text_IO.Put( " ");
         Ada.Integer_Text_IO.Put( Item => matches( 0).Last, Width => 0);
         Ada.Text_IO.Put_Line( "");

         for i in 1 .. parcount loop 
            Ada.Text_IO.Put( " g "); 
            Ada.Integer_Text_IO.Put( Item => matches( i).First, Width => 0);
            Ada.Text_IO.Put( " ");
            Ada.Integer_Text_IO.Put( Item => matches( i).Last, Width => 0);
            Ada.Text_IO.Put_Line( "");
         end loop;

         if matches(0).Last < start then -- (for example, whem pattern is ".?", the last result looks abnormal')
            start := start + 1;
         else
            start := matches(0).Last + 1;
         end if;

         if start > txt'Last then 
            exit;
         end if;

      end loop;

   end;

exception

   when error: GNATCOLL.JSON.Invalid_JSON_Stream =>
      Ada.Text_IO.Standard_Error.Put_Line( Ada.Exceptions.Exception_Information( error));
      Ada.Text_IO.Standard_Error.Put( "Input was: ");
      Ada.Text_IO.Standard_Error.Put_Line( inputString.To_String);

   when error: others =>
      Ada.Text_IO.Standard_Error.Put_Line( Ada.Exceptions.Exception_Information( error));

end AdaWorker;
