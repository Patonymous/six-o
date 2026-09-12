# Six-o

Six-o (pronounced sy-cho, like psycho, but no 'p') is a simple and lightweight tool for debugging Linux C++ applications.

At it's core it is a dynamic library, which can be LD_PRELOAD-ed when running the target program.
Six-o will then intercept calls to functions `cxa_throw` or `abort` and print the stacktrace at the moment of the call to stderr or a given file.

## Running

You can LD_PRELOAD six-o manually:

```sh
SIXO_OUTPUT=stacktraces.log LD_PRELOAD=six-o.so app
```

or you can run a shell tool:

```sh
six-o --output - app
```

Omitting the output env var or flag is the same as setting it to `-` which means outputting to stderr.

## Origins of the name

In Poland calling someone a `sixty` is slang way to describe a snitch.
This is because the sixtieth article of the polish Penal Code deals with crown witnesses.
