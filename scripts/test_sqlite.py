import subprocess

code = '''#include <sqlite3.h>
#include <cstdio>
int main() {
    printf("SQLite version: %s\\n", sqlite3_libversion());
    return 0;
}
'''

with open('build/test_sq.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

cmd = [
    'g++',
    '-O2',
    'build/test_sq.cpp',
    r'C:\Python314\DLLs\sqlite3.dll',
    '-Ithird_party',
    '-o',
    'build/sqlite_test.exe'
]

p = subprocess.run(cmd, capture_output=True, text=True)
print('Compile returncode:', p.returncode)
if p.returncode == 0:
    res = subprocess.run(['build/sqlite_test.exe'], capture_output=True, text=True)
    print('Execution output:', res.stdout.strip())
else:
    print('Compile error:', p.stderr)
