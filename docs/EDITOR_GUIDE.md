# AX terminal editor

AX ships `axedit`, a native C++ terminal editor for `.ax` programs. It is designed for keyboard use in the terminal and needs no Python, Node.js, or editor plugin.

## Build and open a file

Build the project from its root, then open an existing or new source file:

```bash
cmake -S . -B build
cmake --build build --parallel 2
source scripts/ax-env.sh
ax edit examples/hello.ax
```

You can also launch the editor directly with `axedit examples/hello.ax`. `ax edit` starts the `axedit` executable beside the `ax` compiler, so both work with the repository's `build/` directory.

## Editing and suggestions

The editor updates completions as you type. It combines AX keywords and built-ins with function signatures, user-defined types, parameters, and variables found in the current file. Candidate details show the declared or inferred type. The status bar follows the symbol at the cursor and displays its type or function signature.

For example, after typing `pri`, the list offers `print` and `println`, with their call signatures. After writing a declaration such as `values: Tensor[f32, 8] = ...`, placing the cursor on `values` shows its tensor type. Simple literal assignments and calls to functions with a return annotation also provide inferred hints.

Completion selection replaces the partial word at the cursor. It inserts the candidate name, leaving parentheses and arguments for you to write. The editor indents a new line to match the previous line and adds one level after a colon.

## Colors

AX source uses separate terminal colors for:

- Declarations such as `fn`, `struct`, `enum`, `let`, and `const`.
- Control flow such as `if`, `else`, `for`, `while`, and `return`.
- Concurrency keywords such as `spawn`, `await`, `yield`, `select`, and `chan`.
- Accelerator keywords such as `distribute`, `mesh`, and `shard`.
- Built-in and user-defined types, functions, and decorators.
- Strings, numbers, comments, and operators.

## Keys

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor; move through completion choices while the list is open |
| Home / End | Move to the start or end of a line |
| Page Up / Page Down | Move by one screen |
| Enter | Insert a line break, or accept the selected completion |
| Tab | Accept the selected completion; otherwise insert four spaces |
| Ctrl-Space | Show suggestions at the cursor, including with an empty prefix |
| Ctrl-N / Ctrl-P | Move to the next or previous suggestion |
| Escape | Close the suggestion list |
| Ctrl-S / Ctrl-O | Save the current file |
| Ctrl-Q / Ctrl-X | Quit; press again to discard unsaved changes |

## Type hint scope

Hints are derived from the current file's declarations and AX's built-in signatures. They cover annotated function parameters and returns, typed local declarations, simple scalar and tensor literals, and straightforward variable or function-call assignments. This first editor version does not run a full semantic analysis after each keystroke, does not resolve imported modules, and does not yet show compiler diagnostics inline. Use `ax check file.ax` to run the compiler's complete checks.
