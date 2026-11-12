@echo {"p" : "()", "t" : "b"} | DWorker.exe
@echo {"p" : "(a?)(b)", "t" : "b"} | DWorker.exe
@echo {"p" : "(a)?(b)", "t" : "b"} | DWorker.exe
@echo {"p" : "(a)(b?)(c)?(d)", "t" : "ad"} | DWorker.exe
@echo {"p" : "(?P<na>a)(?P<nb>b?)(?P<nc>c)?(?P<nd>d)", "t" : "ad"} | DWorker.exe