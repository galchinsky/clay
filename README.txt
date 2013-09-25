This is a fork/branch of Clay compiler in which all global variables are moved into module state structs. 

The upstream compiler lacks of modularity because it has a lot of global variables.
This makes hard to implement some features which are interesting to me, like JIT compilation during compile-time
(which actually means 2 versions of compiler that are run simulteniously, compile-time with the host platform and
run-time with the target platform)

* This allows using libc functions in compile time. For example, bindings could be automatically created using
file reading functions.
Or compile time functions can be debugged using anything you want (GNUplot? why not).
All these features can be done by implementing compiler primitives but I think it would be better to decrease
amount of things like that.

* evalJit primitive which is faster than eval

* Some fun bootstraping. The core compiler can be written in C++ as it is, and it is very useful,
but additional things like syntax desugaring can be written in "core clay".
In this case Clay.hpp should be transformed to make access from Clay possible, it can be done by clay-bindgen modifications.
Repl can be also done in Clay.

* This modularity can allow fixing some repl bugs

I used a simple approach to get rid of globals. Sometimes globals are moved to the structs that
are passed as a parameter in call-tree, sometimes
I used classes (i.e. state is passed by this) where it was more covinient (read: less text editting).
The approach is very straighforward and doesn't really modular because it saved previous compiler structure.
It was not pulled into the upstream
and it is hard to pull commits from there because a lot lines of code are changed.
I haven't liked any upstream commit since that time (except llvm-3.x updates) that's why my other pathes are
usually based on this branch.
