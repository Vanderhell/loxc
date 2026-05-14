# Contributing to loxc

Thanks for taking the time to contribute.

## Reporting bugs

Use GitHub Issues to report bugs:
https://github.com/Vanderhell/loxc/issues

Include:
- what you expected
- what actually happened
- exact command line
- relevant logs or output
- operating system and compiler version

## Pull requests

1. Fork the repository and create a topic branch.
2. Make the smallest change that solves the problem.
3. Update tests and documentation when needed.
4. Open a pull request using the project template.

## Coding style

- C99 only
- compile with `-Wall -Wextra`
- no new warnings
- keep the code portable and explicit
- avoid hardcoded language assumptions

## Testing policy

Every pull request must pass:

```sh
make test
```

If the change touches build or example code, run the relevant extra target as well.

## Commit messages

Use short, imperative commit messages:

- `Add embedded table loader`
- `Fix CRC validation in CLI`
- `Update README examples`

Prefer one logical change per commit.
