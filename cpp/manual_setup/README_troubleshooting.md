
### Load cmake: does not run
If reloading the CMake file hangs at the conan install step, try running the command 
printed to stdout by yourself. 

If you encounter
```bash
spdlog/1.10.0 is locked by another concurrent conan process, wait...
If not the case, quit, and do 'conan remove --locks'
```
try removing the locks via the aforementioned command. If that does not work, investigate and try removing manually:

```bash
$ which conan
$ cd ~/.conan 
$ find -name _.count
./data/catch2/2.13.9/_/_.count
./data/spdlog/1.10.0/_/_.count
./data/cli11/2.2.0/_/_.count
$ conan remove --locks
Cache locks removed
$ find -name _.count
./data/spdlog/1.10.0/_/_.count
$ rm ./data/spdlog/1.10.0/_/_.count
```

If that does not work 
```bash
cd ~/.conan
rm -rf data
```


### Conan install fails
conan.conf
```
[general]
revisions_enabled = 1 # XXX
```

### Update Conan
Many problems that users have can be resolved by updating Conan, so if you are
having any trouble with this project, you should start by doing that.

To update conan:

    pip install --user --upgrade conan

You may need to use `pip3` instead of `pip` in this command, depending on your
platform.

### Clear Conan cache
If you continue to have trouble with your Conan dependencies, you can try
clearing your Conan cache:

    conan remove -f '*'

The next time you run `cmake` or `cmake --build`, your Conan dependencies will
be rebuilt. If you aren't using your system's default compiler, don't forget to
set the CC, CXX, CMAKE_C_COMPILER, and CMAKE_CXX_COMPILER variables, as
described in the 'Build using an alternate compiler' section above.

### Identifying misconfiguration of Conan dependencies

If you have a dependency 'A' that requires a specific version of another
dependency 'B', and your project is trying to use the wrong version of
dependency 'B', Conan will produce warnings about this configuration error
when you run CMake. These warnings can easily get lost between a couple
hundred or thousand lines of output, depending on the size of your project.

If your project has a Conan configuration error, you can use `conan info` to
find it. `conan info` displays information about the dependency graph of your
project, with colorized output in some terminals.

    cd build
    conan info .

In my terminal, the first couple lines of `conan info`'s output show all of the
project's configuration warnings in a bright yellow font.

For example, the package `spdlog/1.5.0` depends on the package `fmt/6.1.2`.
If you were to modify the file `conanfile.py` so that it requires an
earlier version of `fmt`, such as `fmt/6.0.0`, and then run:

```bash
conan remove -f '*'       # clear Conan cache
rm -rf build              # clear previous CMake build
cmake -S . -B ./build     # rebuild Conan dependencies
conan info ./build
```

...the first line of output would be a warning that `spdlog` needs a more recent
version of `fmt`.


