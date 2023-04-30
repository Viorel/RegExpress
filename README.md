# RegExpress

A tester for Regular Expressions. Made in Visual Studio 2022 using C#, WPF, .NET 7.

It includes the following Regular Expression engines:

* **[_Regex_](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=net-7.0)** class from .NET 7
* **[_Regex_](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=net-6.0)** class from .NET 6
* **[_Regex_](https://learn.microsoft.com/en-us/dotnet/api/system.text.regularexpressions.regex?view=netframework-4.8)** class from .NET Framework 4.8
* **[_wregex_](https://docs.microsoft.com/en-us/cpp/standard-library/regex)** class from Standard Template Library
* **[Boost.Regex](https://www.boost.org/doc/libs/1_75_0/libs/regex/doc/html/index.html)** from Boost C++ Libraries 1.81.0
* **[PCRE2](https://pcre.org/)** Open Source Regex Library 10.42
* **[RE2](https://github.com/google/re2)** C++ Library 2023-03-01 from Google
* **[Oniguruma](https://github.com/kkos/oniguruma)** Regular Expression Library 6.9.8
* **[SubReg](https://github.com/mattbucknall/subreg)** 2022-01-01

<br/>

Sample:

![Screenshot of RegExpress](Screenshot1.png)

The regular expressions are saved and loaded automatically. Press the “➕” button to open more tabs.

The code can be rebuilt using Visual Studio 2022. The sources contain code written in C#, C, and C++. The next Visual Studio workloads are required:

* .NET desktop development,
* Desktop development with C++.

The minimal sources of third-party regular expression libraries are included.

The **main** GIT branch contains the latest sources.

<br/>


> _This version is based on .NET 7._<br/>
> _There is an [alternative](https://github.com/Viorel/RegExpress_WPFFW) made in .NET Framework 4.8._ <br/> 
> _It contains additional engines: ICU, Perl, Python, Rust, D, WebView2, Hyperscan, Chimera and Swift._
