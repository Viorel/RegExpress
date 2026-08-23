@rem TODO: adjust the JDK_ROOT variable; See "Readme".
@set JDK_ROOT=T:\Limbaje\jdk-26.0.2.1
"%JDK_ROOT%\bin\jlink" --add-modules java.base,java.logging --output JRE-min --strip-debug --no-man-pages --no-header-files --compress=zip-0
@rem java.logging needed for Reggie
