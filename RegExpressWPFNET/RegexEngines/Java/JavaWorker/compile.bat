@rem TODO: adjust the JDK_ROOT variable; See "Readme".
@set JDK_ROOT=T:\Limbaje\jdk-23.0.2
"%JDK_ROOT%\bin\javac" JavaWorker.java
"%JDK_ROOT%\bin\javac" -cp .;re2j-1.8.jar RE2JWorker.java