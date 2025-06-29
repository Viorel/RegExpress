@rem TODO: adjust the JDK_ROOT variable; See "Readme".
@set JDK_ROOT=T:\Limbaje\jdk-24.0.1
"%JDK_ROOT%\bin\jlink" --add-modules java.base --output JRE-min --strip-debug --no-man-pages --no-header-files --compress=zip-0
