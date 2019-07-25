source pypy3-venv/bin/activate
scons --version
scons --clean
time scons -j8 debug=yes #  --debug=time
