@rem TODO: adjust the JDK_ROOT variable; See "Readme".
@set JDK_ROOT=T:\Limbaje\jdk-26.0.2.1
"%JDK_ROOT%\bin\javac" -cp .;json-simple-1.1.1.jar JavaWorker.java
"%JDK_ROOT%\bin\javac" -cp .;re2j-1.8.jar;json-simple-1.1.1.jar RE2JWorker.java
"%JDK_ROOT%\bin\javac" -cp .;safere-0.10.0.jar;json-simple-1.1.1.jar SafeREWorker.java
"%JDK_ROOT%\bin\javac" -cp .;reggie-0.3.0.jar;json-simple-1.1.1.jar ReggieWorker.java
"%JDK_ROOT%\bin\javac" -cp .;joni-2.2.7.jar;json-simple-1.1.1.jar;jcodings-1.0.64.jar JoniWorker.java
