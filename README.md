# RegExpress

A tester for Regular Expressions. Made in Visual Studio 2022 using C#, C++, WPF, .NET 7.

It includes the following Regular Expression engines:

* **[Regex](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=net-7.0)** class from .NET 7.
* **[Regex](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=net-6.0)** class from .NET 6.
* **[Regex](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=netframework-4.8)** class from .NET Framework 4.8.
* **[wregex](https://docs.microsoft.com/en-us/cpp/standard-library/regex)** class from Standard Template Library.
* **[Boost.Regex](https://www.boost.org/doc/libs/1_75_0/libs/regex/doc/html/index.html)** from Boost C++ Libraries 1.81.0.
* **[PCRE2](https://pcre.org/)** Open Source Regex Library 10.42.
* **[RE2](https://github.com/google/re2)** C++ Library 2023-03-01 from Google.
* **[Oniguruma](https://github.com/kkos/oniguruma)** Regular Expression Library 6.9.8.
* **[SubReg](https://github.com/mattbucknall/subreg)** 2022-01-01.
* **[RegExp](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/RegExp)** JavaScript object from Microsoft Edge [WebView2](https://docs.microsoft.com/en-us/microsoft-edge/webview2/).
* **[RegExp](https://learn.microsoft.com/en-us/previous-versions/yab2dx62(v=vs.85))** VBScript object used in Access, Excel, Word.

There are more engines that are described in **[Additional Engines](#additional-engines)** section:

* **Hyperscan** 
* **Chimera**
* **ICU Regular Expressions**


<br/>

Sample:

![Screenshot of RegExpress](Screenshot1.png)

The regular expressions are saved and loaded automatically. Press the “➕” button to open more tabs. 
Use the **Options** area to select and configure the Regular Expression engine.

The program can be built using Visual Studio 2022. The sources contain code written in C# and C++. 
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

 
## Additional Engines 

This repository includes several additional engines:

* **[Hyperscan](https://github.com/intel/hyperscan)** 5.4.2 from Intel.
* **[Chimera](http://intel.github.io/hyperscan/dev-reference/chimera.html)**, a hybrid of Hyperscan 5.4.2 and PCRE 8.41.
* **[ICU Regular Expressions](https://icu.unicode.org/)** 73.1

To use these engines, get the **main-extended** branch instead of **main**, then recompile 
the same solution.

The **main-extended** branch also includes the regular engines from the **main** branch.

The additional engines require certain third-party library files, which were downloaded or compiled separately 
and included into **main-extended** branch.

<br/>
<br/>
<br/>
<br/>
<br/>

