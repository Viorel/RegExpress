@rem TODO: adjust the JDK_ROOT variable; See "Readme".
@set JDK_ROOT=T:\Limbaje\jdk-26.0.2.1
"%JDK_ROOT%\bin\jdeps" JavaWorker.class
"%JDK_ROOT%\bin\jdeps" RE2JWorker.class
"%JDK_ROOT%\bin\jdeps" SafeREWorker.class
"%JDK_ROOT%\bin\jdeps" ReggieWorker.class
"%JDK_ROOT%\bin\jdeps" JoniWorker.class
