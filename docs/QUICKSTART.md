# loxc - 5-Minute Quickstart

## Step 1: Build

```bash
git clone https://github.com/Vanderhell/loxc
cd loxc
make
```

You now have `libloxc.a` and the demo module ready.

## Step 2: Try the CLI

```bash
echo "Hello from loxc!" > test.txt
./tools/loxc_cli compress --table modules/loxc_demo.loxctab --embed \
    test.txt test.loxc
./tools/loxc_cli decompress test.loxc restored.txt
diff test.txt restored.txt && echo "Works!"
```

## Step 3: Use as a library

Create `myapp.c`:

```c
#include "loxc_simple.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
    const char *text = "Compress me!";
    loxc_buffer_t out = loxc_compress_buffer(ctx, text, strlen(text), 0);

    printf("Original: %zu, Compressed: %zu\n", strlen(text), out.size);

    loxc_buffer_free(&out);
    loxc_close(ctx);
    return 0;
}
```

Compile:

```bash
cc -Iinclude -Imodules myapp.c libloxc.a -o myapp
./myapp
```

That's it. You are using loxc.

## Step 4: Train your own module

```bash
./tools/loxc_train \
    --input your_text.txt \
    --output modules/loxc_myapp \
    --module-name myapp --module-id 50
```

You now have `modules/loxc_myapp.loxctab`. Use it instead of the demo table.

## What's next?

- See [../examples/](../examples/) for 7 progressively more advanced examples
- See [COOKBOOK.md](COOKBOOK.md) for copy-paste recipes
- See [API.md](API.md) for the full API reference
