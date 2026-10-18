@echo {"pattern": "\\b", "text": "ab cd", "options" : { "FindAll" : true } } | adaworker.exe
@echo -------
@echo {"pattern": "a*", "text": "baab", "options" : { "FindAll" : true } } | adaworker.exe
@echo -------
@echo {"pattern": "a?", "text": "abc", "options" : { "FindAll" : true } } | adaworker.exe
@echo -------
@echo {"pattern": "b?", "text": "abc", "options" : { "FindAll" : true } } | adaworker.exe
@echo -------
@echo {"pattern": "c?", "text": "abc", "options" : { "FindAll" : true } } | adaworker.exe
@echo -------
@echo {"pattern": "x?", "text": "abc", "options" : { "FindAll" : true } } | adaworker.exe
