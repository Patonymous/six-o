# Six-o

Six-o (pronounced sy-cho, like psycho, but no 'p') is a simple and lightweight tool for debugging Linux C++ applications.

It has two parts:

- a **consumer** (`six-o`) that runs your program and prints symbolized stacktraces
- a **producer** (`six-o.so`) that is `LD_PRELOAD`-ed into the target by the consumer

The producer intercepts `__cxa_throw` and selected fatal signals, captures a PC backtrace and sends it to the consumer over a pipe.
The consumer resolves addresses with `llvm-symbolizer` (or `addr2line` as a fallback) and writes the result to stderr or a file.

This tool was develepoed to help me debug my C++ uni projects.
The idea was organic, but implementation is almost 100% machine vibes.
Maybe one day I'll clean it up.

## Running

```sh
make
./build/six-o --output stacktraces.log ./your_app
```

```sh
./build/six-o --output - --signals SEGV,BUS,ILL,FPE,ABRT ./your_app
```

Omitting `--output` / `SIXO_OUTPUT` is the same as `-` (stderr).

Default `--signals` / `SIXO_SIGNALS` is `SEGV,BUS,ILL,FPE,ABRT`.

## Origins of the name

In Poland calling someone a `sixty` is slang way to describe a snitch.
This is because the sixtieth article of the polish Penal Code deals with crown witnesses.
