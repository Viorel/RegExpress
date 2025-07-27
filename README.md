# RegExpress

A tester for Regular Expressions. Made in Visual Studio 2022 using C#, C++, WPF, .NET 9.

It includes the following Regular Expression engines:

* **[Regex](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=net-9.0)** class from .NET 9.
* **[Regex](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=netframework-4.8)** class from .NET Framework 4.8.
* **[wregex](https://docs.microsoft.com/en-us/cpp/standard-library/regex)** class from C++ Standard Template Library.
* **[Boost.Regex](https://www.boost.org/doc/libs/1_88_0/libs/regex/doc/html/index.html)** from Boost C++ Libraries 1.88.0.
* **[PCRE2](https://pcre.org/)** Open Source Regex Library 10.45 (in C).
* **[RE2](https://github.com/google/re2)** Library 2025-06-26b from Google (in C++).
* **[Oniguruma](https://github.com/kkos/oniguruma)** Regular Expression Library 6.9.10 (in C++).
* **[SubReg](https://github.com/mattbucknall/subreg)** 2024-08-11 (in C).
* **JavaScript [RegExp](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/RegExp)** object in Microsoft Edge [WebView2](https://docs.microsoft.com/en-us/microsoft-edge/webview2/).
* **VBScript [RegExp](https://learn.microsoft.com/en-us/previous-versions/yab2dx62(v=vs.85))** object used in Access, Excel, Word.
* **[Hyperscan](https://github.com/intel/hyperscan)** 5.4.2 from Intel (in C).
* **[Chimera](http://intel.github.io/hyperscan/dev-reference/chimera.html)**, a hybrid of Hyperscan 5.4.2 and PCRE 8.41 (in C).
* **[ICU Regular Expressions](https://icu.unicode.org/)** 77.1 (in C++).
* **Rust** 1.88.0 crates: **[regex](https://docs.rs/regex)** 1.11.1, **[regex\_lite](https://docs.rs/regex_lite)** 0.1.6, **[fancy\_regex](https://docs.rs/fancy-regex)** 0.15.0 and **[regress](https://docs.rs/regress)** 0.10.3.
* **[Java](https://docs.oracle.com/en/java/javase/24/docs/api/java.base/java/util/regex/package-summary.html)** 24.0.1 (*java.util.regex* and *com.google.re2j* packages).
* **[Python](https://www.python.org/)** 3.13.2 (standard *re* module, third-party *regex* module).
* **[D](https://dlang.org/phobos/std_regex.html)** 2.109.1 (*std.regex* module).
* **[Perl](https://perldoc.perl.org/perlreref)** 5.40.2 (Strawberry Perl).
* **Fortran [Forgex](https://github.com/ShinobuAmasaki/forgex)** v4.6 module (Intel® Fortran Compiler 2025.1.0).
* **[TRE](https://github.com/laurikari/tre)** 0.9.0 (in C).
* **[tiny-regex-c](https://github.com/rurban/tiny-regex-c)** 2022-06-21 (in C).


<br/>

Sample:

![Screenshot of RegExpress](Screenshot1.png)

The regular expressions are saved and loaded automatically. Press the “➕” button to open more tabs. 
Use the **Options** area to select and configure the Regular Expression engine.

The program can be built using Visual Studio 2022 and .NET 9. The sources are written in C# and C++. 

The following Visual Studio workloads are required:

* .NET desktop development.
* Desktop development with C++.

The minimal sources of third-party regular expression libraries are included.

#### Details

* Principal GIT branch: **main**.
* Solution file: **RegExpressWPFNET.sln**.
* Startup project: **RegExpressWPFNET**.
* Configurations: **“Debug, Any CPU”** or **“Release, Any CPU”**. The C++ projects use **“x64”**.
* Operating Systems: **Windows 11**, **Windows 10**.

<br/>

 
Some of engines require certain third-party library files, which were downloaded or compiled separately 
and included into **main** branch. (No additional installations required).

<br/>

## Feature Matrix

The various functionalities of regular expression engines are presented in the Excel and HTML file.

![Feature Matrix](FM.png)

Download and open the file:

* [Excel](./RegExpressWPFNET/Tools/ExportFeatureMatrix/RegexFeatureMatrix.xlsx),
* [HTML](./RegExpressWPFNET/Tools/ExportFeatureMatrix/RegexFeatureMatrix.html).

<br/>
<br/>
<br/>
