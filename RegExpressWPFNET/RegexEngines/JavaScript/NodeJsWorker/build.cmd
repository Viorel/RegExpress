:: https://nodejs.org/api/single-executable-applications.html
call node --experimental-sea-config sea-config.json 
call node -e "require('fs').copyFileSync(process.execPath, 'node-copy.exe')"
call npx postject "node-copy.exe" NODE_SEA_BLOB "sea-prep.blob" --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2
move /Y "node-copy.exe" "NodeJsWorker.exe"